#include "CitadelUnitAIController.h"
#include "CitadelCombatComponent.h"
#include "CitadelRTS.h"
#include "CitadelUnitSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

ACitadelUnitAIController::ACitadelUnitAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void ACitadelUnitAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplyCrowdSettings();
	ApplySmoothTurning();
	ResolveSpawnOverlap();

	if (bYieldToBlockedUnits)
	{
		// Random first delay spreads the checks of units spawned on the same frame.
		GetWorldTimerManager().SetTimer(YieldTimer, this, &ACitadelUnitAIController::UpdateYield, YieldCheckInterval, true, FMath::FRandRange(0.f, YieldCheckInterval));
	}
}

void ACitadelUnitAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(YieldTimer);
	Super::OnUnPossess();
}

FPathFollowingRequestResult ACitadelUnitAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
	UWorld* World = GetWorld();
	UCitadelUnitSubsystem* Units = World ? World->GetSubsystem<UCitadelUnitSubsystem>() : nullptr;
	if (!bSpreadMoves || !Units || !GetPawn() || !MoveRequest.IsValid() || MoveRequest.IsMoveToActorRequest())
	{
		return Super::MoveTo(MoveRequest, OutPath);
	}

	FAIMoveRequest Spread = MoveRequest;
	Spread.UpdateGoalLocation(Units->ClaimMoveSlot(GetPawn(), MoveRequest.GetGoalLocation()));
	return Super::MoveTo(Spread, OutPath);
}

void ACitadelUnitAIController::ApplyCrowdSettings()
{
	UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	const APawn* ControlledPawn = GetPawn();
	if (!Crowd || !ControlledPawn)
	{
		return;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	ControlledPawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
	if (Radius <= 0.f)
	{
		UE_LOG(LogCitadelRTS, Warning, TEXT("%s: %s has no collision radius, crowd settings left unchanged"), *GetName(), *ControlledPawn->GetName());
		return;
	}

	// Only the last call pushes the parameters to the crowd manager; the agent may also not be registered yet,
	// in which case the values are picked up on registration.
	Crowd->SetCrowdRotateToVelocity(bFaceVelocity);
	Crowd->SetCrowdCollisionQueryRange(Radius * CollisionQueryRangeScale, false);
	Crowd->SetCrowdPathOptimizationRange(Radius * PathOptimizationRangeScale, false);
	Crowd->SetCrowdAvoidanceQuality(static_cast<ECrowdAvoidanceQuality::Type>(static_cast<int32>(AvoidanceQuality)), false);
	Crowd->SetCrowdSeparation(bEnableSeparation, false);
	Crowd->SetCrowdSeparationWeight(SeparationWeight, true);
}

void ACitadelUnitAIController::ApplySmoothTurning()
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = ControlledCharacter ? ControlledCharacter->GetCharacterMovement() : nullptr;
	if (!bSmoothTurning || !Movement)
	{
		return;
	}

	// The controller still decides where to look (path direction, or a focus such as an attack target);
	// the movement component turns the pawn there at a limited rate.
	ControlledCharacter->bUseControllerRotationYaw = false;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = true;
	Movement->RotationRate = FRotator(0.f, TurnRate, 0.f);
}

