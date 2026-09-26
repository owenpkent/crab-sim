// SPDX-License-Identifier: Apache-2.0
#include "CrabBeach.h"
#include "CrabDigMath.h"
#include "CrabFoodMath.h"
#include "CrabSim.h"
#include "CrabTerrainMath.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Math/RandomStream.h"
#include "Misc/PackageName.h"
#include "ProceduralMeshComponent.h"

static TAutoConsoleVariable<float> CVarTideSpeed(
	TEXT("CrabSim.TideSpeed"), 1.f,
	TEXT("Multiplier on how fast the tide clock runs. 10 makes a full tide in 18 seconds."),
	ECVF_Default);

static FAutoConsoleCommandWithWorldAndArgs CmdTideTime(
	TEXT("CrabSim.TideTime"),
	TEXT("Jump the tide clock to this many seconds. 0 is low tide rising, 90 is high tide."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			return;
		}
		for (TActorIterator<ACrabBeach> It(World); It; ++It)
		{
			It->SetTideClock(FCString::Atof(*Args[0]));
		}
	}));

namespace
{
	const TCHAR* MaterialTerrainPath = TEXT("/Game/Crab/Materials/M_Terrain");
	const TCHAR* MaterialWaterPath = TEXT("/Game/Crab/Materials/M_Water");
	const TCHAR* TideCollectionPath = TEXT("/Game/Crab/Materials/MPC_Tide");
	const TCHAR* EnvMeshesPath = TEXT("/Game/Crab/Env/Meshes");

	const FLinearColor SandColor = FLinearColor(0.55f, 0.42f, 0.2f);
	const FLinearColor WaterColor = FLinearColor(0.04f, 0.3f, 0.34f);
	const FLinearColor BurrowColor = FLinearColor(0.02f, 0.015f, 0.01f);
	const FLinearColor RimColor = FLinearColor(0.5f, 0.38f, 0.18f);
	const FLinearColor PatchRichColor = FLinearColor(0.1f, 0.26f, 0.04f);
	const FLinearColor PatchBareColor = FLinearColor(0.36f, 0.27f, 0.15f);
	const FLinearColor PelletColor = FLinearColor(0.28f, 0.42f, 0.08f);

	constexpr float ClearRadiusAtStart = 600.f;
	constexpr float ClearRadiusAtBurrow = 350.f;
	constexpr float ClearRadiusAtPatch = 250.f;
	constexpr float BurrowRadius = 60.f;
	constexpr int32 PatchMatCount = 9;
	constexpr int32 PatchPelletCount = 8;

	/** Positions of a 1D grid: fine spacing across [FineMin, FineMax], growing coarser out to [Min, Max]. */
	TArray<float> BuildAxis(float Min, float Max, float FineMin, float FineMax, float FineStep, float Growth)
	{
		TArray<float> Axis;
		for (float V = FineMin; V < FineMax - 0.01f; V += FineStep)
		{
			Axis.Add(V);
		}
		Axis.Add(FineMax);

		float Step = FineStep;
		for (float V = FineMax; V < Max;)
		{
			Step *= Growth;
			V = FMath::Min(V + Step, Max);
			Axis.Add(V);
		}

		Step = FineStep;
		for (float V = FineMin; V > Min;)
		{
			Step *= Growth;
			V = FMath::Max(V - Step, Min);
			Axis.Insert(V, 0);
		}
		return Axis;
	}

