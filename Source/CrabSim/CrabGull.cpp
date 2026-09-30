// SPDX-License-Identifier: Apache-2.0
#include "CrabGull.h"
#include "CrabBeach.h"
#include "CrabFoodMath.h"
#include "CrabPawn.h"
#include "CrabSim.h"
#include "CrabTerrainMath.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<int32> CVarGulls(
	TEXT("CrabSim.Gulls"), 1,
	TEXT("0 keeps gulls away, so a live test or a recording that must not be interrupted stays clear of them. 1 (the default) is the game."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGullForce(
	TEXT("CrabSim.GullForce"), 0,
	TEXT("Test only, off by default. 1 sends a gull as soon as there is none and the crab is out of its burrow: no first minute, no wait outside, no cooldown."),
	ECVF_Default);

namespace
{
	constexpr float LogInterval = 0.2f;
	/** How high the body rides above the actor's origin (the ground point under the gull): the length of its legs, uu. */
	constexpr float LegLength = 62.f;
	/** The whole bird is drawn this much bigger than its parts are modelled, so it reads from the camera's distance. */
	constexpr float GullScale = 1.3f;
	constexpr float ShadowRise = 3.f;
	constexpr int32 RingSegments = 14;
	constexpr float RingRadius = 150.f;
	constexpr float ShadowOuterSize = 2.4f;
	constexpr float ShadowInnerSize = 1.5f;
	/** Flat ground a gull may land on: away from the dunes and the banks, and above the water. */
	constexpr float FlatMinX = -1900.f;
	constexpr float FlatMaxX = 4500.f;
	constexpr float FlatHalfY = 2300.f;

	const FLinearColor WhiteColor = FLinearColor(0.93f, 0.93f, 0.9f);
	const FLinearColor GreyColor = FLinearColor(0.5f, 0.52f, 0.56f);
	const FLinearColor TipColor = FLinearColor(0.12f, 0.12f, 0.14f);
	const FLinearColor OrangeColor = FLinearColor(0.95f, 0.5f, 0.05f);
	const FLinearColor ShadowOuterColor = FLinearColor(0.34f, 0.25f, 0.12f);
	const FLinearColor ShadowInnerColor = FLinearColor(0.24f, 0.17f, 0.08f);
	const FLinearColor RingColor = FLinearColor(0.96f, 0.93f, 0.7f);
}

ACrabGull::ACrabGull()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	BuildVisual();
}

