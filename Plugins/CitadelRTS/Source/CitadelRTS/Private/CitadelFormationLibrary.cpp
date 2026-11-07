#include "CitadelFormationLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NavigationSystem.h"

float UCitadelFormationLibrary::GetFootprintRadius(const AActor* Actor)
{
	float Radius = 0.f;
	float HalfHeight = 0.f;
	if (Actor)
	{
		Actor->GetSimpleCollisionCylinder(Radius, HalfHeight);
	}
	return Radius;
}

void UCitadelFormationLibrary::ComputeFormationDestinations(const UObject* WorldContextObject, const TArray<AActor*>& Units, FVector Target, TArray<FVector>& Destinations, float SpacingScale)
{
	Destinations.Init(Target, Units.Num());

	TArray<int32> Members;
	FVector Centroid = FVector::ZeroVector;
	float MaxRadius = 0.f;
	float MaxHalfHeight = 0.f;
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		if (const AActor* Unit = Units[Index])
		{
			float Radius = 0.f;
			float HalfHeight = 0.f;
			Unit->GetSimpleCollisionCylinder(Radius, HalfHeight);
			Members.Add(Index);
			Centroid += Unit->GetActorLocation();
			MaxRadius = FMath::Max(MaxRadius, Radius);
			MaxHalfHeight = FMath::Max(MaxHalfHeight, HalfHeight);
		}
	}
	if (Members.Num() == 0)
	{
		return;
	}
	Centroid /= Members.Num();

	FVector Forward = (Target - Centroid).GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const float Spacing = FMath::Max(2.f * MaxRadius * SpacingScale, 1.f);

	const int32 Count = Members.Num();
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);

	// Units furthest along the direction of travel take the front row.
	Members.Sort([&Units, &Centroid, &Forward](int32 A, int32 B)
	{
		return FVector::DotProduct(Units[A]->GetActorLocation() - Centroid, Forward) > FVector::DotProduct(Units[B]->GetActorLocation() - Centroid, Forward);
	});

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const FVector ProjectExtent(Spacing * 0.5f, Spacing * 0.5f, 500.f);

	// A slot is free when no unit outside this order stands there and no other slot of the order is too close.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CitadelFormationSlot), false);
	for (int32 Index : Members)
	{
		QueryParams.AddIgnoredActor(Units[Index]);
	}
	const FCollisionShape Footprint = FCollisionShape::MakeCapsule(MaxRadius, MaxHalfHeight);
	const FCollisionObjectQueryParams OtherUnits(ECC_Pawn);
	TArray<FVector> Taken;
	auto IsFree = [&](const FVector& Point)
	{
		for (const FVector& Other : Taken)
		{
			if (FVector::DistSquared2D(Point, Other) < FMath::Square(Spacing * 0.9f))
			{
				return false;
			}
		}
		return !World || !World->OverlapAnyTestByObjectType(Point + FVector(0.f, 0.f, MaxHalfHeight + 5.f), FQuat::Identity, OtherUnits, Footprint, QueryParams);
	};

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const int32 First = Row * Columns;
		const int32 InRow = FMath::Min(Columns, Count - First);

		// Within a row, keep the units' left-to-right order.
		TArray<int32> RowMembers(&Members[First], InRow);
		RowMembers.Sort([&Units, &Centroid, &Right](int32 A, int32 B)
		{
			return FVector::DotProduct(Units[A]->GetActorLocation() - Centroid, Right) < FVector::DotProduct(Units[B]->GetActorLocation() - Centroid, Right);
		});

		const float RowOffset = ((Rows - 1) * 0.5f - Row) * Spacing;
		for (int32 Column = 0; Column < InRow; ++Column)
		{
			const float ColumnOffset = (Column - (InRow - 1) * 0.5f) * Spacing;
			const FVector Ideal = Target + Forward * RowOffset + Right * ColumnOffset;

			// Look for room around the ideal slot, nearest first; with none, keep the ideal slot anyway.
			FVector Slot = Target;
			bool bPlaced = false;
			for (int32 Ring = 0; Ring <= 3 && !bPlaced; ++Ring)
			{
				const int32 Samples = Ring == 0 ? 1 : 8;
				for (int32 Sample = 0; Sample < Samples && !bPlaced; ++Sample)
				{
					const float Angle = 2.f * UE_PI * Sample / Samples;
					FVector Candidate = Ideal + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (Ring * Spacing * 0.5f);
					if (NavSys)
					{
						FNavLocation OnNavMesh;
						if (!NavSys->ProjectPointToNavigation(Candidate, OnNavMesh, ProjectExtent))
						{
							continue;
						}
						Candidate = OnNavMesh.Location;
					}
					if (Ring == 0)
					{
						Slot = Candidate;
					}
					bPlaced = IsFree(Candidate);
					if (bPlaced)
					{
						Slot = Candidate;
					}
				}
			}
			Taken.Add(Slot);
			Destinations[RowMembers[Column]] = Slot;
		}
	}
}
