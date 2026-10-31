#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CitadelFormationLibrary.generated.h"

UCLASS()
class CITADELRTS_API UCitadelFormationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spreads one group order into a destination per unit: a grid centred on Target, facing from the group
	 * towards it, spaced by the largest unit and projected onto the navmesh. Units are matched to slots by
	 * their current position (front units to the front row, left to right), so their paths do not cross.
	 * Destinations[i] belongs to Units[i]; null entries and slots off the navmesh get Target.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Formation", meta = (WorldContext = "WorldContextObject"))
	static void ComputeFormationDestinations(const UObject* WorldContextObject, const TArray<AActor*>& Units, FVector Target, TArray<FVector>& Destinations, float SpacingScale = 1.15f);

	/** Collision radius of an actor: the scaled capsule for units, the bounds of colliding components otherwise. */
	UFUNCTION(BlueprintPure, Category = "Citadel|Formation")
	static float GetFootprintRadius(const AActor* Actor);
};
