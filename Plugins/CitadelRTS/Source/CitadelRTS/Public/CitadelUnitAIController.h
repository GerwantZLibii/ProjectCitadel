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
 * unit's collision radius instead. It also turns the unit smoothly and, while the unit stands,
 * makes it step aside for a friendly unit that is stuck pushing into it.
 * Reparent AIController_Unit to this class.
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

	/** Runtime switch for stepping aside, e.g. off while a unit must hold its spot. */
	UPROPERTY(BlueprintReadWrite, Category = "Citadel|Yield")
	bool bCanYield = true;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

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

	/**
	 * Face the avoidance velocity instead of the path direction. The avoidance velocity changes every
	 * frame, which makes units wobble left and right; off keeps them facing where their path goes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Crowd")
	bool bFaceVelocity = false;

	/** Turn the pawn at TurnRate towards the controller's rotation instead of snapping to it (Characters only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Turning")
	bool bSmoothTurning = true;

	/** Degrees per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Turning", meta = (ClampMin = "1", EditCondition = "bSmoothTurning"))
	float TurnRate = 540.f;

	/** While standing, step aside when a friendly moving unit is stuck pushing into this one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Yield")
	bool bYieldToBlockedUnits = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Yield", meta = (ClampMin = "0.05", EditCondition = "bYieldToBlockedUnits"))
	float YieldCheckInterval = 0.2f;

	/** Minimum seconds between two steps aside. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Yield", meta = (ClampMin = "0", EditCondition = "bYieldToBlockedUnits"))
	float YieldCooldown = 1.f;

private:
	void ApplySmoothTurning();
	void UpdateYield();
	bool TryStepAside(const FVector& Goal, float Radius);

	FTimerHandle YieldTimer;
	double LastYieldTime = -1000.0;
};
