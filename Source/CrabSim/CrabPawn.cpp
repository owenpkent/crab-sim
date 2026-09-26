// SPDX-License-Identifier: Apache-2.0
#include "CrabPawn.h"
#include "CrabSim.h"
#include "CrabMovementMath.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<int32> CVarStateLog(
	TEXT("CrabSim.StateLog"), 0,
	TEXT("1 logs the crab's position, heading and speed every 0.2 s as CRABSIM_STATE lines. Live tests read these."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCameraDistance(
	TEXT("CrabSim.CameraDistance"), 0.f,
	TEXT("Camera boom length in uu. 0 keeps the pawn's own TargetArmLength. Useful for viewing the whole map."),
	ECVF_Default);

namespace
{
	constexpr float StateLogInterval = 0.2f;
	constexpr float CapsuleRadius = 55.f;
	// Unreal raises a capsule's half-height to at least its radius, so a flat crab still gets a capsule as tall as it is wide.
	constexpr float CapsuleHalfHeight = CapsuleRadius;
	// The parts below are modelled for feet 40 uu under the origin. This drops them onto the real capsule bottom.
	constexpr float ModelFeetOffset = 40.f;

	const FLinearColor ShellColor = FLinearColor(0.75f, 0.16f, 0.06f);
	const FLinearColor ClawColor = FLinearColor(0.9f, 0.28f, 0.08f);
	const FLinearColor EyeColor = FLinearColor(0.02f, 0.02f, 0.02f);
	const FLinearColor MarkerColor = FLinearColor(1.f, 0.85f, 0.2f);
}

ACrabPawn::ACrabPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// The crab's heading is set by the scuttle rules in Tick, not by the movement component.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->bUseControllerDesiredRotation = false;
	Move->MaxWalkSpeed = SideSpeed;
	Move->MaxAcceleration = 4000.f;
	Move->BrakingDecelerationWalking = 3000.f;
	Move->GroundFriction = 8.f;

	// Fixed-angle camera: no rotation to manage, screen up is world +X.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
	CameraBoom->TargetArmLength = 1300.f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->FieldOfView = 60.f;

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(GetCapsuleComponent());
	Visual->SetRelativeLocation(FVector(0.f, 0.f, ModelFeetOffset - CapsuleHalfHeight));

	BuildVisual();
}

void ACrabPawn::BuildVisual()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	BasicMaterial = MaterialFinder.Object;
	UStaticMesh* Sphere = SphereFinder.Object;
	UStaticMesh* Cube = CubeFinder.Object;
	UStaticMesh* Cylinder = CylinderFinder.Object;

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

	// Frame: +X is the front (eyes, claws), +/-Y are the sides the crab scuttles on.
	// Modelled with the feet at Z = -ModelFeetOffset, shifted onto the capsule bottom by Visual's offset.
	AddPart(TEXT("Body"), Sphere, Visual, FVector(0.f, 0.f, -8.f), FRotator::ZeroRotator, FVector(0.95f, 1.25f, 0.5f), &ShellParts);

	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const FString Tag = Side < 0 ? TEXT("L") : TEXT("R");
		const float Sign = static_cast<float>(Side);

		// Claws and arms reach forward and out.
		AddPart(TEXT("Arm") + Tag, Cylinder, Visual, FVector(42.f, Sign * 52.f, -6.f), FRotator(0.f, Sign * -25.f, 90.f), FVector(0.1f, 0.1f, 0.5f), &ShellParts);
		AddPart(TEXT("Claw") + Tag, Sphere, Visual, FVector(80.f, Sign * 75.f, -6.f), FRotator::ZeroRotator, FVector(0.5f, 0.32f, 0.22f), &ClawParts);

		// Eyes on stalks at the front.
		AddPart(TEXT("Stalk") + Tag, Cylinder, Visual, FVector(38.f, Sign * 20.f, 10.f), FRotator::ZeroRotator, FVector(0.04f, 0.04f, 0.25f), &ShellParts);
		AddPart(TEXT("Eye") + Tag, Sphere, Visual, FVector(38.f, Sign * 20.f, 25.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.12f), &EyeParts);

		// Four legs a side, splayed outward and down.
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			AddPart(FString::Printf(TEXT("Leg%s%d"), *Tag, Leg), Cube, Visual,
				FVector(-32.f + Leg * 20.f, Sign * 78.f, -26.f), FRotator(0.f, 0.f, Sign * -35.f), FVector(0.07f, 0.6f, 0.06f), &ShellParts);
		}
	}

	TargetMarker = AddPart(TEXT("TargetMarker"), Cylinder, GetCapsuleComponent(), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.7f, 0.7f, 0.02f), nullptr);
	TargetMarker->SetAbsolute(true, true, true);
	TargetMarker->SetCastShadow(false);
	TargetMarker->SetHiddenInGame(true);
}

