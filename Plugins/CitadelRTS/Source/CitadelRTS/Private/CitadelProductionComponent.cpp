#include "CitadelProductionComponent.h"
#include "AIController.h"
#include "CitadelResourceOwner.h"
#include "CitadelUnitDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"

UCitadelProductionComponent::UCitadelProductionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UCitadelProductionComponent::Enqueue(UCitadelUnitDefinition* Unit)
{
	if (!Unit || !Unit->UnitClass || Queue.Num() >= MaxQueueSize)
	{
		return false;
	}
	if (ProducibleUnits.Num() > 0 && !ProducibleUnits.Contains(Unit))
	{
		return false;
	}
	if (UObject* Payer = GetResourceOwner())
	{
		if (!ICitadelResourceOwner::Execute_TrySpendResources(Payer, Unit->Cost))
		{
			return false;
		}
	}

	Queue.AddDefaulted_GetRef().Unit = Unit;
	SetComponentTickEnabled(true);
	OnQueueChanged.Broadcast();
	return true;
}

bool UCitadelProductionComponent::CancelOrder(int32 Index)
{
	if (!Queue.IsValidIndex(Index))
	{
		return false;
	}

	UCitadelUnitDefinition* Unit = Queue[Index].Unit;
	Queue.RemoveAt(Index);
	if (Index == 0)
	{
		SetBlocked(Unit, false);
		RetryCooldown = 0.f;
	}
	if (UObject* Payer = Unit ? GetResourceOwner() : nullptr)
	{
		ICitadelResourceOwner::Execute_RefundResources(Payer, Unit->Cost);
	}
	SetComponentTickEnabled(Queue.Num() > 0);
	OnQueueChanged.Broadcast();
	return true;
}

void UCitadelProductionComponent::SetRallyPoint(FVector Location)
{
	RallyPoint = Location;
	bHasRallyPoint = true;
}

void UCitadelProductionComponent::ClearRallyPoint()
{
	bHasRallyPoint = false;
}

bool UCitadelProductionComponent::GetRallyPoint(FVector& Location) const
{
	Location = RallyPoint;
	return bHasRallyPoint;
}

float UCitadelProductionComponent::GetCurrentProgress() const
{
	if (Queue.Num() == 0 || !Queue[0].Unit)
	{
		return 0.f;
	}
	const float BuildTime = Queue[0].Unit->BuildTime;
	return BuildTime > 0.f ? FMath::Clamp(Queue[0].Progress / BuildTime, 0.f, 1.f) : 1.f;
}

void UCitadelProductionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Queue.Num() == 0)
	{
		SetComponentTickEnabled(false);
		return;
	}

	FCitadelProductionOrder& Current = Queue[0];
	UCitadelUnitDefinition* Unit = Current.Unit;
	if (!Unit || !Unit->UnitClass)
	{
		Queue.RemoveAt(0);
		OnQueueChanged.Broadcast();
		return;
	}

	Current.Progress = FMath::Min(Current.Progress + DeltaTime, Unit->BuildTime);
	if (Current.Progress < Unit->BuildTime)
	{
		return;
	}

	RetryCooldown -= DeltaTime;
	if (RetryCooldown > 0.f)
	{
		return;
	}

	APawn* Spawned = SpawnUnit(Unit);
	if (!Spawned)
	{
		RetryCooldown = SpawnRetryInterval;
		SetBlocked(Unit, true);
		return;
	}

	// Leave the queue consistent before handing control to listeners, which may queue or cancel orders.
	Queue.RemoveAt(0);
	RetryCooldown = 0.f;
	SetBlocked(Unit, false);
	OnUnitProduced.Broadcast(Spawned, Unit);
	OnQueueChanged.Broadcast();
}