void ACrabGull::BuildVisual()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	BasicMaterial = MaterialFinder.Object;
	UStaticMesh* Sphere = SphereFinder.Object;
	UStaticMesh* Cube = CubeFinder.Object;
	UStaticMesh* Cylinder = CylinderFinder.Object;
	UStaticMesh* Cone = ConeFinder.Object;

	auto AddPart = [this](const FString& Name, UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Loc,
		const FRotator& Rot, const FVector& Scale, TArray<TObjectPtr<UStaticMeshComponent>>* Group) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(Mesh);
		Part->SetRelativeLocationAndRotation(Loc, Rot);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Group)
		{
			Group->Add(Part);
		}
		return Part;
	};

	// Frame: +X is the way it faces, +Y its right. The body is drawn LegLength above the actor's origin, so its feet touch the ground.
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(GetRootComponent());
	Body->SetRelativeLocation(FVector(0.f, 0.f, LegLength * GullScale));
	Body->SetRelativeScale3D(FVector(GullScale));

	AddPart(TEXT("Torso"), Sphere, Body, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.9f, 0.9f, 0.8f), &WhiteParts);
	AddPart(TEXT("Neck"), Sphere, Body, FVector(78.f, 0.f, 26.f), FRotator::ZeroRotator, FVector(0.6f, 0.45f, 0.6f), &WhiteParts);
	AddPart(TEXT("Head"), Sphere, Body, FVector(108.f, 0.f, 46.f), FRotator::ZeroRotator, FVector(0.46f, 0.4f, 0.4f), &WhiteParts);
	AddPart(TEXT("Tail"), Cube, Body, FVector(-98.f, 0.f, 6.f), FRotator(8.f, 0.f, 0.f), FVector(0.5f, 0.42f, 0.05f), &WhiteParts);
	AddPart(TEXT("Bill"), Cone, Body, FVector(152.f, 0.f, 40.f), FRotator(-90.f, 0.f, 0.f), FVector(0.17f, 0.17f, 0.42f), &OrangeParts);
	AddPart(TEXT("EyeL"), Sphere, Body, FVector(118.f, -17.f, 56.f), FRotator::ZeroRotator, FVector(0.08f, 0.08f, 0.08f), &DarkParts);
	AddPart(TEXT("EyeR"), Sphere, Body, FVector(118.f, 17.f, 56.f), FRotator::ZeroRotator, FVector(0.08f, 0.08f, 0.08f), &DarkParts);

	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const FString Tag = Side < 0 ? TEXT("L") : TEXT("R");
		const float Sign = static_cast<float>(Side);
		const int32 Slot = Side < 0 ? 0 : 1;

		WingPivot[Slot] = CreateDefaultSubobject<USceneComponent>(*(TEXT("WingPivot") + Tag));
		WingPivot[Slot]->SetupAttachment(Body);
		WingPivot[Slot]->SetRelativeLocation(FVector(6.f, Sign * 38.f, 24.f));
		AddPart(TEXT("Wing") + Tag, Cube, WingPivot[Slot], FVector(0.f, Sign * 115.f, 0.f), FRotator::ZeroRotator, FVector(0.8f, 2.3f, 0.05f), &GreyParts);
		AddPart(TEXT("WingTip") + Tag, Cube, WingPivot[Slot], FVector(-8.f, Sign * 240.f, 0.f), FRotator::ZeroRotator, FVector(0.6f, 0.5f, 0.05f), &DarkParts);

		AddPart(TEXT("Leg") + Tag, Cylinder, Body, FVector(6.f, Sign * 24.f, -LegLength * 0.5f - 6.f), FRotator::ZeroRotator, FVector(0.06f, 0.06f, 0.62f), &LegParts);
		AddPart(TEXT("Foot") + Tag, Cube, Body, FVector(20.f, Sign * 24.f, -LegLength + 2.f), FRotator::ZeroRotator, FVector(0.26f, 0.1f, 0.03f), &OrangeParts);
	}

	// The soft shadow and the ring keep to the ground, whatever the gull does: they are placed in the world every tick.
	auto AddGround = [this](const FString& Name, UStaticMesh* Mesh, const FVector& Scale) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
		Part->SetupAttachment(GetRootComponent());
		Part->SetStaticMesh(Mesh);
		Part->SetRelativeScale3D(Scale);
		Part->SetAbsolute(true, true, true);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		return Part;
	};
	ShadowOuter = AddGround(TEXT("ShadowOuter"), Cylinder, FVector(ShadowOuterSize, ShadowOuterSize, 0.02f));
	ShadowInner = AddGround(TEXT("ShadowInner"), Cylinder, FVector(ShadowInnerSize, ShadowInnerSize, 0.02f));
	for (int32 Segment = 0; Segment < RingSegments; ++Segment)
	{
		RingParts.Add(AddGround(FString::Printf(TEXT("Ring%d"), Segment), Cube, FVector(0.8f, 0.08f, 0.02f)));
	}
}

void ACrabGull::ApplyColors()
{
	auto Paint = [this](TArray<TObjectPtr<UStaticMeshComponent>>& Group, const FLinearColor& Color)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		for (UStaticMeshComponent* Part : Group)
		{
			Part->SetMaterial(0, Material);
		}
	};
	Paint(WhiteParts, WhiteColor);
	Paint(GreyParts, GreyColor);
	Paint(OrangeParts, OrangeColor);
	Paint(LegParts, OrangeColor);
	Paint(DarkParts, TipColor);
	UMaterialInstanceDynamic* Outer = UMaterialInstanceDynamic::Create(BasicMaterial, this);
	Outer->SetVectorParameterValue(TEXT("Color"), ShadowOuterColor);
	ShadowOuter->SetMaterial(0, Outer);
	UMaterialInstanceDynamic* Inner = UMaterialInstanceDynamic::Create(BasicMaterial, this);
	Inner->SetVectorParameterValue(TEXT("Color"), ShadowInnerColor);
	ShadowInner->SetMaterial(0, Inner);
	Paint(RingParts, RingColor);
}

