// SPDX-License-Identifier: Apache-2.0
#include "CrabPawn.h"
#include "CrabSim.h"
#include "CrabBeach.h"
#include "CrabMovementMath.h"
#include "CrabSurvivalMath.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<int32> CVarStateLog(
	TEXT("CrabSim.StateLog"), 0,
	TEXT("1 logs the crab's position, heading and speed every 0.2 s as CRABSIM_STATE lines. Live tests read these."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCameraDistance(
	TEXT("CrabSim.CameraDistance"), 0.f,
	TEXT("Camera boom length in uu. 0 keeps the pawn's own TargetArmLength. Useful for viewing the whole map."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCameraPitch(
	TEXT("CrabSim.CameraPitch"), 0.f,
	TEXT("Camera pitch in degrees, negative looks down. 0 keeps the pawn's own pitch."),
	ECVF_Default);

namespace
{
	constexpr float StateLogInterval = 0.2f;
	constexpr float CapsuleRadius = 55.f;
	// Unreal raises a capsule's half-height to at least its radius, so a flat crab still gets a capsule as tall as it is wide.
	constexpr float CapsuleHalfHeight = CapsuleRadius;
	// The parts below are modelled for feet 40 uu under the origin. This drops them onto the real capsule bottom.
	constexpr float ModelFeetOffset = 40.f;
	constexpr float VisualBaseZ = ModelFeetOffset - CapsuleHalfHeight;
	// The Scuttle clip is authored for about this ground speed, uu/s. Playback scales around it.
	constexpr float ScuttleReferenceSpeed = 450.f;
	constexpr float SinkSpeed = 2.2f;

	const FLinearColor ShellColor = FLinearColor(0.75f, 0.16f, 0.06f);
	const FLinearColor ClawColor = FLinearColor(0.9f, 0.28f, 0.08f);
	const FLinearColor EyeColor = FLinearColor(0.02f, 0.02f, 0.02f);
	const FLinearColor MarkerColor = FLinearColor(1.f, 0.85f, 0.2f);

	const TCHAR* SkeletalMeshPackage = TEXT("/Game/Crab/Meshes/SK_FiddlerCrab");
	const TCHAR* ClipNames[] = {TEXT("Idle"), TEXT("Scuttle"), TEXT("Dance"), TEXT("Dash")};
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

	// Fixed-angle camera looking along +X, toward the sea: no rotation to manage.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-38.f, 0.f, 0.f));
	CameraBoom->TargetArmLength = 1150.f;
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
	Camera->FieldOfView = 65.f;

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(GetCapsuleComponent());
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualBaseZ));

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

	for (const UStaticMeshComponent* Claw : ClawParts)
	{
		ClawBaseLocations.Add(Claw->GetRelativeLocation());
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

void ACrabPawn::TryUseSkeletalMesh()
{
	// The art is optional. Without it the crab stays the shape-built one.
	if (!FPackageName::DoesPackageExist(SkeletalMeshPackage))
	{
		return;
	}
	USkeletalMesh* Skeletal = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Crab/Meshes/SK_FiddlerCrab.SK_FiddlerCrab"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Skeletal)
	{
		return;
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FString Package = FString::Printf(TEXT("/Game/Crab/Anims/A_FiddlerCrab_%s"), ClipNames[Index]);
		if (FPackageName::DoesPackageExist(Package))
		{
			Clips[Index] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.A_FiddlerCrab_%s"), *Package, ClipNames[Index]), nullptr, LOAD_Quiet | LOAD_NoWarn);
		}
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	MeshComponent->SetSkeletalMesh(Skeletal);
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -CapsuleHalfHeight), FRotator::ZeroRotator);
	MeshComponent->SetVisibility(true);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Visual->SetVisibility(false, true);
	bUseSkeletalMesh = true;

	if (Clips[static_cast<int32>(ECrabAnim::Idle)])
	{
		MeshComponent->PlayAnimation(Clips[static_cast<int32>(ECrabAnim::Idle)], true);
	}
	UE_LOG(LogCrabSim, Log, TEXT("Using SK_FiddlerCrab, clips: idle=%d scuttle=%d dance=%d dash=%d"),
		Clips[0] != nullptr, Clips[1] != nullptr, Clips[2] != nullptr, Clips[3] != nullptr);
}

void ACrabPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyColors();
	TryUseSkeletalMesh();
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_READY"));
}

ACrabBeach* ACrabPawn::GetBeach() const
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

float ACrabPawn::GetFeetZ() const
{
	return GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

float ACrabPawn::GetMessageAlpha() const
{
	return MessageTimeRemaining <= 0.f ? 0.f : FMath::Clamp(MessageTimeRemaining / 0.6f, 0.f, 1.f);
}

void ACrabPawn::SetMessage(const FString& Text, float Seconds, bool bReplaceCurrent)
{
	if (!bReplaceCurrent && MessageTimeRemaining > 0.f)
	{
		return;
	}
	Message = Text;
	MessageDuration = FMath::Max(Seconds, 0.1f);
	MessageTimeRemaining = MessageDuration;
}

void ACrabPawn::LogEvent(const TCHAR* Name) const
{
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_EVENT %s t=%.2f"), Name, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
}

const TCHAR* ACrabPawn::AnimName(ECrabAnim State)
{
	switch (State)
	{
	case ECrabAnim::Scuttle: return TEXT("Scuttle");
	case ECrabAnim::Dance: return TEXT("Dance");
	case ECrabAnim::Dash: return TEXT("Dash");
	default: return TEXT("Idle");
	}
}

// --- Walking ---------------------------------------------------------------

void ACrabPawn::SetMoveTarget(const FVector& WorldPoint, int32 EnterBurrowIndex)
{
	// A crab in its burrow stays put until it is told to come out (ExitBurrow).
	if (IsInBurrow())
	{
		return;
	}
	MoveTarget = FVector(WorldPoint.X, WorldPoint.Y, GetFeetZ());
	PendingBurrow = EnterBurrowIndex;
	bHasTarget = true;
	TargetMarker->SetWorldLocation(MoveTarget + FVector(0.f, 0.f, 3.f));
	TargetMarker->SetHiddenInGame(false);
}

void ACrabPawn::ClearMoveTarget()
{
	bHasTarget = false;
	PendingBurrow = INDEX_NONE;
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

	ExitBurrow();
	StopDance();

	DashDirection = Direction;
	DashTimeRemaining = DashDuration;
	DashCooldownRemaining = DashCooldown;

	// A burst is an impulse: full speed now, not after the acceleration ramp. The speed cap goes up
	// with it, or the movement component would brake the burst back to the old cap before the next Tick.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = SideSpeed * DashSpeedMultiplier;
	Move->Velocity = DashDirection * SideSpeed * DashSpeedMultiplier;
	// And input must already be waiting: a movement component that ticks first and finds none brakes the burst.
	AddMovementInput(DashDirection, 1.f);
	return true;
}

// --- Dance -------------------------------------------------------------------

bool ACrabPawn::StartDance()
{
	if (bDancing)
	{
		return true;
	}
	if (IsInBurrow() || IsDashing() || WaterDepth > CrabSurvival::SurgeDepth)
	{
		return false;
	}
	ClearMoveTarget();
	bDancing = true;
	DanceClock = 0.f;
	LogEvent(TEXT("dance_start"));
	return true;
}

void ACrabPawn::StopDance()
{
	if (!bDancing)
	{
		return;
	}
	bDancing = false;
	LogEvent(TEXT("dance_stop"));
}

bool ACrabPawn::ToggleDance()
{
	if (bDancing)
	{
		StopDance();
		return false;
	}
	return StartDance();
}

// --- Burrows -------------------------------------------------------------------

bool ACrabPawn::EnterBurrow(int32 BurrowIndex)
{
	ACrabBeach* Beach = GetBeach();
	if (!Beach || !Beach->GetBurrows().IsValidIndex(BurrowIndex))
	{
		return false;
	}
	if (Beach->IsBurrowFlooded(BurrowIndex))
	{
		SetMessage(TEXT("That burrow is flooded"));
		return false;
	}

	const FCrabBurrow& Burrow = Beach->GetBurrows()[BurrowIndex];
	StopDance();
	ClearMoveTarget();
	DashTimeRemaining = 0.f;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	SetActorLocation(FVector(Burrow.Location.X, Burrow.Location.Y, Burrow.Location.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	Move->StopMovementImmediately();
	Move->DisableMovement();

	CurrentBurrow = BurrowIndex;
	SetMessage(TEXT("Dug in"));
	LogEvent(TEXT("burrow_enter"));
	return true;
}

void ACrabPawn::ExitBurrow()
{
	if (!IsInBurrow())
	{
		return;
	}
	CurrentBurrow = INDEX_NONE;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	LogEvent(TEXT("burrow_exit"));
}

void ACrabPawn::UpdateBurrowSink(float DeltaSeconds)
{
	const float Goal = IsInBurrow() ? 1.f : 0.f;
	BurrowSink = FMath::FInterpConstantTo(BurrowSink, Goal, DeltaSeconds, SinkSpeed);

	const FVector Offset(0.f, 0.f, -BurrowSink * BurrowSinkDistance);
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualBaseZ) + Offset);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -CapsuleHalfHeight) + Offset);

	// Once fully under, nothing shows through the sand, so stop drawing it.
	const bool bHidden = BurrowSink > 0.97f;
	if (bUseSkeletalMesh)
	{
		GetMesh()->SetVisibility(!bHidden);
	}
	else
	{
		Visual->SetVisibility(!bHidden, true);
	}
	if (bHidden)
	{
		TargetMarker->SetHiddenInGame(true);
	}
}

