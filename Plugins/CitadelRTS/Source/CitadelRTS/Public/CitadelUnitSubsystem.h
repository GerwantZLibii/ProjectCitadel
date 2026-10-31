#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CitadelUnitSubsystem.generated.h"

class UCitadelCombatComponent;

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

	/** Owners of all living registered combatants of a team; -1 returns every team. */
	UFUNCTION(BlueprintCallable, Category = "Citadel|Units")
	TArray<AActor*> GetActorsOfTeam(int32 Team) const;

private:
	TArray<TWeakObjectPtr<UCitadelCombatComponent>> Combatants;
};
