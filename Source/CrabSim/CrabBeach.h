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
class UStaticMeshComponent;

/** A hole in the sand the crab can dig into. Location is on the ground. */
struct FCrabBurrow
{
	FVector Location = FVector::ZeroVector;
	float Radius = 60.f;
	/** Water this deep over the hole floods it and forces the crab out. */
	float FloodDepth = 50.f;
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

	// --- Ground and water ---------------------------------------------------

	float GetGroundHeight(float X, float Y) const;

	/** How deep the water is over the ground at this XY right now. Zero on dry ground. */
	float GetWaterDepthAt(float X, float Y) const;

	// --- Burrows ------------------------------------------------------------

	const TArray<FCrabBurrow>& GetBurrows() const { return Burrows; }

	/** The nearest burrow within MaxDistance of the point (XY only), or INDEX_NONE. */
	int32 FindBurrowNear(const FVector& Point, float MaxDistance) const;

	bool IsBurrowFlooded(int32 Index) const;

	/** The highest burrow, the last to flood. INDEX_NONE if there are none. */
	int32 FindSafestBurrow() const;

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
	void BuildProps();
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
	float TideClock = 0.f;
};
