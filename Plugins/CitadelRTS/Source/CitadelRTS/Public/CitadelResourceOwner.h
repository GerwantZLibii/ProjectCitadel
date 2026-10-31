#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CitadelResourceOwner.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UCitadelResourceOwner : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by whatever stores a side's resources (in ProjectCitadel: PC_ProjectCitadel). */
class CITADELRTS_API ICitadelResourceOwner
{
	GENERATED_BODY()

public:
	/** Pays the whole cost or nothing. Returns false when it cannot be afforded. */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Citadel|Resources")
	bool TrySpendResources(const TMap<FName, int32>& Cost);

	/** Gives resources back, e.g. when a queued unit is cancelled. */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Citadel|Resources")
	void RefundResources(const TMap<FName, int32>& Amount);
};