	void AddGrid(const TArray<float>& XS, const TArray<float>& YS, TFunctionRef<float(float, float)> HeightAt, bool bTerrain,
		TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs,
		TArray<FLinearColor>& Colors, TArray<FProcMeshTangent>& Tangents)
	{
		const int32 NX = XS.Num();
		const int32 NY = YS.Num();
		Vertices.Reserve(NX * NY);
		Normals.Reserve(NX * NY);
		UVs.Reserve(NX * NY);
		Colors.Reserve(NX * NY);
		Tangents.Reserve(NX * NY);

		for (int32 J = 0; J < NY; ++J)
		{
			for (int32 I = 0; I < NX; ++I)
			{
				const float X = XS[I];
				const float Y = YS[J];
				const float Z = HeightAt(X, Y);
				Vertices.Add(FVector(X, Y, Z));
				UVs.Add(FVector2D(X / 100.0, Y / 100.0));

				if (bTerrain)
				{
					const FVector N = CrabTerrain::Normal(X, Y);
					Normals.Add(N);
					// Vertex colour: R is macro variation noise, G is how steep the slope is.
					const float Macro = 0.5f + 0.5f * FMath::PerlinNoise2D(FVector2D(X, Y) / 260.0 + FVector2D(3.1, 9.7));
					const float Steep = FMath::Clamp((1.f - N.Z) * 3.f, 0.f, 1.f);
					Colors.Add(FLinearColor(Macro, Steep, 0.f, 1.f));
					FVector Tangent = FVector(1.f, 0.f, -N.X / FMath::Max(N.Z, 0.05f)).GetSafeNormal();
					Tangents.Add(FProcMeshTangent(Tangent, false));
				}
				else
				{
					Normals.Add(FVector::UpVector);
					Colors.Add(FLinearColor::White);
					Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
				}
			}
		}

		// This winding faces up. (Checked on screen: the opposite order culls the whole ground.)
		for (int32 J = 0; J + 1 < NY; ++J)
		{
			for (int32 I = 0; I + 1 < NX; ++I)
			{
				const int32 V00 = J * NX + I;
				const int32 V10 = V00 + 1;
				const int32 V01 = V00 + NX;
				const int32 V11 = V01 + 1;
				Triangles.Append({V00, V01, V10, V10, V01, V11});
			}
		}
	}
}

ACrabBeach::ACrabBeach()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

UObject* ACrabBeach::LoadGameAssetIfPresent(const TCHAR* PackagePath, const TCHAR* ObjectName, UClass* Class)
{
	// Checking first keeps a missing optional asset from logging a load failure.
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		return nullptr;
	}
	return StaticLoadObject(Class, nullptr, *FString::Printf(TEXT("%s.%s"), PackagePath, ObjectName), nullptr, LOAD_Quiet | LOAD_NoWarn);
}

UStaticMesh* ACrabBeach::LoadEngineShape(const TCHAR* Name)
{
	return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
}

void ACrabBeach::BeginPlay()
{
	Super::BeginPlay();

	BasicMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TideParameters = Cast<UMaterialParameterCollection>(LoadGameAssetIfPresent(TideCollectionPath, TEXT("MPC_Tide"), UMaterialParameterCollection::StaticClass()));

	BuildTerrain();
	BuildWater();
	BuildBurrows();
	BuildFoodPatches();
	BuildProps();
	PushTideToMaterials();
}

void ACrabBeach::SetTideClock(float Seconds)
{
	TideClock = FMath::Max(0.f, Seconds);
	if (Water)
	{
		Water->SetWorldLocation(FVector(0.f, 0.f, GetSurfaceLevel()));
	}
	PushTideToMaterials();
	UpdateFoodPatches();
}

void ACrabBeach::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TideClock += DeltaSeconds * CVarTideSpeed.GetValueOnGameThread();
	if (Water)
	{
		Water->SetWorldLocation(FVector(0.f, 0.f, GetSurfaceLevel()));
	}
	PushTideToMaterials();
	UpdateFoodPatches();
}

void ACrabBeach::PushTideToMaterials()
{
	if (TideParameters)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, TideParameters, TEXT("WaterLevel"), GetSurfaceLevel());
	}
}

float ACrabBeach::GetGroundHeight(float X, float Y) const
{
	return CrabTerrain::Height(X, Y);
}

float ACrabBeach::GetWaterDepthAt(float X, float Y) const
{
	return FMath::Max(0.f, GetSurfaceLevel() - CrabTerrain::Height(X, Y));
}

int32 ACrabBeach::FindBurrowNear(const FVector& Point, float MaxDistance) const
{
	int32 Best = INDEX_NONE;
	float BestDistance = MaxDistance;
	for (int32 Index = 0; Index < Burrows.Num(); ++Index)
	{
		const float Distance = FVector::Dist2D(Point, Burrows[Index].Location);
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

bool ACrabBeach::IsBurrowFlooded(int32 Index) const
{
	if (!Burrows.IsValidIndex(Index))
	{
		return false;
	}
	const FCrabBurrow& Burrow = Burrows[Index];
	return GetWaterDepthAt(Burrow.Location.X, Burrow.Location.Y) > Burrow.FloodDepth;
}

int32 ACrabBeach::FindSafestBurrow() const
{
	int32 Best = INDEX_NONE;
	float BestZ = -BIG_NUMBER;
	for (int32 Index = 0; Index < Burrows.Num(); ++Index)
	{
		if (!Burrows[Index].bDug && Burrows[Index].Location.Z > BestZ)
		{
			BestZ = Burrows[Index].Location.Z;
			Best = Index;
		}
	}
	return Best;
}

float ACrabBeach::GetNearestBurrowDistance(const FVector& Point) const
{
	float Nearest = BIG_NUMBER;
	for (const FCrabBurrow& Burrow : Burrows)
	{
		Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Point, Burrow.Location)));
	}
	return Nearest;
}