void ACrabPawn::ApplyColors()
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
	Paint(ShellParts, ShellColor);
	Paint(ClawParts, ClawColor);
	Paint(EyeParts, EyeColor);

	UMaterialInstanceDynamic* Marker = UMaterialInstanceDynamic::Create(BasicMaterial, this);
	Marker->SetVectorParameterValue(TEXT("Color"), MarkerColor);
	TargetMarker->SetMaterial(0, Marker);
}

void ACrabPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyColors();
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_READY"));
}

float ACrabPawn::GetFeetZ() const
{
	return GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void ACrabPawn::SetMoveTarget(const FVector& WorldPoint)
{
	MoveTarget = FVector(WorldPoint.X, WorldPoint.Y, GetFeetZ());
	bHasTarget = true;
	TargetMarker->SetWorldLocation(MoveTarget + FVector(0.f, 0.f, 3.f));
	TargetMarker->SetHiddenInGame(false);
}

void ACrabPawn::ClearMoveTarget()
{
	bHasTarget = false;
	TargetMarker->SetHiddenInGame(true);
}

bool ACrabPawn::TryDash(const FVector& TowardWorldPoint)
{
	if (IsDashing() || DashCooldownRemaining > 0.f)
	{
		return false;
	}

	FVector Direction = TowardWorldPoint - GetActorLocation();
	Direction.Z = 0.f;
	if (!Direction.Normalize())
	{
		return false;
	}

	DashDirection = Direction;
	DashTimeRemaining = DashDuration;
	DashCooldownRemaining = DashCooldown;

	// A burst is an impulse: full speed now, not after the acceleration ramp.
	GetCharacterMovement()->Velocity = DashDirection * SideSpeed * DashSpeedMultiplier;
	return true;
}

void ACrabPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	DashCooldownRemaining = FMath::Max(0.f, DashCooldownRemaining - DeltaSeconds);

	const float CameraOverride = CVarCameraDistance.GetValueOnGameThread();
	if (CameraOverride > 0.f)
	{
		CameraBoom->TargetArmLength = CameraOverride;
	}

	UCharacterMovementComponent* Move = GetCharacterMovement();
	FVector Direction = FVector::ZeroVector;
	float Speed = 0.f;
	float DistanceToTarget = 0.f;

	if (IsDashing())
	{
		DashTimeRemaining = FMath::Max(0.f, DashTimeRemaining - DeltaSeconds);
		Direction = DashDirection;
		Speed = SideSpeed * DashSpeedMultiplier;
	}
	else if (bHasTarget)
	{
		FVector ToTarget = MoveTarget - GetActorLocation();
		ToTarget.Z = 0.f;
		DistanceToTarget = ToTarget.Size();
		if (DistanceToTarget <= ArrivalRadius)
		{
			ClearMoveTarget();
		}
		else
		{
			Direction = ToTarget / DistanceToTarget;
		}
	}

	if (!Direction.IsNearlyZero())
	{
		const float CurrentYaw = GetActorRotation().Yaw;
		const float MoveYaw = Direction.Rotation().Yaw;
		const float NewYaw = FMath::FixedTurn(CurrentYaw, CrabMovementMath::SideFacingYaw(CurrentYaw, MoveYaw), TurnRate * DeltaSeconds);
		SetActorRotation(FRotator(0.f, NewYaw, 0.f));

		if (!IsDashing())
		{
			const float Alignment = CrabMovementMath::SidewaysAlignment(NewYaw, MoveYaw);
			Speed = CrabMovementMath::SpeedForAlignment(Alignment, ForwardSpeed, SideSpeed);
			// Ease in over the last stretch so the crab settles on the point instead of skidding past.
			Speed = FMath::Min(Speed, DistanceToTarget * 5.f + 40.f);
		}

		Move->MaxWalkSpeed = Speed;
		AddMovementInput(Direction, 1.f);
	}

	StateLogTimer += DeltaSeconds;
	if (StateLogTimer >= StateLogInterval)
	{
		StateLogTimer = 0.f;
		if (CVarStateLog.GetValueOnGameThread() != 0)
		{
			LogState();
		}
	}
}

void ACrabPawn::LogState() const
{
	const FVector Location = GetActorLocation();
	const FString Target = bHasTarget ? FString::Printf(TEXT("%.1f,%.1f"), MoveTarget.X, MoveTarget.Y) : FString(TEXT("none"));
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_STATE t=%.2f loc=%.1f,%.1f,%.1f yaw=%.1f speed=%.1f target=%s dash=%d"),
		GetWorld()->GetTimeSeconds(), Location.X, Location.Y, Location.Z,
		FRotator::NormalizeAxis(GetActorRotation().Yaw), GetCharacterMovement()->Velocity.Size2D(), *Target, IsDashing() ? 1 : 0);
}