void ACrabGull::BeginPlay()
{
	Super::BeginPlay();
	ApplyColors();
	SetActorHiddenInGame(true);
}

ACrabPawn* ACrabGull::GetCrab() const
{
	if (ACrabPawn* Cached = CrabCache.Get())
	{
		return Cached;
	}
	if (UWorld* World = GetWorld())
	{
		TActorIterator<ACrabPawn> It(World);
		if (It)
		{
			CrabCache = *It;
			return *It;
		}
	}
	return nullptr;
}

ACrabBeach* ACrabGull::GetBeach() const
{
	if (ACrabBeach* Cached = BeachCache.Get())
	{
		return Cached;
	}
	if (UWorld* World = GetWorld())
	{
		TActorIterator<ACrabBeach> It(World);
		if (It)
		{
			BeachCache = *It;
			return *It;
		}
	}
	return nullptr;
}

float ACrabGull::GetCrabDistance() const
{
	const ACrabPawn* Crab = GetCrab();
	if (!Crab)
	{
		return BIG_NUMBER;
	}
	return static_cast<float>(FVector2D::Distance(State.Location, FVector2D(Crab->GetActorLocation().X, Crab->GetActorLocation().Y)));
}

void ACrabGull::ResetForNewRound(int32 RoundIndex)
{
	State = CrabGull::FState(Seed * 7919 + RoundIndex * 104729);
	SeenRound = RoundIndex;
	bSpawnNow = false;
	FoldBlend = 1.f;
	LungeBlend = 0.f;
	SetActorHiddenInGame(true);
}

void ACrabGull::LogEvent(const TCHAR* Name, const FString& Detail) const
{
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_EVENT %s t=%.2f%s%s"), Name, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
		Detail.IsEmpty() ? TEXT("") : TEXT(" "), *Detail);
}

void ACrabGull::LogGull(const CrabGull::FCrabView& View) const
{
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_GULL t=%.2f phase=%s loc=%.1f,%.1f alt=%.0f dist=%.0f patch=%d count=%d"),
		GetWorld()->GetTimeSeconds(), CrabGull::PhaseName(State.Phase), State.Location.X, State.Location.Y, State.Altitude,
		CrabGull::DistanceToCrab(State, View), State.Patch, State.Count);
}

void ACrabGull::HandleEvents(const CrabGull::FStep& Out, const CrabGull::FCrabView& View, ACrabPawn* Crab, ACrabBeach* Beach)
{
	if (Out.EatPatch != INDEX_NONE && Beach)
	{
		Beach->TakeFood(Out.EatPatch, Out.EatAmount);
	}
	const float Distance = CrabGull::DistanceToCrab(State, View);
	for (const CrabGull::EEvent Event : Out.Events)
	{
		switch (Event)
		{
		case CrabGull::EEvent::Circling:
			LogEvent(TEXT("gull_circling"), FString::Printf(TEXT("distance=%.0f count=%d"), Distance, State.Count));
			break;
		case CrabGull::EEvent::Landed:
			LogEvent(TEXT("gull_landed"), FString::Printf(TEXT("patch=%d distance=%.0f"), State.Patch, Distance));
			break;
		case CrabGull::EEvent::Stalking:
			LogEvent(TEXT("gull_stalking"), FString::Printf(TEXT("distance=%.0f"), Distance));
			break;
		case CrabGull::EEvent::Scared:
			LogEvent(TEXT("gull_scared"), FString::Printf(TEXT("distance=%.0f"), Distance));
			break;
		case CrabGull::EEvent::Left:
			LogEvent(TEXT("gull_left"), FString::Printf(TEXT("reason=%s"), CrabGull::LeaveText(State.LeaveReason)));
			break;
		case CrabGull::EEvent::Catch:
			LogEvent(TEXT("gull_catch"), FString::Printf(TEXT("distance=%.0f"), Distance));
			if (Crab)
			{
				Crab->EatenByGull();
			}
			break;
		}
	}
}