int32 ACrabBeach::GetDugBurrowCount() const
{
	int32 Count = 0;
	for (const FCrabBurrow& Burrow : Burrows)
	{
		Count += Burrow.bDug ? 1 : 0;
	}
	return Count;
}

int32 ACrabBeach::AddDugBurrow(const FVector& Where)
{
	if (GetDugBurrowCount() >= CrabDig::MaxDug)
	{
		return INDEX_NONE;
	}
	FCrabBurrow Burrow;
	Burrow.Radius = BurrowRadius;
	Burrow.bDug = true;
	Burrow.Location = FVector(Where.X, Where.Y, CrabTerrain::Height(Where.X, Where.Y));
	Burrows.Add(Burrow);
	AddBurrowVisual(Burrow.Location);
	return Burrows.Num() - 1;
}

int32 ACrabBeach::FindFoodPatchAt(const FVector& Point) const
{
	int32 Best = INDEX_NONE;
	float BestDistance = BIG_NUMBER;
	for (int32 Index = 0; Index < FoodPatches.Num(); ++Index)
	{
		const float Distance = FVector::Dist2D(Point, FoodPatches[Index].Location);
		if (Distance <= FoodPatches[Index].ClickRadius && Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

float ACrabBeach::GetNearestFoodPatchDistance(const FVector& Point) const
{
	float Nearest = BIG_NUMBER;
	for (const FCrabFoodPatch& Patch : FoodPatches)
	{
		Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Point, Patch.Location)));
	}
	return Nearest;
}

float ACrabBeach::TakeFood(int32 Index, float Amount)
{
	if (!FoodPatches.IsValidIndex(Index))
	{
		return 0.f;
	}
	FCrabFoodPatch& Patch = FoodPatches[Index];
	const float Taken = FMath::Clamp(Amount, 0.f, Patch.Richness);
	Patch.Richness -= Taken;
	return Taken;
}

void ACrabBeach::UpdateFoodPatches()
{
	for (int32 Index = 0; Index < FoodPatches.Num(); ++Index)
	{
		FCrabFoodPatch& Patch = FoodPatches[Index];
		if (CrabFood::IsSoaked(GetWaterDepthAt(Patch.Location.X, Patch.Location.Y)))
		{
			Patch.bSoaked = true;
		}
		else if (Patch.bSoaked)
		{
			// The tide has come and gone: the flats are fresh again.
			Patch.bSoaked = false;
			Patch.Richness = Patch.FullRichness;
		}
		RefreshPatchVisual(Index);
	}
}

void ACrabBeach::RefreshPatchVisual(int32 Index)
{
	if (!FoodPatches.IsValidIndex(Index) || !PatchVisuals.IsValidIndex(Index))
	{
		return;
	}
	FPatchVisual& Visual = PatchVisuals[Index];
	const float Richness = FoodPatches[Index].Richness;
	if (FMath::IsNearlyEqual(Visual.ShownRichness, Richness, 0.005f))
	{
		return;
	}
	Visual.ShownRichness = Richness;
	if (Visual.Mats)
	{
		Visual.Mats->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(PatchBareColor, PatchRichColor, FMath::Clamp(Richness * 1.25f, 0.f, 1.f)));
	}
	const int32 Shown = FMath::CeilToInt(Visual.Pellets.Num() * FMath::Clamp(Richness, 0.f, 1.f));
	for (int32 Pellet = 0; Pellet < Visual.Pellets.Num(); ++Pellet)
	{
		Visual.Pellets[Pellet]->SetVisibility(Pellet < Shown);
	}
}

