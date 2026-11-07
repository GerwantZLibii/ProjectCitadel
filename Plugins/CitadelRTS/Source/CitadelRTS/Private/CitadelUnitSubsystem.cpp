#include "CitadelUnitSubsystem.h"
#include "CitadelCombatComponent.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"

namespace
{
	bool IsNearAny(const TArray<FVector>& Points, const FVector& Point, float Distance)
	{
		for (const FVector& Other : Points)
		{
			if (FVector::DistSquared2D(Point, Other) < FMath::Square(Distance))
			{
				return true;
			}
		}
		return false;
	}
}

void UCitadelUnitSubsystem::Register(UCitadelCombatComponent* Combatant)
{
	if (Combatant)
	{
		Combatants.AddUnique(Combatant);
	}
}

void UCitadelUnitSubsystem::Unregister(UCitadelCombatComponent* Combatant)
{
	Combatants.RemoveAllSwap([Combatant](const TWeakObjectPtr<UCitadelCombatComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Combatant;
	});
}

AActor* UCitadelUnitSubsystem::FindNearestHostile(const UCitadelCombatComponent* Requester, float Radius) const
{
	const AActor* Self = Requester ? Requester->GetOwner() : nullptr;
	if (!Self)
	{
		return nullptr;
	}

	const FVector Origin = Self->GetActorLocation();
	AActor* Best = nullptr;
	int32 BestPriority = 0;
	double BestDistance = 0.0;
	for (const TWeakObjectPtr<UCitadelCombatComponent>& Entry : Combatants)
	{
		const UCitadelCombatComponent* Other = Entry.Get();
		AActor* OtherActor = Other ? Other->GetOwner() : nullptr;
		if (!OtherActor || Other == Requester || !Other->IsAlive() || !Requester->IsHostileTo(Other) || Requester->IsIgnoring(OtherActor))
		{
			continue;
		}

		const double Distance = FVector::Dist2D(Origin, OtherActor->GetActorLocation()) - Other->GetFootprintRadius();
		if (Distance > Radius)
		{
			continue;
		}
		if (!Best || Other->TargetPriority > BestPriority || (Other->TargetPriority == BestPriority && Distance < BestDistance))
		{
			Best = OtherActor;
			BestPriority = Other->TargetPriority;
			BestDistance = Distance;
		}
	}
	return Best;
}

FVector UCitadelUnitSubsystem::ClaimMoveSlot(const APawn* Unit, const FVector& Goal)
{
	UWorld* World = GetWorld();
	if (!Unit || !World)
	{
		return Goal;
	}

	GroupMoves.RemoveAllSwap([](const FCitadelGroupMove& Group) { return Group.Frame != GFrameCounter; });
	FCitadelGroupMove* Group = GroupMoves.FindByPredicate([&Goal](const FCitadelGroupMove& Candidate)
	{
		return FVector::DistSquared2D(Candidate.Goal, Goal) < FMath::Square(10.f);
	});
	if (!Group)
	{
		Group = &GroupMoves.AddDefaulted_GetRef();
		Group->Goal = Goal;
		Group->Frame = GFrameCounter;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	Unit->GetSimpleCollisionCylinder(Radius, HalfHeight);
	Group->Spacing = FMath::Max(Group->Spacing, Radius * 2.f * 1.15f);
	Group->Units.Add(Unit);

	// Units of the same order do not block each other's spots; everyone else does.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CitadelMoveSlot), false);
	for (const AActor* Member : Group->Units)
	{
		QueryParams.AddIgnoredActor(Member);
	}
	const FCollisionShape Footprint = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FCollisionObjectQueryParams OtherUnits(ECC_Pawn);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const float Spacing = Group->Spacing;
	const FVector ProjectExtent(Spacing * 0.5f, Spacing * 0.5f, 500.f);
	const FVector From = Unit->GetActorLocation();

	// Smallest number of rings holding one more spot than already claimed: ring R has 6R spots.
	int32 FillRing = 0;
	while (1 + 3 * FillRing * (FillRing + 1) <= Group->Slots.Num())
	{
		++FillRing;
	}

	for (int32 MaxRing = FillRing; MaxRing <= FillRing + 3; ++MaxRing)
	{
		FVector Best = Goal;
		double BestDistSq = TNumericLimits<double>::Max();
		for (int32 Ring = 0; Ring <= MaxRing; ++Ring)
		{
			const int32 Samples = Ring == 0 ? 1 : 6 * Ring;
			for (int32 Sample = 0; Sample < Samples; ++Sample)
			{
				// Rings are rotated against each other so their spots interleave.
				const float Angle = 2.f * UE_PI * Sample / Samples + Ring * 0.5f;
				FVector Spot = Goal + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (Ring * Spacing);
				if (NavSys)
				{
					FNavLocation OnNavMesh;
					if (!NavSys->ProjectPointToNavigation(Spot, OnNavMesh, ProjectExtent))
					{
						continue;
					}
					Spot = OnNavMesh.Location;
				}

				const double DistSq = FVector::DistSquared2D(From, Spot);
				if (DistSq >= BestDistSq || IsNearAny(Group->Slots, Spot, Spacing * 0.9f))
				{
					continue;
				}
				if (World->OverlapAnyTestByObjectType(Spot + FVector(0.f, 0.f, HalfHeight + 5.f), FQuat::Identity, OtherUnits, Footprint, QueryParams))
				{
					continue;
				}
				Best = Spot;
				BestDistSq = DistSq;
			}
		}
		if (BestDistSq < TNumericLimits<double>::Max())
		{
			Group->Slots.Add(Best);
			return Best;
		}
	}

	Group->Slots.Add(Goal);
	return Goal;
}

TArray<AActor*> UCitadelUnitSubsystem::GetActorsOfTeam(int32 Team) const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<UCitadelCombatComponent>& Entry : Combatants)
	{
		const UCitadelCombatComponent* Combatant = Entry.Get();
		if (Combatant && Combatant->IsAlive() && (Team < 0 || Combatant->TeamId == Team))
		{
			if (AActor* OwningActor = Combatant->GetOwner())
			{
				Result.Add(OwningActor);
			}
		}
	}
	return Result;
}
