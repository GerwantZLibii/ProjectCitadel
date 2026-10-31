#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CitadelProductionComponent.generated.h"

class APawn;
class UCitadelUnitDefinition;

USTRUCT(BlueprintType)
struct FCitadelProductionOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Citadel|Production")
	TObjectPtr<UCitadelUnitDefinition> Unit = nullptr;

	/** Seconds of build time already done. */
	UPROPERTY(BlueprintReadOnly, Category = "Citadel|Production")
	float Progress = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCitadelProductionQueueChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCitadelUnitProduced, APawn*, Unit, UCitadelUnitDefinition*, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCitadelProductionBlocked, UCitadelUnitDefinition*, Definition, bool, bBlocked);

/**
 * Production queue for a building. Units are paid for when queued, built one at a time, and spawned
 * only on free, navigable ground found around the building's exit points. When there is no room the
 * finished unit waits in the queue (OnProductionBlocked) instead of spawning inside another unit.
 */
UCLASS(ClassGroup = (Citadel), meta = (BlueprintSpawnableComponent))
class CITADELRTS_API UCitadelProductionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCitadelProductionComponent();

	/** Units this building can make; drives the UI. Empty = anything may be queued. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Production")
	TArray<TObjectPtr<UCitadelUnitDefinition>> ProducibleUnits;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Production", meta = (ClampMin = "1"))
	int32 MaxQueueSize = 5;

	/** Scene components with this tag mark where units leave the building; the search for free ground starts there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Production")
	FName ExitPointTag = TEXT("SpawnPoint");

	/** Rings of candidate spots searched around each exit point, spaced by the unit's diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Production", meta = (ClampMin = "0"))
	int32 SearchRings = 4;

	/** How often a finished unit retries spawning while there is no room. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Production", meta = (ClampMin = "0.05"))
	float SpawnRetryInterval = 0.5f;

	/** Sends new units to the rally point with a plain AI move. Leave off to move them yourself from OnUnitProduced. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Production")
	bool bMoveToRallyPoint = false;

	/** Who pays (must implement CitadelResourceOwner). Null = the first player controller. */
	UPROPERTY(BlueprintReadWrite, Category = "Citadel|Production")
	TObjectPtr<UObject> ResourceOwner;

	/** Pays for and queues a unit. False if the queue is full, the unit is not producible here or cannot be afforded. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Production")
	bool Enqueue(UCitadelUnitDefinition* Unit);

	/** Removes an order and refunds its cost. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Production")
	bool CancelOrder(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Citadel|Production")
	void SetRallyPoint(FVector Location);

	UFUNCTION(BlueprintCallable, Category = "Citadel|Production")
	void ClearRallyPoint();

	UFUNCTION(BlueprintPure, Category = "Citadel|Production")
	bool GetRallyPoint(FVector& Location) const;

	UFUNCTION(BlueprintPure, Category = "Citadel|Production")
	TArray<FCitadelProductionOrder> GetQueue() const { return Queue; }

	/** Build progress of the front order, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Citadel|Production")
	float GetCurrentProgress() const;

	/** True while a finished unit is waiting for free ground. */
	UFUNCTION(BlueprintPure, Category = "Citadel|Production")
	bool IsBlocked() const { return bBlocked; }

	/** Finds free, navigable ground for the unit near the exit points. Location is the actor location to spawn at. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Production")
	bool FindSpawnLocation(const UCitadelUnitDefinition* Unit, FVector& Location) const;

	UPROPERTY(BlueprintAssignable, Category = "Citadel|Production")
	FCitadelProductionQueueChanged OnQueueChanged;

	UPROPERTY(BlueprintAssignable, Category = "Citadel|Production")
	FCitadelUnitProduced OnUnitProduced;

	UPROPERTY(BlueprintAssignable, Category = "Citadel|Production")
	FCitadelProductionBlocked OnProductionBlocked;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	APawn* SpawnUnit(UCitadelUnitDefinition* Unit) const;
	TArray<FVector> GetExitPoints() const;
	UObject* GetResourceOwner() const;
	void SetBlocked(UCitadelUnitDefinition* Unit, bool bNewBlocked);

	UPROPERTY(Transient)
	TArray<FCitadelProductionOrder> Queue;

	FVector RallyPoint = FVector::ZeroVector;
	bool bHasRallyPoint = false;
	bool bBlocked = false;
	float RetryCooldown = 0.f;
};