UStaticMeshComponent* ACrabBeach::AddShape(UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const FLinearColor* FallbackColor, bool bBlocks)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetStaticMesh(Mesh);
	Shape->SetupAttachment(GetRootComponent());
	Shape->SetRelativeLocationAndRotation(Location, Rotation);
	Shape->SetRelativeScale3D(Scale);
	Shape->SetCollisionProfileName(bBlocks ? TEXT("BlockAll") : TEXT("NoCollision"));

	if (FallbackColor && BasicMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), *FallbackColor);
		Shape->SetMaterial(0, Material);
	}

	Shape->RegisterComponent();
	Shapes.Add(Shape);
	return Shape;
}

void ACrabBeach::BuildTerrain()
{
	TArray<float> XS;
	TArray<float> YS;
	for (float X = CrabTerrain::MinX; X < CrabTerrain::MaxX - 0.01f; X += TerrainStep)
	{
		XS.Add(X);
	}
	XS.Add(CrabTerrain::MaxX);
	for (float Y = -CrabTerrain::HalfWidthY; Y < CrabTerrain::HalfWidthY - 0.01f; Y += TerrainStep)
	{
		YS.Add(Y);
	}
	YS.Add(CrabTerrain::HalfWidthY);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	AddGrid(XS, YS, [](float X, float Y) { return CrabTerrain::Height(X, Y); }, true, Vertices, Triangles, Normals, UVs, Colors, Tangents);

	Terrain = NewObject<UProceduralMeshComponent>(this, TEXT("Terrain"));
	Terrain->SetupAttachment(GetRootComponent());
	// The crab has to stand on this the moment the game starts, so cook its collision now.
	Terrain->bUseAsyncCooking = false;
	Terrain->bUseComplexAsSimpleCollision = true;
	Terrain->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
	Terrain->SetCollisionProfileName(TEXT("BlockAll"));

	UMaterialInterface* Material = Cast<UMaterialInterface>(LoadGameAssetIfPresent(MaterialTerrainPath, TEXT("M_Terrain"), UMaterialInterface::StaticClass()));
	if (!Material && BasicMaterial)
	{
		UMaterialInstanceDynamic* Fallback = UMaterialInstanceDynamic::Create(BasicMaterial, this);
		Fallback->SetVectorParameterValue(TEXT("Color"), SandColor);
		Material = Fallback;
	}
	Terrain->SetMaterial(0, Material);
	Terrain->RegisterComponent();

	// Invisible walls close the map where the ground itself does not: inland, and far out to sea.
	UStaticMesh* Cube = LoadEngineShape(TEXT("Cube"));
	const float Height = 4000.f;
	const float Half = CrabTerrain::HalfWidthY;
	const float Length = (CrabTerrain::MaxX - CrabTerrain::MinX) / 100.f;
	struct FWall { FVector Location; FVector Scale; };
	const FWall Walls[] = {
		{FVector(CrabTerrain::MinX - 50.f, 0.f, 0.f), FVector(1.f, Half * 2.f / 100.f, Height / 100.f)},
		{FVector(CrabTerrain::MaxX + 50.f, 0.f, 0.f), FVector(1.f, Half * 2.f / 100.f, Height / 100.f)},
		{FVector(0.5f * (CrabTerrain::MinX + CrabTerrain::MaxX), Half + 50.f, 0.f), FVector(Length, 1.f, Height / 100.f)},
		{FVector(0.5f * (CrabTerrain::MinX + CrabTerrain::MaxX), -Half - 50.f, 0.f), FVector(Length, 1.f, Height / 100.f)},
	};
	for (const FWall& Wall : Walls)
	{
		UStaticMeshComponent* Shape = AddShape(Cube, Wall.Location, FRotator::ZeroRotator, Wall.Scale, nullptr, true);
		Shape->SetVisibility(false);
	}
}

