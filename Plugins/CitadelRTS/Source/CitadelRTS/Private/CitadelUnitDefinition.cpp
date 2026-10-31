#include "CitadelUnitDefinition.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"

void UCitadelUnitDefinition::GetFootprint(float& Radius, float& HalfHeight) const
{
	Radius = FootprintRadiusOverride;
	HalfHeight = 0.f;

	const APawn* DefaultPawn = UnitClass.GetDefaultObject();
	if (const UCapsuleComponent* Capsule = DefaultPawn ? Cast<UCapsuleComponent>(DefaultPawn->GetRootComponent()) : nullptr)
	{
		// A class default object is never registered, so its world transform carries no scale; the root's relative one does.
		const float Scale = Capsule->GetRelativeScale3D().GetAbsMax();
		if (Radius <= 0.f)
		{
			Radius = Capsule->GetUnscaledCapsuleRadius() * Scale;
		}
		HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight() * Scale;
	}

	Radius = Radius > 0.f ? Radius : 50.f;
	HalfHeight = HalfHeight > 0.f ? HalfHeight : Radius;
}
