#include "CitadelUnitAIController.h"
#include "CitadelRTS.h"
#include "GameFramework/Pawn.h"
#include "Navigation/CrowdFollowingComponent.h"

ACitadelUnitAIController::ACitadelUnitAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void ACitadelUnitAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplyCrowdSettings();
}

void ACitadelUnitAIController::ApplyCrowdSettings()
{
	UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	const APawn* ControlledPawn = GetPawn();
	if (!Crowd || !ControlledPawn)
	{
		return;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	ControlledPawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
	if (Radius <= 0.f)
	{
		UE_LOG(LogCitadelRTS, Warning, TEXT("%s: %s has no collision radius, crowd settings left unchanged"), *GetName(), *ControlledPawn->GetName());
		return;
	}

	// Only the last call pushes the parameters to the crowd manager; the agent may also not be registered yet,
	// in which case the values are picked up on registration.
	Crowd->SetCrowdCollisionQueryRange(Radius * CollisionQueryRangeScale, false);
	Crowd->SetCrowdPathOptimizationRange(Radius * PathOptimizationRangeScale, false);
	Crowd->SetCrowdAvoidanceQuality(static_cast<ECrowdAvoidanceQuality::Type>(static_cast<int32>(AvoidanceQuality)), false);
	Crowd->SetCrowdSeparation(bEnableSeparation, false);
	Crowd->SetCrowdSeparationWeight(SeparationWeight, true);
}