void ACrabBeach::BuildWater()
{
	// Fine grid across the beach so waves displace properly, coarser out to the horizon.
	const TArray<float> XS = BuildAxis(-40000.f, 60000.f, CrabTerrain::MinX - 100.f, 6000.f, WaterStep, 1.35f);
	const TArray<float> YS = BuildAxis(-50000.f, 50000.f, -CrabTerrain::HalfWidthY - 100.f, CrabTerrain::HalfWidthY + 100.f, WaterStep, 1.35f);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	AddGrid(XS, YS, [](float, float) { return 0.f; }, false, Vertices, Triangles, Normals, UVs, Colors, Tangents);

	Water = NewObject<UProceduralMeshComponent>(this, TEXT("Water"));
	Water->SetupAttachment(GetRootComponent());
	Water->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetCastShadow(false);

	UMaterialInterface* Material = Cast<UMaterialInterface>(LoadGameAssetIfPresent(MaterialWaterPath, TEXT("M_Water"), UMaterialInterface::StaticClass()));
	if (!Material)
	{
		// Stand-in until M_Water is built: the engine's translucent debug material, so the crab and ground show through.
		UMaterialInterface* Translucent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/M_SimpleTranslucent.M_SimpleTranslucent"), nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (Translucent)
		{
			UMaterialInstanceDynamic* Fallback = UMaterialInstanceDynamic::Create(Translucent, this);
			Fallback->SetVectorParameterValue(TEXT("Color"), WaterColor);
			Fallback->SetScalarParameterValue(TEXT("Opacity"), 0.6f);
			Material = Fallback;
		}
		else if (BasicMaterial)
		{
			UMaterialInstanceDynamic* Fallback = UMaterialInstanceDynamic::Create(BasicMaterial, this);
			Fallback->SetVectorParameterValue(TEXT("Color"), WaterColor);
			Material = Fallback;
		}
	}
	Water->SetMaterial(0, Material);
	Water->RegisterComponent();
	Water->SetWorldLocation(FVector(0.f, 0.f, GetSurfaceLevel()));
}

void ACrabBeach::BuildBurrows()
{
	// Spread from the dune foot down to the low flats, so the tide floods them one after another.
	const FVector2D Spots[] = {
		FVector2D(-2450.f, 350.f),
		FVector2D(-1000.f, -520.f),
		FVector2D(700.f, 760.f),
		FVector2D(1900.f, -900.f),
	};

	Burrows.Reset();

	for (const FVector2D& Spot : Spots)
	{
		FCrabBurrow Burrow;
		Burrow.Radius = BurrowRadius;
		Burrow.Location = FVector(Spot.X, Spot.Y, CrabTerrain::Height(Spot.X, Spot.Y));
		Burrows.Add(Burrow);
		AddBurrowVisual(Burrow.Location);
	}
}

void ACrabBeach::AddBurrowVisual(const FVector& Location)
{
	UStaticMesh* Cylinder = LoadEngineShape(TEXT("Cylinder"));
	UStaticMesh* Sphere = LoadEngineShape(TEXT("Sphere"));

	const FVector Normal = CrabTerrain::Normal(Location.X, Location.Y);
	const FRotator Tilt = FRotationMatrix::MakeFromZ(Normal).Rotator();
	AddShape(Cylinder, Location + Normal * 2.f, Tilt, FVector(1.5f, 1.5f, 0.02f), &BurrowColor, false);

	// A low rim of sand thrown up around the hole.
	for (int32 Lump = 0; Lump < 10; ++Lump)
	{
		const float Angle = 2.f * PI * Lump / 10.f;
		const float RimX = Location.X + FMath::Cos(Angle) * 78.f;
		const float RimY = Location.Y + FMath::Sin(Angle) * 78.f;
		AddShape(Sphere, FVector(RimX, RimY, CrabTerrain::Height(RimX, RimY) + 2.f), FRotator::ZeroRotator, FVector(0.36f, 0.36f, 0.14f), &RimColor, false);
	}
}

