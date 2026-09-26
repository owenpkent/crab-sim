// SPDX-License-Identifier: Apache-2.0
#include "CrabBeach.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"

namespace
{
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	const FLinearColor SandColor = FLinearColor(0.55f, 0.42f, 0.2f);
	const FLinearColor DuneColor = FLinearColor(0.4f, 0.3f, 0.14f);
	const FLinearColor BurrowColor = FLinearColor(0.02f, 0.015f, 0.01f);

	constexpr float ClearRadiusAtStart = 600.f;
	constexpr float ClearRadiusAtBurrow = 450.f;
}

ACrabBeach::ACrabBeach()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

UStaticMeshComponent* ACrabBeach::AddShape(const TCHAR* MeshPath, const FVector& Location, const FVector& Scale, const FLinearColor& Color, bool bBlocks)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetStaticMesh(Mesh);
	Shape->SetupAttachment(GetRootComponent());
	Shape->SetRelativeLocation(Location);
	Shape->SetRelativeScale3D(Scale);
	Shape->SetCollisionProfileName(bBlocks ? TEXT("BlockAll") : TEXT("NoCollision"));

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicMaterial, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Shape->SetMaterial(0, Material);

	Shape->RegisterComponent();
	Shapes.Add(Shape);
	return Shape;
}

void ACrabBeach::BeginPlay()
{
	Super::BeginPlay();

	BasicMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	// Sand: the top face sits at SandTopZ.
	const float SlabThickness = 40.f;
	AddShape(CubePath, FVector(0.f, 0.f, SandTopZ - SlabThickness * 0.5f),
		FVector(HalfExtent * 2.f / 100.f, HalfExtent * 2.f / 100.f, SlabThickness / 100.f), SandColor, true);

	// Dune walls on all four edges keep the crab on the sand.
	const float WallHeight = 90.f;
	const float WallThickness = 200.f;
	const float Reach = HalfExtent + WallThickness * 0.5f;
	const float Length = (HalfExtent * 2.f + WallThickness * 2.f) / 100.f;
	const float WallZ = SandTopZ + WallHeight * 0.5f;
	const FVector AlongX(Length, WallThickness / 100.f, WallHeight / 100.f);
	const FVector AlongY(WallThickness / 100.f, Length, WallHeight / 100.f);
	AddShape(CubePath, FVector(Reach, 0.f, WallZ), AlongY, DuneColor, true);
	AddShape(CubePath, FVector(-Reach, 0.f, WallZ), AlongY, DuneColor, true);
	AddShape(CubePath, FVector(0.f, Reach, WallZ), AlongX, DuneColor, true);
	AddShape(CubePath, FVector(0.f, -Reach, WallZ), AlongX, DuneColor, true);

	// Burrows are spaced evenly on a ring around the start.
	BurrowLocations.Reset();
	const float BurrowRing = 1600.f;
	for (int32 Index = 0; Index < BurrowCount; ++Index)
	{
		const float Angle = 2.f * PI * Index / FMath::Max(1, BurrowCount);
		const FVector Location(FMath::Cos(Angle) * BurrowRing, FMath::Sin(Angle) * BurrowRing, SandTopZ);
		BurrowLocations.Add(GetActorLocation() + Location);
		AddShape(CylinderPath, Location + FVector(0.f, 0.f, 0.5f), FVector(1.8f, 1.8f, 0.01f), BurrowColor, false);
	}

	BuildRocks();
}

void ACrabBeach::BuildRocks()
{
	FRandomStream Random(Seed);
	const float Limit = HalfExtent - 500.f;

	int32 Placed = 0;
	// Bounded attempts, so a crowded configuration cannot loop forever.
	for (int32 Attempt = 0; Attempt < RockCount * 20 && Placed < RockCount; ++Attempt)
	{
		const FVector2D Spot(Random.FRandRange(-Limit, Limit), Random.FRandRange(-Limit, Limit));
		const float Size = Random.FRandRange(1.6f, 4.0f);
		const float Radius = Size * 50.f;

		if (Spot.Size() < ClearRadiusAtStart + Radius)
		{
			continue;
		}
		bool bNearBurrow = false;
		for (const FVector& Burrow : BurrowLocations)
		{
			if (FVector2D::Distance(Spot, FVector2D(Burrow.X, Burrow.Y)) < ClearRadiusAtBurrow + Radius)
			{
				bNearBurrow = true;
				break;
			}
		}
		if (bNearBurrow)
		{
			continue;
		}

		const FVector Scale(Size, Size * Random.FRandRange(0.7f, 1.2f), Size * 0.7f);
		const float Shade = Random.FRandRange(0.12f, 0.22f);
		AddShape(SpherePath, FVector(Spot.X, Spot.Y, SandTopZ + Radius * 0.25f), Scale, FLinearColor(Shade, Shade * 0.95f, Shade * 0.85f), true);
		++Placed;
	}
}
