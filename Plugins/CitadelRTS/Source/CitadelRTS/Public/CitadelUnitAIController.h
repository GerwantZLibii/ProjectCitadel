#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CitadelUnitAIController.generated.h"

/** Mirrors ECrowdAvoidanceQuality so it can be edited in the details panel. */
UENUM(BlueprintType)
enum class ECitadelAvoidanceQuality : uint8
{
	Low,
	Medium,
	Good,
	High,
};

/**
 * AI controller using Detour Crowd avoidance whose ranges follow the size of the possessed unit.
 *
 * The stock crowd agent only considers neighbours within a fixed 400 uu, which is less than the
 * distance at which two scaled-up units already touch. Here the ranges are a multiple of the
 * unit's collision radius instead. Reparent AIController_Unit to this class.
 */
UCLASS()
class CITADELRTS_API ACitadelUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACitadelUnitAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Re-reads the pawn's collision radius and pushes the crowd settings; call after rescaling a unit at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Crowd")
	void ApplyCrowdSettings();

protected:
	virtual void OnPossess(APawn* InPawn) override;

	/** Neighbours closer than radius * this are avoided. Detour's reference value is 12. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Crowd", meta = (ClampMin = "1"))
	float CollisionQueryRangeScale = 12.f;

	/** How far ahead (radius * this) the path corridor is shortcut. Detour's reference value is 30. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Crowd", meta = (ClampMin = "1"))
	float PathOptimizationRangeScale = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Crowd")
	ECitadelAvoidanceQuality AvoidanceQuality = ECitadelAvoidanceQuality::High;

	/** Pushes agents apart when they bunch up, e.g. at a shared destination. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Crowd")
	bool bEnableSeparation = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Crowd", meta = (ClampMin = "0", EditCondition = "bEnableSeparation"))
	float SeparationWeight = 2.f;
};