void ACrabBeach::BuildFoodPatches()
{
	// Mid to low flats. The low ones flood early and are richer, the high ones stay dry longer and are poorer.
	struct FSpec { float X; float Y; float Richness; };
	const FSpec Specs[] = {
		{-1300.f, 450.f, 0.40f},
		{-650.f, 520.f, 0.45f},
		{-350.f, -700.f, 0.60f},
		{600.f, -650.f, 0.75f},
		{1150.f, -80.f, 0.80f},
		{1700.f, -420.f, 0.90f},
		{1800.f, 850.f, 1.00f},
	};

	UStaticMesh* Cylinder = LoadEngineShape(TEXT("Cylinder"));
	UStaticMesh* Sphere = LoadEngineShape(TEXT("Sphere"));
	UMaterialInstanceDynamic* PelletMaterial = nullptr;
	if (BasicMaterial)
	{
		PelletMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, this);
		PelletMaterial->SetVectorParameterValue(TEXT("Color"), PelletColor);
	}

	FoodPatches.Reset();
	PatchVisuals.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Specs); ++Index)
	{
		const FSpec& Spec = Specs[Index];
		FCrabFoodPatch Patch;
		Patch.Location = FVector(Spec.X, Spec.Y, CrabTerrain::Height(Spec.X, Spec.Y));
		Patch.Richness = Spec.Richness;
		Patch.FullRichness = Spec.Richness;
		FoodPatches.Add(Patch);

		FPatchVisual Visual;
		if (BasicMaterial)
		{
			Visual.Mats = UMaterialInstanceDynamic::Create(BasicMaterial, this);
		}

		FRandomStream Random(Seed * 31 + Index);
		// Flat mats of algae, small ones so each follows the ground it lies on.
		for (int32 Mat = 0; Mat < PatchMatCount; ++Mat)
		{
			const float Angle = 2.f * PI * (Mat + Random.FRandRange(0.f, 0.6f)) / PatchMatCount;
			const float Reach = Mat == 0 ? 0.f : Random.FRandRange(0.3f, 0.75f) * Patch.Radius;
			const float MatX = Spec.X + FMath::Cos(Angle) * Reach;
			const float MatY = Spec.Y + FMath::Sin(Angle) * Reach;
			const float Size = Random.FRandRange(0.7f, 1.1f);
			const FVector Normal = CrabTerrain::Normal(MatX, MatY);
			const FVector Where(MatX, MatY, CrabTerrain::Height(MatX, MatY) + 1.5f + 0.2f * Mat);
			UStaticMeshComponent* Shape = AddShape(Cylinder, Where + Normal * 0.5f, FRotationMatrix::MakeFromZ(Normal).Rotator(),
				FVector(Size, Size * Random.FRandRange(0.8f, 1.2f), 0.015f), nullptr, false);
			if (Visual.Mats)
			{
				Shape->SetMaterial(0, Visual.Mats);
			}
		}
		// Pellets on top: the rich ones fade out as the patch is eaten.
		for (int32 Pellet = 0; Pellet < PatchPelletCount; ++Pellet)
		{
			const float Angle = Random.FRandRange(0.f, 2.f * PI);
			const float Reach = FMath::Sqrt(Random.FRand()) * 0.85f * Patch.Radius;
			const float PelletX = Spec.X + FMath::Cos(Angle) * Reach;
			const float PelletY = Spec.Y + FMath::Sin(Angle) * Reach;
			const float Size = Random.FRandRange(0.18f, 0.3f);
			UStaticMeshComponent* Shape = AddShape(Sphere, FVector(PelletX, PelletY, CrabTerrain::Height(PelletX, PelletY) + 3.f),
				FRotator::ZeroRotator, FVector(Size, Size, 0.1f), nullptr, false);
			if (PelletMaterial)
			{
				Shape->SetMaterial(0, PelletMaterial);
			}
			Visual.Pellets.Add(Shape);
		}
		PatchVisuals.Add(Visual);
		RefreshPatchVisual(Index);
	}
}