void ACrabGull::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ACrabPawn* Crab = GetCrab();
	ACrabBeach* Beach = GetBeach();
	if (!Crab)
	{
		return;
	}
	if (Crab->GetRoundIndex() != SeenRound)
	{
		ResetForNewRound(Crab->GetRoundIndex());
	}

	const FVector CrabAt = Crab->GetActorLocation();
	CrabGull::FCrabView View;
	View.Location = FVector2D(CrabAt.X, CrabAt.Y);
	View.Speed = Crab->GetGroundSpeed();
	View.WaterDepth = Crab->GetWaterDepth();
	View.RoundSeconds = Crab->GetRoundSeconds();
	View.bInBurrow = Crab->IsInBurrow();
	View.bMolting = Crab->IsMolting();
	View.bDancing = Crab->IsDancing();
	View.bRoundOver = Crab->IsRoundOver();

	if (CVarGulls.GetValueOnGameThread() == 0)
	{
		State.Phase = CrabGull::EPhase::Absent;
		State.Altitude = 0.f;
		UpdateVisual(DeltaSeconds, Beach);
		return;
	}

	PatchSpots.Reset();
	if (Beach)
	{
		for (const FCrabFoodPatch& Patch : Beach->GetFoodPatches())
		{
			CrabGull::FPatchSpot Spot;
			Spot.Location = FVector2D(Patch.Location.X, Patch.Location.Y);
			Spot.bFree = !CrabFood::IsEmpty(Patch.Richness) && Beach->GetWaterDepthAt(Patch.Location.X, Patch.Location.Y) <= CrabFood::SoakDepth;
			PatchSpots.Add(Spot);
		}
	}
	CrabGull::FEnvironment Env;
	Env.Patches = PatchSpots;
	Env.IsOpenFlat = [Beach](const FVector2D& Spot)
	{
		return FMath::Abs(Spot.Y) <= FlatHalfY && Spot.X >= FlatMinX && Spot.X <= FlatMaxX
			&& (!Beach || Beach->GetWaterDepthAt(Spot.X, Spot.Y) <= 0.f);
	};

	const CrabGull::ESpawn Mode = bSpawnNow ? CrabGull::ESpawn::Now
		: (CVarGullForce.GetValueOnGameThread() != 0 ? CrabGull::ESpawn::WhenFree : CrabGull::ESpawn::Natural);
	CrabGull::FStep Out;
	CrabGull::Step(State, View, Env, DeltaSeconds, Mode, Out);
	if (Out.Has(CrabGull::EEvent::Circling))
	{
		bSpawnNow = false;
	}
	HandleEvents(Out, View, Crab, Beach);
	UpdateVisual(DeltaSeconds, Beach);

	LogTimer += DeltaSeconds;
	if (LogTimer >= LogInterval)
	{
		LogTimer = 0.f;
		const IConsoleVariable* StateLog = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.StateLog"));
		if (StateLog && StateLog->GetInt() != 0 && IsActive())
		{
			LogGull(View);
		}
	}
}

