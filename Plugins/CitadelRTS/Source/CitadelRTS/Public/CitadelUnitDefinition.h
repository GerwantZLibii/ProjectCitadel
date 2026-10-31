#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "CitadelUnitDefinition.generated.h"

class APawn;
class UTexture2D;

/** Everything needed to show and produce one unit type; one asset per unit replaces hard-coded switches. */
UCLASS(BlueprintType)
class CITADELRTS_API UCitadelUnitDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unit")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unit")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unit")
	TSubclassOf<APawn> UnitClass;

	/** Resource name (e.g. Gold, Stone) to amount; paid through ICitadelResourceOwner when the unit is queued. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Production")
	TMap<FName, int32> Cost;

	/** Seconds from the front of the queue to the unit leaving the building. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Production", meta = (ClampMin = "0"))
	float BuildTime = 5.f;

	/** Radius used when looking for free ground to spawn on; 0 reads it from the unit's capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Production", meta = (ClampMin = "0"))
	float FootprintRadiusOverride = 0.f;

	/** Collision size of the unit as it will be spawned (capsule radius and half height, including scale). */
	UFUNCTION(BlueprintPure, Category = "Citadel|Unit")
	void GetFootprint(float& Radius, float& HalfHeight) const;
};
