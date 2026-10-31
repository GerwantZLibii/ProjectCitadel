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
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		if (const AActor* Unit = Units[Index])
		{
			Members.Add(Index);
			Centroid += Unit->GetActorLocation();
			MaxRadius = FMath::Max(MaxRadius, GetFootprintRadius(Unit));
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
			FVector Slot = Target + Forward * RowOffset + Right * ColumnOffset;
			if (NavSys)
			{
				FNavLocation OnNavMesh;
				Slot = NavSys->ProjectPointToNavigation(Slot, OnNavMesh, ProjectExtent) ? OnNavMesh.Location : Target;
			}
			Destinations[RowMembers[Column]] = Slot;
		}
	}
}
