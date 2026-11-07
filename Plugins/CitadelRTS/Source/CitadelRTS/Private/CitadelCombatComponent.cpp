#include "CitadelCombatComponent.h"
#include "AIController.h"
#include "CitadelFormationLibrary.h"
#include "CitadelUnitAIController.h"
#include "CitadelUnitSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"

UCitadelCombatComponent::UCitadelCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UCitadelCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	FootprintRadius = UCitadelFormationLibrary::GetFootprintRadius(GetOwner());
	// Spread target searches of units spawned on the same frame over the interval.
	AcquireCooldown = FMath::FRandRange(0.f, AcquireInterval);
	SetComponentTickInterval(ThinkInterval);
	SetComponentTickEnabled(bCanAttack);

	if (UCitadelUnitSubsystem* Units = GetWorld()->GetSubsystem<UCitadelUnitSubsystem>())
	{
		Units->Register(this);
	}
	if (bCanAttack)
	{
		GetOwner()->OnTakeAnyDamage.AddDynamic(this, &UCitadelCombatComponent::HandleOwnerDamaged);
	}
}

void UCitadelCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UCitadelUnitSubsystem* Units = World->GetSubsystem<UCitadelUnitSubsystem>())
		{
			Units->Unregister(this);
		}
	}
	if (AActor* OwningActor = GetOwner())
	{
		OwningActor->OnTakeAnyDamage.RemoveDynamic(this, &UCitadelCombatComponent::HandleOwnerDamaged);
	}
	Super::EndPlay(EndPlayReason);
}

void UCitadelCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsAlive())
	{
		return;
	}

	AttackCooldown = FMath::Max(AttackCooldown - DeltaTime, 0.f);
	ChaseRetryCooldown = FMath::Max(ChaseRetryCooldown - DeltaTime, 0.f);
	AcquireCooldown -= DeltaTime;
	if (AcquireCooldown <= 0.f)
	{
		AcquireCooldown = AcquireInterval;
		UpdateAutoTargeting();
	}

	if (bHasTarget && !IsValidTarget(Target.Get()))
	{
		ClearTarget();
	}
	AActor* Victim = Target.Get();
	if (!Victim)
	{
		return;
	}

	const double Distance = GetDistanceToActor(Victim);
	if (TargetSource == ECitadelTargetSource::Auto && SightRadius > 0.f && Distance > SightRadius * AutoTargetLeash)
	{
		ClearTarget();
		return;
	}

	const double Reach = State == ECitadelCombatState::Attacking ? AttackRange + RangeTolerance : AttackRange;
	if (Distance <= Reach)
	{
		EngageTarget(Victim);
	}
	else
	{
		ChaseTarget(Victim, Distance);
	}
}

void UCitadelCombatComponent::AttackTarget(AActor* NewTarget, bool bLockTarget)
{
	if (!bCanAttack || !IsAlive() || !IsValidTarget(NewTarget))
	{
		return;
	}
	if (IgnoredActor.Get() == NewTarget)
	{
		IgnoredActor.Reset();
	}
	bAutoTargetingSuppressed = false;
	SetTarget(NewTarget, bLockTarget ? ECitadelTargetSource::Locked : ECitadelTargetSource::Assigned);
}

void UCitadelCombatComponent::StopAttacking()
{
	ClearTarget();
}

void UCitadelCombatComponent::SetAutoTargetingSuppressed(bool bSuppressed)
{
	bAutoTargetingSuppressed = bSuppressed;
	if (bSuppressed && bHasTarget && TargetSource == ECitadelTargetSource::Auto)
	{
		ClearTarget();
	}
}

void UCitadelCombatComponent::NotifyDied()
{
	if (!IsAlive())
	{
		return;
	}
	ClearTarget();
	SetState(ECitadelCombatState::Dead);
	SetComponentTickEnabled(false);
	if (UCitadelUnitSubsystem* Units = GetWorld()->GetSubsystem<UCitadelUnitSubsystem>())
	{
		Units->Unregister(this);
	}
}

float UCitadelCombatComponent::GetDistanceToActor(const AActor* Other) const
{
	const AActor* Self = GetOwner();
	if (!Self || !Other)
	{
		return TNumericLimits<float>::Max();
	}

	const FVector From = Self->GetActorLocation();
	FVector PointOnTarget;
	const double ToSurface = GetClosestPointOnActor(Other, PointOnTarget)
		? FVector::Dist2D(From, PointOnTarget)
		: FVector::Dist2D(From, Other->GetActorLocation()) - UCitadelFormationLibrary::GetFootprintRadius(Other);
	return static_cast<float>(FMath::Max(ToSurface - FootprintRadius, 0.0));
}

