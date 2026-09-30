// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrabCarryComponent.h"
#include "CrabColonyMath.h"
#include "Math/RandomStream.h"
#include "CrabColony.generated.h"

class ACrabBeach;
class ACrabColonyNpc;
class ACrabGull;
class ACrabPawn;
class UCrabColonyViewComponent;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * The colony under the dune-foot burrow: the plan and its dig state, the food store, the non-playable crabs and
 * their jobs, the cutaway that shows it all, and the pellet mound at the mouth. Spawned by the game mode next to
 * the beach. It runs on its own whether or not the player is down there; what it draws depends on where the
 * player is (SetPlayerUnderground). Rules are in CrabColonyMath.h; this actor only keeps time and state, moves
 * the colony crabs and drives the view. See GAME.md, "Colony".
 */
UCLASS()
class CRABSIM_API ACrabColony : public AActor
{
	GENERATED_BODY()

public:
	ACrabColony();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- What it is ------------------------------------------------------------------------------------

	const CrabColony::FBlueprint& GetPlan() const { return Plan; }
	const CrabColony::FColonyState& GetState() const { return State; }
	const CrabColony::FTuning& GetTuning() const { return Tuning; }
	UCrabColonyViewComponent* GetView() const { return View; }

	/** The beach burrow the colony lives under: its mouth. Always burrow 0. */
	int32 GetEntranceBurrow() const { return 0; }
	/** The mouth on the beach, on the ground. The cutaway's (0, 0). */
	FVector GetEntranceWorld() const;

	/** A plan point (U, V) as a world point in the cutaway, and back. */
	FVector PlanToWorld(FVector2D UV) const;
	FVector2D WorldToPlan(const FVector& World) const;

	/** The edge being dug now and where its face is, for the DIG button and the player's walk to it. INDEX_NONE when the plan is done. */
	int32 GetActiveDigEdge() const;
	FVector2D GetDigFacePos() const;

	/** Where the pantry with food is (the nearest open pantry to the point), for EAT. INDEX_NONE if none is open. */
	int32 FindPantryNear(FVector2D UV) const;

	int32 GetPopulation() const { return State.Population; }
	int32 CountJob(CrabColony::EJob Job) const;
	int32 CountOnSurface() const;

	// --- The player down here ------------------------------------------------------------------------------

	/**
	 * The player went down (true) or came up (false). Down: the beach, the sea and the gull are hidden, the cutaway
	 * and the colony crabs in it are shown, and colony crabs out on the beach are hidden. Up: the reverse.
	 */
	void SetPlayerUnderground(bool bUnderground);
	bool IsPlayerUnderground() const { return bPlayerUnderground; }

	/** The player digs at the active face for this long at the player's rate. Returns uu dug (0 when nothing is left to dig). */
	float PlayerDig(float DeltaSeconds);

	/** The player eats from the store for this long, taking at most Room. Returns what was eaten. */
	float PlayerEat(float DeltaSeconds, float Room);

	/** A pellet dropped on the mound at the mouth. Who is "player" or "npc", for the log. */
	void AddMoundPellet(const TCHAR* Who);

	/** A pellet set down on a tunnel floor, and picked up again. */
	int32 AddLoosePellet(FVector2D UV);
	void RemoveLoosePellet(int32 Handle);
	/** The loose pellet nearest a point, within Radius uu, or INDEX_NONE. */
	int32 FindLoosePelletNear(FVector2D UV, float Radius) const;

	/** The player danced at this plan point: colony crabs close by wave back for a while. */
	void PlayerDanced(FVector2D UV);

	/** A new round: the plan back to its pre-dug state, the store and population back to the start, the mound gone. */
	void ResetForNewRound();

	/** The beach-side pellets: a ring round a freshly dug burrow, and a feeding pellet behind a sifting crab. Drawn only. */
	void ScatterBurrowPellets(const FVector& Hole);
	void DropFeedingPellet(const FVector& Where);

	/** Test only: sets the food store directly, so a test can force a hatch without waiting out real foraging. */
	void Test_SetStore(float NewStore) { State.Store = NewStore; }

private:
	/** A colony NPC's whole state: its body, its job and how far it has got, and where it is. */
	struct FCrabState
	{
		ACrabColonyNpc* Body = nullptr;
		CrabColony::EJob Job = CrabColony::EJob::Rest;
		CrabColony::EPlace Place = CrabColony::EPlace::Underground;
		/** How far the current job has got. Meaning is private to whichever TickXxx/BeginXxx pair owns the job. */
		int32 Stage = 0;

		/** Underground position, valid while Place is Underground. */
		FVector2D PlanPos = FVector2D::ZeroVector;
		TArray<FVector2D> Waypoints;
		int32 WaypointIndex = 0;

		/** Surface position and the straight-line target it is walking to, valid while Place is Surface. */
		FVector2D SurfaceXY = FVector2D::ZeroVector;
		FVector2D SurfaceTarget = FVector2D::ZeroVector;
		/** World yaw while on the surface: kept from tick to tick so CrabMovementMath::SideFacingYaw has continuity. */
		float FacingYaw = 0.f;

		/** Countdown or count-up used by whichever job is running: a rest, a sift. */
		float Timer = 0.f;

		ECarry Carrying = ECarry::None;
		/** Richness gathered so far this forage trip, carried home and added to Store on arrival. */
		float CarriedAmount = 0.f;
		int32 ForagePatch = INDEX_NONE;

