#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "CitadelCombatComponent.generated.h"

class AAIController;
class AController;
class UDamageType;

UENUM(BlueprintType)
enum class ECitadelCombatState : uint8
{
	Idle,
	Chasing,
	Attacking,
	Dead,
};

/** Where the current target came from; decides what may replace it. */
enum class ECitadelTargetSource : uint8
{
	Auto,
	Assigned,
	Locked,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCitadelCombatStateChanged, ECitadelCombatState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCitadelCombatTargetChanged, AActor*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCitadelCombatAttack, AActor*, Victim);

/**
 * Target selection, chasing and attacking for one unit, as a single state machine.
 *
 * - Range is measured between collision edges, so it means the same for a worker and a giant.
 * - Each chase move is checked: when one ends without reaching the target and without getting closer,
 *   the unit gives up on that target for a while instead of freezing.
 * - Player orders (locked targets) win over automatic targeting and retaliation.
 *
 * Also put it (with bCanAttack off) on anything that should be found as a target, e.g. buildings.
 */
UCLASS(ClassGroup = (Citadel), meta = (BlueprintSpawnableComponent))
class CITADELRTS_API UCitadelCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCitadelCombatComponent();

	/** Combatants with a different team are enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat")
	int32 TeamId = 0;

	/** Off for things that can only be attacked (HQ, houses). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Citadel|Combat")
	bool bCanAttack = true;

	/** Preferred when targets are picked automatically; e.g. units 1, buildings 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat")
	int32 TargetPriority = 1;

	/** Distance between this unit's and the target's collision edges at which attacks land. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	float AttackRange = 100.f;

	/** Once attacking, the target may get this much further away before the unit starts chasing again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	float RangeTolerance = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0.05", EditCondition = "bCanAttack"))
	float AttackInterval = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	float Damage = 10.f;

	/** Off: attacks only broadcast OnAttack, e.g. so a Blueprint can spawn a projectile that deals Damage on hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (EditCondition = "bCanAttack"))
	bool bApplyDamageOnAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (EditCondition = "bCanAttack"))
	TSubclassOf<UDamageType> DamageTypeClass;

	/** Enemies whose edge comes this close are picked up automatically; 0 disables automatic targeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	float SightRadius = 1500.f;

	/** Automatically picked targets are dropped beyond SightRadius times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "1", EditCondition = "bCanAttack"))
	float AutoTargetLeash = 1.5f;

	/** Chase moves that end without reaching the target and without getting closer before giving up on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	int32 MaxChaseAttemptsWithoutProgress = 3;

	/** Seconds a target that could not be reached is ignored by automatic targeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	float UnreachableIgnoreTime = 3.f;

	/** How often automatic targeting runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citadel|Combat", meta = (ClampMin = "0.05", EditCondition = "bCanAttack"))
	float AcquireInterval = 0.3f;

	/** How often the state machine updates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Citadel|Combat", meta = (ClampMin = "0", EditCondition = "bCanAttack"))
	float ThinkInterval = 0.1f;

	/**
	 * Attack an actor. Locked targets (player orders) are kept until they die or another order comes;
	 * unlocked ones (e.g. an AI's "go for the HQ") give way to higher-priority enemies met on the way.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Combat")
	void AttackTarget(AActor* NewTarget, bool bLockTarget = true);

	/** Drops the target and stops the chase move this component started. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Combat")
	void StopAttacking();

	/** While suppressed (e.g. during a player move order) the unit neither picks targets nor retaliates. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Combat")
	void SetAutoTargetingSuppressed(bool bSuppressed);

	/** Call when the owner dies: stops everything and removes it from target searches. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Combat")
	void NotifyDied();

	UFUNCTION(BlueprintPure, Category = "Citadel|Combat")
	AActor* GetTarget() const { return Target.Get(); }

	UFUNCTION(BlueprintPure, Category = "Citadel|Combat")
	ECitadelCombatState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Citadel|Combat")
	bool IsAlive() const { return State != ECitadelCombatState::Dead; }

	UFUNCTION(BlueprintPure, Category = "Citadel|Combat")
	bool IsHostileTo(const UCitadelCombatComponent* Other) const { return Other && Other->TeamId != TeamId; }

	UFUNCTION(BlueprintPure, Category = "Citadel|Combat")
	float GetFootprintRadius() const { return FootprintRadius; }

	/** Distance from this unit's collision edge to the closest point of Other's blocking collision. */
	UFUNCTION(BlueprintPure, Category = "Citadel|Combat")
	float GetDistanceToActor(const AActor* Other) const;

	/** True while automatic targeting skips Candidate after failing to reach it. */
	bool IsIgnoring(const AActor* Candidate) const;

	UPROPERTY(BlueprintAssignable, Category = "Citadel|Combat")
	FCitadelCombatStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Citadel|Combat")
	FCitadelCombatTargetChanged OnTargetChanged;

	/** Fired on every attack, before damage is applied. */
	UPROPERTY(BlueprintAssignable, Category = "Citadel|Combat")
	FCitadelCombatAttack OnAttack;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION()
	void HandleOwnerDamaged(AActor* DamagedActor, float DamageAmount, const UDamageType* DamageTypeUsed, AController* InstigatedBy, AActor* DamageCauser);

	void UpdateAutoTargeting();
	void EngageTarget(AActor* Victim);
	void ChaseTarget(AActor* Victim, double Distance);
	void GiveUpOnTarget(AActor* Victim);
	void SetTarget(AActor* NewTarget, ECitadelTargetSource Source);
	void ClearTarget();
	void SetState(ECitadelCombatState NewState);

	bool IsValidTarget(const AActor* Candidate) const;
	AActor* AsHostileCombatant(AActor* Candidate) const;
	bool GetClosestPointOnActor(const AActor* Other, FVector& OutPoint) const;
	bool IsChaseMoveActive(const AAIController* AI) const;
	AAIController* GetAIController() const;
	static int32 GetTargetPriority(const AActor* Candidate);

	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<AActor> IgnoredActor;
	ECitadelTargetSource TargetSource = ECitadelTargetSource::Auto;
	ECitadelCombatState State = ECitadelCombatState::Idle;
	bool bHasTarget = false;
	bool bAutoTargetingSuppressed = false;

	FAIRequestID ChaseRequestId;
	bool bChaseIssued = false;
	int32 ChaseAttemptsWithoutProgress = 0;
	double BestChaseDistance = TNumericLimits<double>::Max();

	float FootprintRadius = 0.f;
	float AttackCooldown = 0.f;
	float AcquireCooldown = 0.f;
	float ChaseRetryCooldown = 0.f;
	double IgnoreUntil = 0.0;
};