bool UCitadelCombatComponent::IsIgnoring(const AActor* Candidate) const
{
	const UWorld* World = GetWorld();
	return Candidate && World && IgnoredActor.Get() == Candidate && World->GetTimeSeconds() < IgnoreUntil;
}

void UCitadelCombatComponent::HandleOwnerDamaged(AActor* DamagedActor, float DamageAmount, const UDamageType* DamageTypeUsed, AController* InstigatedBy, AActor* DamageCauser)
{
	if (!bCanAttack || !IsAlive() || bAutoTargetingSuppressed || (bHasTarget && TargetSource == ECitadelTargetSource::Locked))
	{
		return;
	}

	AActor* Attacker = AsHostileCombatant(DamageCauser);
	if (!Attacker && InstigatedBy)
	{
		Attacker = AsHostileCombatant(InstigatedBy->GetPawn());
	}
	AActor* Victim = Target.Get();
	if (!Attacker || Attacker == Victim)
	{
		return;
	}
	// Keep hitting what is already in reach unless the attacker matters more.
	if (State == ECitadelCombatState::Attacking && Victim && GetTargetPriority(Victim) >= GetTargetPriority(Attacker))
	{
		return;
	}
	SetTarget(Attacker, ECitadelTargetSource::Auto);
}

void UCitadelCombatComponent::UpdateAutoTargeting()
{
	if (!bCanAttack || bAutoTargetingSuppressed || SightRadius <= 0.f || (bHasTarget && TargetSource == ECitadelTargetSource::Locked))
	{
		return;
	}

	UCitadelUnitSubsystem* Units = GetWorld()->GetSubsystem<UCitadelUnitSubsystem>();
	// Without an AI controller the owner cannot move (e.g. a tower): only look as far as it can hit.
	const float SearchRadius = GetAIController() ? SightRadius : FMath::Min(SightRadius, AttackRange);
	AActor* Found = Units ? Units->FindNearestHostile(this, SearchRadius) : nullptr;
	AActor* Victim = Target.Get();
	if (Found && Found != Victim && (!Victim || GetTargetPriority(Found) > GetTargetPriority(Victim)))
	{
		SetTarget(Found, ECitadelTargetSource::Auto);
	}
}

void UCitadelCombatComponent::EngageTarget(AActor* Victim)
{
	if (State != ECitadelCombatState::Attacking)
	{
		if (AAIController* AI = GetAIController())
		{
			if (IsChaseMoveActive(AI))
			{
				AI->StopMovement();
			}
			AI->SetFocus(Victim, EAIFocusPriority::Gameplay);
		}
		SetState(ECitadelCombatState::Attacking);
	}
	bChaseIssued = false;
	ChaseAttemptsWithoutProgress = 0;
	BestChaseDistance = TNumericLimits<double>::Max();

	if (AttackCooldown > 0.f)
	{
		return;
	}
	AttackCooldown = AttackInterval;

	OnAttack.Broadcast(Victim);
	if (bApplyDamageOnAttack && Damage > 0.f && IsValid(Victim))
	{
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		UGameplayStatics::ApplyDamage(Victim, Damage, OwnerPawn ? OwnerPawn->GetController() : nullptr, GetOwner(), DamageTypeClass);
	}
}

void UCitadelCombatComponent::ChaseTarget(AActor* Victim, double Distance)
{
	AAIController* AI = GetAIController();
	if (!AI)
	{
		// Static attackers only fight what is in reach.
		ClearTarget();
		return;
	}
	if (State == ECitadelCombatState::Attacking)
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
	}
	SetState(ECitadelCombatState::Chasing);

	if (IsChaseMoveActive(AI) || ChaseRetryCooldown > 0.f)
	{
		return;
	}

	// The previous chase move ended (reached, blocked or failed) with the target still out of range.
	if (bChaseIssued)
	{
		const double ProgressStep = FMath::Max(AttackRange * 0.25, 25.0);
		if (Distance < BestChaseDistance - ProgressStep)
		{
			ChaseAttemptsWithoutProgress = 0;
		}
		else if (++ChaseAttemptsWithoutProgress > MaxChaseAttemptsWithoutProgress)
		{
			GiveUpOnTarget(Victim);
			return;
		}
	}
	BestChaseDistance = FMath::Min(BestChaseDistance, Distance);

	// Stopping halfway into the range leaves room for the target to shift without restarting the chase.
	const float Acceptance = AttackRange * 0.5f;
	if (Cast<APawn>(Victim))
	{
		// Following the actor keeps the path updated while the target moves.
		AI->MoveToActor(Victim, Acceptance, true, true, false);
	}
	else
	{
		// Buildings: walk to the closest point of their collision, not to their centre inside the mesh.
		FVector Goal;
		if (!GetClosestPointOnActor(Victim, Goal))
		{
			Goal = Victim->GetActorLocation();
		}
		// Exactly that point: spreading it to a free spot could move it out of reach.
		ACitadelUnitAIController* CitadelAI = Cast<ACitadelUnitAIController>(AI);
		const bool bWasSpreading = CitadelAI && CitadelAI->bSpreadMoves;
		if (CitadelAI)
		{
			CitadelAI->bSpreadMoves = false;
		}
		AI->MoveToLocation(Goal, Acceptance, true, true, true, false);
		if (CitadelAI)
		{
			CitadelAI->bSpreadMoves = bWasSpreading;
		}
	}
	ChaseRequestId = AI->GetCurrentMoveRequestID();
	bChaseIssued = true;
	ChaseRetryCooldown = 0.25f;
}

