// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrabGullMath.h"
#include "CrabGull.generated.h"

class ACrabBeach;
class ACrabPawn;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

/**
 * The gull: a threat that is slow to arrive, loudly announced and answered by one click. It owns the rules that
 * bring it (CrabGullMath.h), reads the crab and the beach every tick, eats a food patch when it lands on one,
 * ends the round when it catches the crab, and draws itself from engine shapes: a white body, grey wings that flap
 * while it flies, an orange bill and legs, a soft shadow on the ground and, once it is down, a ring round its feet.
 * One per world. It starts over with every new round, with a seed of its own for each.
 */
UCLASS()
class CRABSIM_API ACrabGull : public AActor
{
	GENERATED_BODY()

public:
	ACrabGull();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	const CrabGull::FState& GetState() const { return State; }
	CrabGull::EPhase GetPhase() const { return State.Phase; }
	/** A gull is about: circling, down, stalking or on its way off. */
	bool IsActive() const { return State.Phase != CrabGull::EPhase::Absent; }
	/** The HUD warns of it: circling, or down and near. */
	bool IsWarned() const { return CrabGull::IsWarned(State.Phase); }
	/** Ground XY of the gull. */
	FVector2D GetGroundLocation() const { return State.Location; }
	/** Ground distance from the crab, uu. Huge when there is no crab. */
	float GetCrabDistance() const;
	/** Gulls so far this round. */
	int32 GetCount() const { return State.Count; }

	/** How far the wing tips are lifted, degrees: it swings while the gull flies, and hangs at about -78 once it is down. */
	float GetWingTilt() const { return WingTilt; }
	/** The ring round its feet, shown once it is down. */
	bool IsRingShown() const { return bRingShown; }

	/** Begin circling on the next tick whatever the rules say (never with the results panel up). For tests. */
	void SpawnNow() { bSpawnNow = true; }

	/** Start over as for a new round: no gull, nothing owed, a fresh seed. */
	void ResetForNewRound(int32 RoundIndex);

	/**
	 * Hides the gull regardless of IsActive, and stops it from un-hiding itself: for ACrabColony::SetPlayerUnderground,
	 * since the beach, the sea and the gull are not drawn while the crab is down (GAME.md, "Colony (building)"), even
	 * though the gull goes on hunting up there unseen. False puts UpdateVisual's own IsActive check back in charge.
	 */
	void SetSuppressed(bool bNewSuppressed) { bSuppressed = bNewSuppressed; }

	/** The seed a round's gulls come from: this and the round's number. */
	UPROPERTY(EditAnywhere, Category = "Gull")
	int32 Seed = 11;

private:
	void BuildVisual();
	void ApplyColors();
	ACrabPawn* GetCrab() const;
	ACrabBeach* GetBeach() const;
	void HandleEvents(const CrabGull::FStep& Out, const CrabGull::FCrabView& View, ACrabPawn* Crab, ACrabBeach* Beach);
	void UpdateVisual(float DeltaSeconds, const ACrabBeach* Beach);
	void LogEvent(const TCHAR* Name, const FString& Detail = FString()) const;
	void LogGull(const CrabGull::FCrabView& View) const;

	UPROPERTY(VisibleAnywhere, Category = "Gull")
	TObjectPtr<USceneComponent> Body;

	/** The wings turn on their shoulders: Left is on the crab's left as the gull faces the way it goes. */
	UPROPERTY()
	TObjectPtr<USceneComponent> WingPivot[2];

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> WhiteParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GreyParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> OrangeParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> DarkParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> LegParts;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ShadowOuter;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ShadowInner;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> RingParts;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	mutable TWeakObjectPtr<ACrabPawn> CrabCache;
	mutable TWeakObjectPtr<ACrabBeach> BeachCache;

	CrabGull::FState State;
	TArray<CrabGull::FPatchSpot> PatchSpots;
	int32 SeenRound = -1;
	bool bSpawnNow = false;
	float FlapClock = 0.f;
	float FoldBlend = 1.f;
	float LungeBlend = 0.f;
	float LogTimer = 0.f;
	float WingTilt = -78.f;
	bool bRingShown = false;
	bool bSuppressed = false;
};