		float SizeScale = 1.f;
		/** Seconds since hatching, growth eases from JuvenileScale to 1 over Tuning.GrowSeconds. Already 1 for a starting crab. */
		float GrowTimer = 0.f;
		/** This crab's own seed: RestSeconds, and which slot of a chamber it settles in. */
		uint32 Seed = 0;

		bool bDancing = false;
		float DanceTimer = 0.f;
	};

	/** A pellet the player set down in a tunnel: View's own handle, and where it is, for FindLoosePelletNear. */
	struct FLoosePelletRecord
	{
		int32 Handle = INDEX_NONE;
		FVector2D UV = FVector2D::ZeroVector;
	};

	CrabColony::FBlueprint Plan;
	CrabColony::FColonyState State;
	CrabColony::FTuning Tuning;

	UPROPERTY(VisibleAnywhere, Category = "Colony")
	TObjectPtr<UCrabColonyViewComponent> View;

	/** Not a UPROPERTY, matching UCrabColonyViewComponent's own FEdgeVisual/EdgeVisuals: the bodies are actors, kept
	 * alive by the world regardless, and this struct is plain C++, not a USTRUCT. */
	TArray<FCrabState> Crabs;
	TArray<FLoosePelletRecord> LoosePelletRecords;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BeachRingPellets;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FeedingPellets;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BeachMoundBalls;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeachPelletMaterial;

	FRandomStream BeachPelletRandom = FRandomStream(20261);

	bool bPlayerUnderground = false;
	int32 LastActiveEdge = INDEX_NONE;
	uint32 CrabSeedCounter = 0;
	float StateLogTimer = 0.f;
	float BeachPelletWashTimer = 0.f;

	ACrabBeach* GetBeach() const;
	ACrabGull* GetGull() const;

	// --- Spawning and population -----------------------------------------------------------------------

	void SpawnCrab(uint32 Seed);
	void SpawnJuvenile();
	uint32 NextCrabSeed();

	// --- Per-crab simulation, one state machine per job -------------------------------------------------

	void TickOneCrab(FCrabState& Crab, float DeltaSeconds);
	void UpdateGrowth(FCrabState& Crab, float DeltaSeconds);
	void BeginNextJob(FCrabState& Crab);

	void BeginRest(FCrabState& Crab);
	void TickRest(FCrabState& Crab, float DeltaSeconds);
	void BeginDig(FCrabState& Crab);
	void TickDig(FCrabState& Crab, float DeltaSeconds);
	void BeginHaul(FCrabState& Crab);
	void TickHaul(FCrabState& Crab, float DeltaSeconds);
	void BeginForage(FCrabState& Crab);
	void TickForage(FCrabState& Crab, float DeltaSeconds);
	void BeginFlee(FCrabState& Crab);
	void TickFlee(FCrabState& Crab, float DeltaSeconds);
	void BeginReturnHome(FCrabState& Crab);
	void TickReturnHome(FCrabState& Crab, float DeltaSeconds);
	void DepositFood(FCrabState& Crab);

	// --- Movement primitives ------------------------------------------------------------------------------

	void BeginUndergroundWalk(FCrabState& Crab, const FVector2D& ToUV) const;
	void AdvanceUnderground(FCrabState& Crab, float Distance) const;
	bool HasArrived(const FCrabState& Crab) const;
	void BeginSurfaceWalk(FCrabState& Crab, const FVector2D& Target) const;
	void AdvanceSurface(FCrabState& Crab, float Distance) const;
	void ApplySurfacePose(FCrabState& Crab) const;
	bool HasArrivedSurface(const FCrabState& Crab) const;
	/** Constant for every underground pose: see the definition for why a yaw alone can only ever face the glass. */
	static float UndergroundFacingYaw();

	// --- Plan queries used to pick where a job goes -------------------------------------------------------

	int32 GetEntranceNode() const;
	FVector2D GetEntranceNodePos() const;
	int32 GetUpperJunctionNode() const;
	int32 FindNurseryNode() const;
	int32 FindNearestOpenNode(const FVector2D& From, TFunctionRef<bool(const CrabColony::FNode&)> Match) const;
	FVector2D FindRestSpotUV(const FCrabState& Crab) const;
	/** A point inside the chamber's disc, picked from Seed so crabs resting in it are not all on the same spot. Node's own position if it holds no chamber. */
	FVector2D ChamberSpot(int32 NodeIndex, uint32 Seed) const;
	bool FindBestForagePatch(int32& OutPatch) const;
	bool IsGullDown() const;
	CrabColony::FJobContext BuildJobContext() const;
	TArray<float> ComputeDugFractions() const;

	// --- Visibility ------------------------------------------------------------------------------------

	void ApplyVisibility(FCrabState& Crab) const;
	void ApplyBeachPelletVisibility() const;

	// --- Beach-side pellets, drawn only ------------------------------------------------------------------

	void AddBeachMoundBall();
	void TickBeachPelletWashAway(float DeltaSeconds);

	// --- Log -------------------------------------------------------------------------------------------

	void LogEvent(const TCHAR* Name, const FString& Detail = FString()) const;
	void LogColonyState() const;

	// --- Debug -----------------------------------------------------------------------------------------

	void ApplyColonyPeek();
};