void ACitadelUnitAIController::ResolveSpawnOverlap()
{
	APawn* ControlledPawn = GetPawn();
	UWorld* World = GetWorld();
	UCitadelUnitSubsystem* Units = World ? World->GetSubsystem<UCitadelUnitSubsystem>() : nullptr;
	if (!bResolveSpawnOverlap || !ControlledPawn || !Units)
	{
		return;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	ControlledPawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const FVector Location = ControlledPawn->GetActorLocation();
	// Slightly smaller than the unit, so a neighbour that merely touches does not count.
	const FCollisionShape Footprint = FCollisionShape::MakeCapsule(Radius * 0.9f, HalfHeight * 0.9f);
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CitadelSpawnOverlap), false, ControlledPawn);
	if (!World->OverlapAnyTestByObjectType(Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), Footprint, QueryParams))
	{
		return;
	}

	const FVector Free = Units->ClaimMoveSlot(ControlledPawn, Location);
	if (!Free.Equals(Location))
	{
		ControlledPawn->SetActorLocation(Free + FVector(0.f, 0.f, HalfHeight + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ACitadelUnitAIController::UpdateYield()
{
	APawn* ControlledPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!bCanYield || !ControlledPawn || !World || GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		return;
	}
	if (World->GetTimeSeconds() - LastYieldTime < YieldCooldown)
	{
		return;
	}
	const UCitadelCombatComponent* Combat = ControlledPawn->FindComponentByClass<UCitadelCombatComponent>();
	if (Combat && Combat->GetState() != ECitadelCombatState::Idle)
	{
		return;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	ControlledPawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const FVector Location = ControlledPawn->GetActorLocation();

	const TArray<TEnumAsByte<EObjectTypeQuery>> PawnTypes = { UEngineTypes::ConvertToObjectType(ECC_Pawn) };
	const TArray<AActor*> Ignored = { ControlledPawn };
	TArray<AActor*> Touching;
	UKismetSystemLibrary::SphereOverlapActors(this, Location, Radius * 1.5f, PawnTypes, APawn::StaticClass(), Ignored, Touching);

	for (AActor* Actor : Touching)
	{
		const APawn* Mover = Cast<APawn>(Actor);
		const AAIController* MoverAI = Mover ? Cast<AAIController>(Mover->GetController()) : nullptr;
		const UPathFollowingComponent* MoverPath = MoverAI ? MoverAI->GetPathFollowingComponent() : nullptr;
		if (!MoverPath || MoverPath->GetStatus() != EPathFollowingStatus::Moving)
		{
			continue;
		}

		const UCitadelCombatComponent* MoverCombat = Mover->FindComponentByClass<UCitadelCombatComponent>();
		if (Combat && MoverCombat && Combat->IsHostileTo(MoverCombat))
		{
			continue;
		}

		// Only a unit heading into us and slowed down by us; one that slides past needs nothing.
		const FVector MoveDirection = MoverPath->GetCurrentDirection().GetSafeNormal2D();
		const FVector ToSelf = Location - Mover->GetActorLocation();
		const UPawnMovementComponent* MoverMovement = Mover->GetMovementComponent();
		const float MoverMaxSpeed = MoverMovement ? MoverMovement->GetMaxSpeed() : 0.f;
		if (MoveDirection.IsNearlyZero() || FVector::DotProduct(ToSelf.GetSafeNormal2D(), MoveDirection) < 0.5 || Mover->GetVelocity().Size2D() > MoverMaxSpeed * 0.5f)
		{
			continue;
		}

		float MoverRadius = 0.f;
		float MoverHalfHeight = 0.f;
		Mover->GetSimpleCollisionCylinder(MoverRadius, MoverHalfHeight);
		const float Step = (Radius + MoverRadius) * 1.2f;

		// Step off its path to the side we already lean to; the other side if that one is off the navmesh.
		FVector Side = FVector::CrossProduct(FVector::UpVector, MoveDirection);
		if (FVector::DotProduct(ToSelf, Side) < 0.0)
		{
			Side = -Side;
		}
		if (TryStepAside(Location + Side * Step, Radius) || TryStepAside(Location - Side * Step, Radius))
		{
			LastYieldTime = World->GetTimeSeconds();
		}
		return;
	}
}

bool ACitadelUnitAIController::TryStepAside(const FVector& Goal, float Radius)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation OnNavMesh;
	if (!NavSys || !NavSys->ProjectPointToNavigation(Goal, OnNavMesh, FVector(Radius, Radius, 300.f)))
	{
		return false;
	}
	// Exactly this spot, and strafing: the unit sidesteps instead of turning its back on the one passing.
	TGuardValue<bool> ExactMove(bSpreadMoves, false);
	return MoveToLocation(OnNavMesh.Location, Radius * 0.25f, false, true, false, true) == EPathFollowingRequestResult::RequestSuccessful;
}
