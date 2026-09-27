// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrabTide.h"
#include "CrabBeach.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UProceduralMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** A hole in the sand the crab can dig into. Location is on the ground. */
struct FCrabBurrow
{
	FVector Location = FVector::ZeroVector;
	float Radius = 60.f;
	/** Water this deep over the hole floods it and forces the crab out. */
	float FloodDepth = 50.f;
	/** Dug by the crab during the round, not part of the beach. */
	bool bDug = false;
	/** The hole and the rim drawn for it, so a dug burrow can be taken away again. */
	TArray<UStaticMeshComponent*> Parts;
};

/** A patch of algae-rich mud the crab can sift for food. Location is on the ground. */
struct FCrabFoodPatch
{
	FVector Location = FVector::ZeroVector;
	/** The drawn patch, and how far the crab may stray from its centre and keep feeding, uu. */
	float Radius = 130.f;
	/** A click this close to the centre is a click on the patch, uu. Generous on purpose. */
	float ClickRadius = 200.f;
	/** What the patch holds now, 0 to 1. */
	float Richness = 1.f;
	/** What the tide refills it to. Low flats are richer, high ones poorer. */
	float FullRichness = 1.f;
	/** Under water since it was last fresh. It refills when the water leaves. */
	bool bSoaked = false;
};

/**
 * The world the crab lives in: a sloped tidal beach with dunes, a creek, rocks,
 * burrows, and a sea whose level follows the tide. The ground is a mesh built
 * from CrabTerrain::Height, so the height the crab stands on, the height the
 * tide rules use and the height on screen are the same function. Built at
 * BeginPlay from engine shapes, and from art in /Game/Crab when it exists.
 */
UCLASS()
class CRABSIM_API ACrabBeach : public AActor
{
	GENERATED_BODY()

public:
	ACrabBeach();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Tide -------------------------------------------------------------

	const FCrabTideSettings& GetTideSettings() const { return Tide; }

	/** Seconds on the tide clock. 0 is low tide, rising. */
	float GetTideClock() const { return TideClock; }
	void SetTideClock(float Seconds);

	/** The water surface right now, tide plus swell, world Z. */
	float GetSurfaceLevel() const { return Tide.SurfaceLevelAt(TideClock); }
	float GetTideLevel() const { return Tide.TideLevelAt(TideClock); }
	/** 0 at low tide, 1 at high tide. */
	float GetTideFraction() const { return Tide.FractionAt(TideClock); }
	bool IsTideRising() const { return Tide.IsRisingAt(TideClock); }
	float GetSecondsToTurn() const { return Tide.SecondsToTurnAt(TideClock); }

	/** A frozen tide holds still, swell and all. The results panel freezes it. */
	void SetTideFrozen(bool bFrozen) { bTideFrozen = bFrozen; }
	bool IsTideFrozen() const { return bTideFrozen; }

	// --- Ground and water ---------------------------------------------------

	float GetGroundHeight(float X, float Y) const;

	/** How deep the water is over the ground at this XY right now. Zero on dry ground. */
	float GetWaterDepthAt(float X, float Y) const;

	/**
	 * Seconds until the water over the ground at this XY is deeper than Depth: 0 if it already is, BIG_NUMBER if
	 * it will not be within Horizon seconds or the tide is frozen.
	 */
	float GetSecondsUntilWaterDeeperThan(float X, float Y, float Depth, float Horizon = 120.f) const;

	// --- Burrows ------------------------------------------------------------

	const TArray<FCrabBurrow>& GetBurrows() const { return Burrows; }

	/** The nearest burrow within MaxDistance of the point (XY only), or INDEX_NONE. */
	int32 FindBurrowNear(const FVector& Point, float MaxDistance) const;

	bool IsBurrowFlooded(int32 Index) const;

	/** The highest of the beach's own burrows, the last to flood. Dug burrows do not count. INDEX_NONE if there are none. */
	int32 FindSafestBurrow() const;

	/** Distance (XY) from the point to the nearest burrow's centre. Huge when there are none. */
	float GetNearestBurrowDistance(const FVector& Point) const;

	int32 GetDugBurrowCount() const;

	/**
	 * Dig a new burrow at the point's XY, on the ground. Returns its index, or INDEX_NONE at the limit of
	 * dug burrows. Whether the spot is fit to dig is the caller's rule (CrabDig).
	 */
	int32 AddDugBurrow(const FVector& Where);

	// --- Food ---------------------------------------------------------------

	const TArray<FCrabFoodPatch>& GetFoodPatches() const { return FoodPatches; }

	/** The nearest patch whose click radius holds the point (XY only), or INDEX_NONE. */
	int32 FindFoodPatchAt(const FVector& Point) const;

	/** Distance (XY) from the point to the nearest patch's centre. Huge when there are none. */
	float GetNearestFoodPatchDistance(const FVector& Point) const;

	/** Take up to Amount of richness from a patch. Returns what it gave, never more than it held. */
	float TakeFood(int32 Index, float Amount);

	// --- New round -----------------------------------------------------------

	/** Back to how a round begins: tide at low water and running, every patch full and fresh, dug burrows gone. */
	void ResetForNewRound();

	/** The generated meshes, for tests and tooling. Null before BeginPlay. */
	UProceduralMeshComponent* GetTerrainMesh() const { return Terrain; }
	UProceduralMeshComponent* GetWaterMesh() const { return Water; }

	UPROPERTY(EditAnywhere, Category = "Tide")
	FCrabTideSettings Tide;

	UPROPERTY(EditAnywhere, Category = "Beach")
	int32 RockCount = 14;

	UPROPERTY(EditAnywhere, Category = "Beach")
	int32 Seed = 7;

	/** Grid spacing of the ground mesh, uu. */
	UPROPERTY(EditAnywhere, Category = "Beach")
	float TerrainStep = 80.f;

	/** Grid spacing of the water mesh near the beach, uu. */
	UPROPERTY(EditAnywhere, Category = "Beach")
	float WaterStep = 100.f;

private:
	void BuildTerrain();
	void BuildWater();
	void BuildBurrows();
	void BuildFoodPatches();
	void BuildProps();
	/** Draws the hole and its rim. Returns the pieces. */
	TArray<UStaticMeshComponent*> AddBurrowVisual(const FVector& Location);
	void UpdateFoodPatches();
	void RefreshPatchVisual(int32 Index);
	void PushTideToMaterials();

	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const FLinearColor* FallbackColor, bool bBlocks);
	static UStaticMesh* LoadEngineShape(const TCHAR* Name);
	static UObject* LoadGameAssetIfPresent(const TCHAR* PackagePath, const TCHAR* ObjectName, UClass* Class);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Terrain;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Water;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> TideParameters;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Shapes;

	TArray<FCrabBurrow> Burrows;

	/** What a patch looks like on the mud: flat mats and pellets that dull and thin as the patch runs out. */
	struct FPatchVisual
	{
		TArray<UStaticMeshComponent*> Pellets;
		UMaterialInstanceDynamic* Mats = nullptr;
		float ShownRichness = -1.f;
	};

	TArray<FCrabFoodPatch> FoodPatches;
	TArray<FPatchVisual> PatchVisuals;
	float TideClock = 0.f;
	bool bTideFrozen = false;
};