void UCitadelCombatComponent::GiveUpOnTarget(AActor* Victim)
{
	IgnoredActor = Victim;
	IgnoreUntil = GetWorld()->GetTimeSeconds() + UnreachableIgnoreTime;
	ClearTarget();
}

void UCitadelCombatComponent::SetTarget(AActor* NewTarget, ECitadelTargetSource Source)
{
	if (bHasTarget && NewTarget == Target.Get())
	{
		TargetSource = Source;
		return;
	}

	AAIController* AI = GetAIController();
	if (AI && IsChaseMoveActive(AI))
	{
		AI->StopMovement();
	}

	Target = NewTarget;
	bHasTarget = true;
	TargetSource = Source;
	bChaseIssued = false;
	ChaseAttemptsWithoutProgress = 0;
	BestChaseDistance = TNumericLimits<double>::Max();
	ChaseRetryCooldown = 0.f;

	if (AI)
	{
		if (State == ECitadelCombatState::Attacking)
		{
			AI->SetFocus(NewTarget, EAIFocusPriority::Gameplay);
		}
		else
		{
			AI->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
	OnTargetChanged.Broadcast(NewTarget);
}

void UCitadelCombatComponent::ClearTarget()
{
	if (AAIController* AI = GetAIController())
	{
		if (IsChaseMoveActive(AI))
		{
			AI->StopMovement();
		}
		if (State == ECitadelCombatState::Attacking)
		{
			AI->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	const bool bHadTarget = bHasTarget;
	Target.Reset();
	bHasTarget = false;
	TargetSource = ECitadelTargetSource::Auto;
	bChaseIssued = false;
	ChaseAttemptsWithoutProgress = 0;
	BestChaseDistance = TNumericLimits<double>::Max();

	if (IsAlive())
	{
		SetState(ECitadelCombatState::Idle);
	}
	if (bHadTarget)
	{
		OnTargetChanged.Broadcast(nullptr);
	}
}

void UCitadelCombatComponent::SetState(ECitadelCombatState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(NewState);
	}
}

bool UCitadelCombatComponent::IsValidTarget(const AActor* Candidate) const
{
	if (!IsValid(Candidate) || Candidate == GetOwner())
	{
		return false;
	}
	const UCitadelCombatComponent* Other = Candidate->FindComponentByClass<UCitadelCombatComponent>();
	return !Other || Other->IsAlive();
}

AActor* UCitadelCombatComponent::AsHostileCombatant(AActor* Candidate) const
{
	const UCitadelCombatComponent* Other = Candidate ? Candidate->FindComponentByClass<UCitadelCombatComponent>() : nullptr;
	return Other && Other->IsAlive() && IsHostileTo(Other) ? Candidate : nullptr;
}

bool UCitadelCombatComponent::GetClosestPointOnActor(const AActor* Other, FVector& OutPoint) const
{
	const AActor* Self = GetOwner();
	if (!Self || !Other)
	{
		return false;
	}

	// Only collision that stops units counts: capsules and building meshes, not sight or trigger spheres.
	const FVector From = Self->GetActorLocation();
	double BestDistSq = TNumericLimits<double>::Max();
	bool bFound = false;
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Other);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Primitive->IsCollisionEnabled() || Primitive->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
		{
			continue;
		}
		FVector Point;
		if (Primitive->GetClosestPointOnCollision(From, Point) < 0.f)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared2D(From, Point);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutPoint = Point;
			bFound = true;
		}
	}
	return bFound;
}

bool UCitadelCombatComponent::IsChaseMoveActive(const AAIController* AI) const
{
	return bChaseIssued && AI->GetMoveStatus() != EPathFollowingStatus::Idle && AI->GetCurrentMoveRequestID() == ChaseRequestId;
}

AAIController* UCitadelCombatComponent::GetAIController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
}

int32 UCitadelCombatComponent::GetTargetPriority(const AActor* Candidate)
{
	const UCitadelCombatComponent* Other = Candidate ? Candidate->FindComponentByClass<UCitadelCombatComponent>() : nullptr;
	return Other ? Other->TargetPriority : 0;
}
