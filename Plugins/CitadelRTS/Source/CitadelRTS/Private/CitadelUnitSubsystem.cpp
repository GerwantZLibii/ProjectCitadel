#include "CitadelUnitSubsystem.h"
#include "CitadelCombatComponent.h"
#include "GameFramework/Actor.h"

void UCitadelUnitSubsystem::Register(UCitadelCombatComponent* Combatant)
{
	if (Combatant)
	{
		Combatants.AddUnique(Combatant);
	}
}

void UCitadelUnitSubsystem::Unregister(UCitadelCombatComponent* Combatant)
{
	Combatants.RemoveAllSwap([Combatant](const TWeakObjectPtr<UCitadelCombatComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Combatant;
	});
}

AActor* UCitadelUnitSubsystem::FindNearestHostile(const UCitadelCombatComponent* Requester, float Radius) const
{
	const AActor* Self = Requester ? Requester->GetOwner() : nullptr;
	if (!Self)
	{
		return nullptr;
	}

	const FVector Origin = Self->GetActorLocation();
	AActor* Best = nullptr;
	int32 BestPriority = 0;
	double BestDistance = 0.0;
	for (const TWeakObjectPtr<UCitadelCombatComponent>& Entry : Combatants)
	{
		const UCitadelCombatComponent* Other = Entry.Get();
		AActor* OtherActor = Other ? Other->GetOwner() : nullptr;
		if (!OtherActor || Other == Requester || !Other->IsAlive() || !Requester->IsHostileTo(Other) || Requester->IsIgnoring(OtherActor))
		{
			continue;
		}

		const double Distance = FVector::Dist2D(Origin, OtherActor->GetActorLocation()) - Other->GetFootprintRadius();
		if (Distance > Radius)
		{
			continue;
		}
		if (!Best || Other->TargetPriority > BestPriority || (Other->TargetPriority == BestPriority && Distance < BestDistance))
		{
			Best = OtherActor;
			BestPriority = Other->TargetPriority;
			BestDistance = Distance;
		}
	}
	return Best;
}

TArray<AActor*> UCitadelUnitSubsystem::GetActorsOfTeam(int32 Team) const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<UCitadelCombatComponent>& Entry : Combatants)
	{
		const UCitadelCombatComponent* Combatant = Entry.Get();
		if (Combatant && Combatant->IsAlive() && (Team < 0 || Combatant->TeamId == Team))
		{
			if (AActor* OwningActor = Combatant->GetOwner())
			{
				Result.Add(OwningActor);
			}
		}
	}
	return Result;
}