void ACrabGull::UpdateVisual(float DeltaSeconds, const ACrabBeach* Beach)
{
	const bool bActive = IsActive();
	SetActorHiddenInGame(bSuppressed || !bActive);
	if (!bActive)
	{
		return;
	}

	const float X = State.Location.X;
	const float Y = State.Location.Y;
	const float Ground = Beach ? Beach->GetGroundHeight(X, Y) : 0.f;
	SetActorLocationAndRotation(FVector(X, Y, Ground + State.Altitude), FRotator(0.f, State.Heading, 0.f));

	const bool bFlying = CrabGull::IsFlying(State);
	const CrabGull::EPhase Phase = State.Phase;
	FoldBlend = FMath::FInterpTo(FoldBlend, bFlying ? 0.f : 1.f, DeltaSeconds, 7.f);
	LungeBlend = FMath::FInterpTo(LungeBlend, Phase == CrabGull::EPhase::Lunging ? 1.f : 0.f, DeltaSeconds, 12.f);

	// Wings: flapping in the air, folded to the sides once it is down. Tips up is positive, and the two sides mirror.
	const bool bGliding = Phase == CrabGull::EPhase::Circling;
	FlapClock += DeltaSeconds * (bGliding ? 1.6f : 3.2f);
	const float FlapAmplitude = bGliding ? 26.f : 48.f;
	const float Flap = FMath::Sin(2.f * PI * FlapClock) * FlapAmplitude + (bGliding ? 8.f : 0.f);
	const float Tips = FMath::Lerp(Flap, -78.f, FoldBlend);
	WingTilt = Tips;
	const float Reach = FMath::Lerp(1.f, 0.3f, FoldBlend);
	WingPivot[0]->SetRelativeRotation(FRotator(0.f, 0.f, Tips));
	WingPivot[1]->SetRelativeRotation(FRotator(0.f, 0.f, -Tips));
	WingPivot[0]->SetRelativeScale3D(FVector(1.f, Reach, 1.f));
	WingPivot[1]->SetRelativeScale3D(FVector(1.f, Reach, 1.f));

	// Body: banks into the circle in the air, leans in to a stalk, dips its head for the lunge, bobs as it walks.
	const bool bWalking = Phase == CrabGull::EPhase::Stalking;
	const float Bob = bWalking ? FMath::Abs(FMath::Sin(2.f * PI * 1.8f * FlapClock)) * 7.f : 0.f;
	const float Pitch = (bWalking ? 9.f : 0.f) - 26.f * LungeBlend + (bFlying ? -6.f : 0.f);
	const float Bank = bGliding ? 20.f : 0.f;
	Body->SetRelativeLocation(FVector(30.f * LungeBlend, 0.f, (LegLength + Bob) * GullScale));
	Body->SetRelativeRotation(FRotator(Pitch, 0.f, Bank));
	for (int32 Leg = 0; Leg < LegParts.Num(); ++Leg)
	{
		const float Swing = bWalking ? FMath::Sin(2.f * PI * 1.8f * FlapClock + (Leg == 0 ? 0.f : PI)) * 24.f : 0.f;
		LegParts[Leg]->SetRelativeRotation(FRotator(Swing, 0.f, 0.f));
	}

	// The shadow lies on the ground under the gull, smaller the higher it is. The ring shows once it is down.
	const float Aloft = FMath::Clamp(State.Altitude / CrabGull::Tuning::CircleAltitude, 0.f, 1.f);
	const float ShadowSize = FMath::Lerp(1.f, 0.6f, Aloft);
	// Flat on the ground where it lies, its long side along the heading Yaw (degrees) once it is tilted to the slope.
	auto Lay = [Beach](UStaticMeshComponent* Part, float Px, float Py, float Rise, float Yaw)
	{
		const float Floor = Beach ? Beach->GetGroundHeight(Px, Py) : 0.f;
		const float Radians = FMath::DegreesToRadians(Yaw);
		const FVector Along(FMath::Cos(Radians), FMath::Sin(Radians), 0.f);
		Part->SetWorldLocationAndRotation(FVector(Px, Py, Floor + Rise), FRotationMatrix::MakeFromZX(CrabTerrain::Normal(Px, Py), Along).ToQuat());
	};
	ShadowOuter->SetWorldScale3D(FVector(ShadowOuterSize * ShadowSize, ShadowOuterSize * ShadowSize, 0.02f));
	ShadowInner->SetWorldScale3D(FVector(ShadowInnerSize * ShadowSize, ShadowInnerSize * ShadowSize, 0.02f));
	Lay(ShadowOuter, X, Y, ShadowRise, 0.f);
	Lay(ShadowInner, X, Y, ShadowRise + 0.6f, 0.f);

	const bool bDown = !bFlying && Phase != CrabGull::EPhase::Leaving;
	bRingShown = bDown;
	for (int32 Segment = 0; Segment < RingParts.Num(); ++Segment)
	{
		RingParts[Segment]->SetVisibility(bDown);
		if (bDown)
		{
			const float Angle = 2.f * PI * Segment / RingParts.Num();
			const float Px = X + FMath::Cos(Angle) * RingRadius;
			const float Py = Y + FMath::Sin(Angle) * RingRadius;
			Lay(RingParts[Segment], Px, Py, ShadowRise + 1.f, FMath::RadiansToDegrees(Angle) + 90.f);
		}
	}
}