// --- Survival ---------------------------------------------------------------------

void ACrabPawn::SweepOut()
{
	ACrabBeach* Beach = GetBeach();
	StopDance();
	ExitBurrow();
	ClearMoveTarget();
	DashTimeRemaining = 0.f;
	++SweptCount;

	FVector Landing = GetActorLocation();
	if (Beach)
	{
		const int32 Haven = Beach->FindSafestBurrow();
		if (Haven != INDEX_NONE)
		{
			Landing = Beach->GetBurrows()[Haven].Location + FVector(140.f, 0.f, 0.f);
		}
	}
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Ground = Beach ? Beach->GetGroundHeight(Landing.X, Landing.Y) : Landing.Z;
	SetActorLocation(FVector(Landing.X, Landing.Y, Ground + Half + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->StopMovementImmediately();

	Grip = 0.5f;
	SetMessage(TEXT("Swept out! Washed up by the dunes"), 3.5f);
	LogEvent(TEXT("swept_out"));
}

void ACrabPawn::UpdateSurvival(float DeltaSeconds)
{
	ACrabBeach* Beach = GetBeach();
	if (!Beach)
	{
		WaterDepth = 0.f;
		return;
	}

	const FVector Location = GetActorLocation();
	if (IsInBurrow())
	{
		WaterDepth = 0.f;
		if (Beach->IsBurrowFlooded(CurrentBurrow))
		{
			ExitBurrow();
			SetMessage(TEXT("Flooded out!"), 3.f);
			LogEvent(TEXT("flooded_out"));
		}
	}
	else
	{
		WaterDepth = Beach->GetWaterDepthAt(Location.X, Location.Y);
	}

	const bool bInSurge = WaterDepth > CrabSurvival::SurgeDepth;
	if (bInSurge && !bWasInSurge)
	{
		// Advice, not news: it must not talk over "Flooded out!" or "Swept out!".
		SetMessage(TEXT("The tide has you! Get to higher ground"), 3.f, /*bReplaceCurrent=*/false);
		LogEvent(TEXT("surge_begin"));
	}
	bWasInSurge = bInSurge;

	const float Drain = CrabSurvival::GripDrainPerSecond(WaterDepth);
	const float Regen = CrabSurvival::GripRegenPerSecond(WaterDepth, IsInBurrow());
	Grip = FMath::Clamp(Grip + (Regen - Drain) * DeltaSeconds, 0.f, 1.f);

	if (bInSurge)
	{
		StopDance();
		// A rising tide shoves the crab up the beach, a falling one drags it out to sea.
		const float Direction = Beach->IsTideRising() ? -1.f : 1.f;
		AddActorWorldOffset(FVector(Direction * CrabSurvival::SurgePushSpeed(WaterDepth) * DeltaSeconds, 0.f, 0.f), true);
	}

	if (Grip <= 0.f)
	{
		SweepOut();
	}
}

// --- Per-frame update -----------------------------------------------------------

void ACrabPawn::UpdateWalking(float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	FVector Direction = FVector::ZeroVector;
	float Speed = 0.f;
	float DistanceToTarget = 0.f;

	if (IsInBurrow())
	{
		return;
	}

	if (IsDashing())
	{
		DashTimeRemaining = FMath::Max(0.f, DashTimeRemaining - DeltaSeconds);
		Direction = DashDirection;
		Speed = SideSpeed * DashSpeedMultiplier;
	}
	else if (bDancing)
	{
		// The crab turns to face the camera and holds its ground.
		const float CurrentYaw = GetActorRotation().Yaw;
		SetActorRotation(FRotator(0.f, FMath::FixedTurn(CurrentYaw, DanceFacingYaw, TurnRate * DeltaSeconds), 0.f));
		return;
	}
	else if (bHasTarget)
	{
		FVector ToTarget = MoveTarget - GetActorLocation();
		ToTarget.Z = 0.f;
		DistanceToTarget = ToTarget.Size();
		if (DistanceToTarget <= ArrivalRadius)
		{
			const int32 Burrow = PendingBurrow;
			ClearMoveTarget();
			if (Burrow != INDEX_NONE)
			{
				EnterBurrow(Burrow);
			}
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
			Speed *= CrabSurvival::SpeedScaleForDepth(WaterDepth);
		}

		Move->MaxWalkSpeed = Speed;
		AddMovementInput(Direction, 1.f);
	}
}

void ACrabPawn::SetAnimState(ECrabAnim NewState)
{
	if (NewState == AnimState)
	{
		return;
	}
	AnimState = NewState;
	if (!bUseSkeletalMesh)
	{
		return;
	}
	if (UAnimSequence* Clip = Clips[static_cast<int32>(NewState)])
	{
		GetMesh()->PlayAnimation(Clip, NewState != ECrabAnim::Dash);
		GetMesh()->SetPlayRate(1.f);
	}
}

void ACrabPawn::UpdateAnimation(float DeltaSeconds)
{
	const FVector Velocity = GetCharacterMovement()->Velocity;
	const float Speed = Velocity.Size2D();

	ECrabAnim Desired = ECrabAnim::Idle;
	if (IsDashing())
	{
		Desired = ECrabAnim::Dash;
	}
	else if (bDancing)
	{
		Desired = ECrabAnim::Dance;
	}
	else if (Speed > 40.f && !IsInBurrow())
	{
		Desired = ECrabAnim::Scuttle;
	}
	SetAnimState(Desired);

	// The clip travels toward the crab's right. Playing it backward travels left.
	if (bUseSkeletalMesh && AnimState == ECrabAnim::Scuttle)
	{
		const float Side = FVector::DotProduct(Velocity.GetSafeNormal2D(), GetActorRightVector()) >= 0.f ? 1.f : -1.f;
		GetMesh()->SetPlayRate(Side * FMath::Clamp(Speed / ScuttleReferenceSpeed, 0.4f, 2.f));
	}

	UpdateProceduralDance(DeltaSeconds);
}

void ACrabPawn::UpdateProceduralDance(float DeltaSeconds)
{
	DanceBlend = FMath::FInterpTo(DanceBlend, bDancing ? 1.f : 0.f, DeltaSeconds, 6.f);
	if (bDancing)
	{
		DanceClock += DeltaSeconds;
	}
	if (bUseSkeletalMesh || BurrowSink > 0.f)
	{
		return;
	}

	// Stand-in dance for the shape-built crab: bounce on the beat (120 bpm), rock side to side, alternate the claws.
	const float Beat = DanceClock * 2.f;
	const float Bounce = FMath::Abs(FMath::Sin(PI * Beat)) * 9.f * DanceBlend;
	const float Rock = FMath::Sin(PI * Beat) * 9.f * DanceBlend;
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualBaseZ + Bounce));
	Visual->SetRelativeRotation(FRotator(0.f, 0.f, Rock));
	for (int32 Index = 0; Index < ClawParts.Num() && Index < ClawBaseLocations.Num(); ++Index)
	{
		const float Alternate = Index == 0 ? 0.f : PI;
		const float Lift = (0.5f + 0.5f * FMath::Sin(PI * Beat + Alternate)) * 55.f * DanceBlend;
		ClawParts[Index]->SetRelativeLocation(ClawBaseLocations[Index] + FVector(0.f, 0.f, Lift));
	}
}

void ACrabPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	DashCooldownRemaining = FMath::Max(0.f, DashCooldownRemaining - DeltaSeconds);
	MessageTimeRemaining = FMath::Max(0.f, MessageTimeRemaining - DeltaSeconds);

	const float CameraDistance = CVarCameraDistance.GetValueOnGameThread();
	if (CameraDistance > 0.f)
	{
		CameraBoom->TargetArmLength = CameraDistance;
	}
	const float CameraPitch = CVarCameraPitch.GetValueOnGameThread();
	if (CameraPitch != 0.f)
	{
		CameraBoom->SetRelativeRotation(FRotator(CameraPitch, 0.f, 0.f));
	}

	UpdateSurvival(DeltaSeconds);
	UpdateBurrowSink(DeltaSeconds);
	UpdateWalking(DeltaSeconds);
	UpdateAnimation(DeltaSeconds);

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
	const ACrabBeach* Beach = GetBeach();
	// The live test parses everything up to dash=. New fields go after it.
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_STATE t=%.2f loc=%.1f,%.1f,%.1f yaw=%.1f speed=%.1f target=%s dash=%d grip=%.2f depth=%.1f water=%.1f tide=%.2f burrow=%d dance=%d anim=%s skel=%d swept=%d"),
		GetWorld()->GetTimeSeconds(), Location.X, Location.Y, Location.Z,
		FRotator::NormalizeAxis(GetActorRotation().Yaw), GetCharacterMovement()->Velocity.Size2D(), *Target, IsDashing() ? 1 : 0,
		Grip, WaterDepth, Beach ? Beach->GetSurfaceLevel() : 0.f, Beach ? Beach->GetTideFraction() : 0.f,
		CurrentBurrow, bDancing ? 1 : 0, AnimName(AnimState), bUseSkeletalMesh ? 1 : 0, SweptCount);
}