APawn* UCitadelProductionComponent::SpawnUnit(UCitadelUnitDefinition* Unit) const
{
	UWorld* World = GetWorld();
	const AActor* Building = GetOwner();
	FVector Location;
	if (!World || !Building || !FindSpawnLocation(Unit, Location))
	{
		return nullptr;
	}

	// Face away from the building so the unit walks out instead of turning first.
	const FRotator Rotation(0.f, (Location - Building->GetActorLocation()).Rotation().Yaw, 0.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	APawn* Pawn = World->SpawnActor<APawn>(Unit->UnitClass, Location, Rotation, Params);
	if (!Pawn)
	{
		return nullptr;
	}

	if (!Pawn->GetController())
	{
		Pawn->SpawnDefaultController();
	}
	if (bMoveToRallyPoint && bHasRallyPoint)
	{
		if (AAIController* AI = Cast<AAIController>(Pawn->GetController()))
		{
			AI->MoveToLocation(RallyPoint, -1.f, true, true, true, false);
		}
	}
	return Pawn;
}

bool UCitadelProductionComponent::FindSpawnLocation(const UCitadelUnitDefinition* Unit, FVector& Location) const
{
	UWorld* World = GetWorld();
	const AActor* Building = GetOwner();
	if (!Unit || !World || !Building)
	{
		return false;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	Unit->GetFootprint(Radius, HalfHeight);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FCollisionObjectQueryParams OtherUnits(ECC_Pawn);
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CitadelSpawnLocation), false, Building);
	const FVector ProjectExtent(Radius, Radius, 500.f);
	const float RingStep = Radius * 2.2f;
	const TArray<FVector> Exits = GetExitPoints();

	// Nearest rings first across all exits, so the closest free spot to any exit wins.
	for (int32 Ring = 0; Ring <= SearchRings; ++Ring)
	{
		const int32 Samples = Ring == 0 ? 1 : 6 * Ring;
		for (const FVector& Exit : Exits)
		{
			for (int32 Sample = 0; Sample < Samples; ++Sample)
			{
				const float Angle = 2.f * UE_PI * Sample / Samples;
				FVector Candidate = Exit + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (Ring * RingStep);

				// The navmesh already keeps static geometry (buildings, rocks) out; only other units are tested here.
				if (NavSys)
				{
					FNavLocation OnNavMesh;
					if (!NavSys->ProjectPointToNavigation(Candidate, OnNavMesh, ProjectExtent))
					{
						continue;
					}
					Candidate = OnNavMesh.Location;
				}
				Candidate.Z += HalfHeight + 5.f;

				if (!World->OverlapAnyTestByObjectType(Candidate, FQuat::Identity, OtherUnits, Capsule, QueryParams))
				{
					Location = Candidate;
					return true;
				}
			}
		}
	}
	return false;
}

TArray<FVector> UCitadelProductionComponent::GetExitPoints() const
{
	TArray<FVector> Points;
	if (const AActor* Building = GetOwner())
	{
		for (UActorComponent* Component : Building->GetComponentsByTag(USceneComponent::StaticClass(), ExitPointTag))
		{
			Points.Add(CastChecked<USceneComponent>(Component)->GetComponentLocation());
		}
		if (Points.Num() == 0)
		{
			Points.Add(Building->GetActorLocation());
		}
	}
	return Points;
}

UObject* UCitadelProductionComponent::GetResourceOwner() const
{
	UObject* Candidate = ResourceOwner;
	if (!Candidate)
	{
		if (const UWorld* World = GetWorld())
		{
			Candidate = World->GetFirstPlayerController();
		}
	}
	return Candidate && Candidate->GetClass()->ImplementsInterface(UCitadelResourceOwner::StaticClass()) ? Candidate : nullptr;
}

void UCitadelProductionComponent::SetBlocked(UCitadelUnitDefinition* Unit, bool bNewBlocked)
{
	if (bBlocked != bNewBlocked)
	{
		bBlocked = bNewBlocked;
		OnProductionBlocked.Broadcast(Unit, bNewBlocked);
	}
}
