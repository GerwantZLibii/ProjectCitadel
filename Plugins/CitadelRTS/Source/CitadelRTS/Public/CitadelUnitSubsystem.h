#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CitadelUnitSubsystem.generated.h"

class APawn;
class UCitadelCombatComponent;

/** Location moves to one goal issued in one frame, e.g. an order for the whole selection. */
struct FCitadelGroupMove
{
	FVector Goal = FVector::ZeroVector;
	uint64 Frame = 0;
	float Spacing = 0.f;
	TArray<FVector> Slots;
	TArray<const AActor*> Units;
};

/**
 * Registry of everything that can fight or be fought. Target acquisition queries it on an interval
 * instead of every unit carrying a large overlap sphere.
 */
UCLASS()
class CITADELRTS_API UCitadelUnitSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(UCitadelCombatComponent* Combatant);
	void Unregister(UCitadelCombatComponent* Combatant);

	/**
	 * Best living enemy of Requester whose edge is within Radius of Requester's centre: the highest
	 * TargetPriority wins, then the nearest. Targets Requester has given up on are skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Units")
	AActor* FindNearestHostile(const UCitadelCombatComponent* Requester, float Radius) const;

	/**
	 * Free spot for Unit near Goal. Units sent to the same goal in the same frame fill rings around it from
	 * the inside out, each taking the free spot of the current ring nearest to itself; spots off the navmesh
	 * or taken by other units are skipped. Returns Goal when there is no room.
	 */
	FVector ClaimMoveSlot(const APawn* Unit, const FVector& Goal);

	/** Owners of all living registered combatants of a team; -1 returns every team. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Units")
	TArray<AActor*> GetActorsOfTeam(int32 Team) const;

private:
	TArray<TWeakObjectPtr<UCitadelCombatComponent>> Combatants;

	/** Only the current frame's groups; the raw pointers are never used after that frame. */
	TArray<FCitadelGroupMove> GroupMoves;
};