void ACrabBeach::BuildProps()
{
	// Whatever the art pipeline has put in /Game/Crab/Env/Meshes, sorted by its name prefix.
	TArray<UStaticMesh*> RockMeshes;
	TArray<UStaticMesh*> ShellMeshes;
	TArray<UStaticMesh*> WoodMeshes;
	TArray<UStaticMesh*> PlantMeshes;

	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	Registry.Get().GetAssetsByPath(FName(EnvMeshesPath), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName())
		{
			continue;
		}
		UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (!Mesh)
		{
			continue;
		}
		const FString Name = Asset.AssetName.ToString();
		if (Name.StartsWith(TEXT("SM_Rock")))
		{
			RockMeshes.Add(Mesh);
		}
		else if (Name.StartsWith(TEXT("SM_Shell")))
		{
			ShellMeshes.Add(Mesh);
		}
		else if (Name.StartsWith(TEXT("SM_Driftwood")))
		{
			WoodMeshes.Add(Mesh);
		}
		else if (Name.StartsWith(TEXT("SM_Plant")) || Name.StartsWith(TEXT("SM_Grass")) || Name.StartsWith(TEXT("SM_Seaweed")))
		{
			PlantMeshes.Add(Mesh);
		}
	}

	FRandomStream Random(Seed);
	UStaticMesh* Sphere = LoadEngineShape(TEXT("Sphere"));

	auto ClearOfStartAndBurrows = [this](const FVector2D& Spot, float Radius)
	{
		if (Spot.Size() < ClearRadiusAtStart + Radius)
		{
			return false;
		}
		for (const FCrabBurrow& Burrow : Burrows)
		{
			if (FVector2D::Distance(Spot, FVector2D(Burrow.Location.X, Burrow.Location.Y)) < ClearRadiusAtBurrow + Radius)
			{
				return false;
			}
		}
		for (const FCrabFoodPatch& Patch : FoodPatches)
		{
			if (FVector2D::Distance(Spot, FVector2D(Patch.Location.X, Patch.Location.Y)) < ClearRadiusAtPatch + Radius)
			{
				return false;
			}
		}
		return true;
	};

	// Scatter Count props of one kind between two ground heights. Bounded attempts so a crowded setup cannot loop forever.
	auto Scatter = [&](const TArray<UStaticMesh*>& Meshes, int32 Count, float MinHeight, float MaxHeight, float MinSize, float MaxSize,
		float Radius, bool bBlocks, bool bSinkIntoGround)
	{
		int32 Placed = 0;
		for (int32 Attempt = 0; Attempt < Count * 30 && Placed < Count; ++Attempt)
		{
			const FVector2D Spot(Random.FRandRange(CrabTerrain::MinX + 300.f, CrabTerrain::MaxX - 1500.f), Random.FRandRange(-2300.f, 2300.f));
			const float Ground = CrabTerrain::Height(Spot.X, Spot.Y);
			if (Ground < MinHeight || Ground > MaxHeight || !ClearOfStartAndBurrows(Spot, Radius))
			{
				continue;
			}
			const float Size = Random.FRandRange(MinSize, MaxSize);
			const FRotator Yaw(0.f, Random.FRandRange(0.f, 360.f), 0.f);
			if (Meshes.Num() > 0)
			{
				UStaticMesh* Mesh = Meshes[Random.RandRange(0, Meshes.Num() - 1)];
				AddShape(Mesh, FVector(Spot.X, Spot.Y, Ground - (bSinkIntoGround ? 4.f : 0.f)), Yaw, FVector(Size), nullptr, bBlocks);
			}
			++Placed;
		}
	};

	if (RockMeshes.Num() > 0)
	{
		Scatter(RockMeshes, RockCount, -300.f, 250.f, 0.7f, 1.6f, 200.f, true, true);
	}
	else
	{
		// Stand-in rocks: grey half-buried spheres, until real rocks are imported.
		int32 Placed = 0;
		for (int32 Attempt = 0; Attempt < RockCount * 30 && Placed < RockCount; ++Attempt)
		{
			const FVector2D Spot(Random.FRandRange(CrabTerrain::MinX + 300.f, CrabTerrain::MaxX - 1500.f), Random.FRandRange(-2300.f, 2300.f));
			const float Size = Random.FRandRange(1.6f, 4.f);
			const float Radius = Size * 50.f;
			if (!ClearOfStartAndBurrows(Spot, Radius))
			{
				continue;
			}
			const float Shade = Random.FRandRange(0.12f, 0.22f);
			const FLinearColor Color(Shade, Shade * 0.95f, Shade * 0.85f);
			AddShape(Sphere, FVector(Spot.X, Spot.Y, CrabTerrain::Height(Spot.X, Spot.Y) + Radius * 0.25f), FRotator::ZeroRotator,
				FVector(Size, Size * Random.FRandRange(0.7f, 1.2f), Size * 0.7f), &Color, true);
			++Placed;
		}
	}

	// Decoration only exists when the art does: shells on the flats, driftwood at the high-tide line, plants on the dunes.
	if (ShellMeshes.Num() > 0)
	{
		Scatter(ShellMeshes, 26, -250.f, 60.f, 0.8f, 1.3f, 60.f, false, true);
	}
	if (WoodMeshes.Num() > 0)
	{
		Scatter(WoodMeshes, 5, 60.f, 140.f, 0.8f, 1.2f, 120.f, true, true);
	}
	if (PlantMeshes.Num() > 0)
	{
		Scatter(PlantMeshes, 30, 140.f, 500.f, 0.8f, 1.4f, 80.f, false, true);
	}
}
