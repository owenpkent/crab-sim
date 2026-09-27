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

static TAutoConsoleVariable<int32> CVarCrabAnimation(
	TEXT("CrabSim.CrabAnimation"), 1,
	TEXT("0 leaves the skeletal crab in its reference pose, to tell a broken animation from a broken mesh."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCrabPlainMaterial(
	TEXT("CrabSim.CrabPlainMaterial"), 0,
	TEXT("1 draws the skeletal crab in a plain orange engine material, to tell a broken material from a broken mesh."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStartFood(
	TEXT("CrabSim.StartFood"), -1.f,
	TEXT("Test only. 0 to 1 sets the crab's food to this once, whenever the value changes. Negative (the default) leaves food alone."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarFoodFloor(
	TEXT("CrabSim.FoodFloor"), -1.f,
	TEXT("Test only. 0 to 1 keeps the crab's food from dropping below this, so a recording can molt again and again. Negative (the default) is off."),
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
	// Depth the water must fall below the surge line before the surge is over, uu.
	constexpr float SurgeHysteresis = 10.f;
	// A molting crab settles this far into its burrow (0 standing, 1 out of sight), so it can be seen, and pulses.
	constexpr float MoltSink = 0.35f;
	constexpr float MoltAfterglowSeconds = 1.6f;
	constexpr float MoltPulseHz = 1.2f;
	constexpr float MoltPulseSize = 0.04f;
	// How fast the crab swells to its new size after a molt, per second.
	constexpr float GrowthEase = 2.5f;
	// A crab in its burrow shows its eyes, shell top and claw tips over the hole. It rises this far out of the sand (uu),
	// bobs a little and takes about a second to come up.
	constexpr float PeekRise = 110.f;
	// The peek is drawn this much bigger than the crab, so it reads at a glance.
	constexpr float PeekSize = 1.3f;
	constexpr float PeekBobSize = 3.f;
	constexpr float PeekBobHz = 0.7f;
	constexpr float PeekEase = 7.f;

	const FLinearColor ShellColor = FLinearColor(0.75f, 0.16f, 0.06f);
	const FLinearColor ClawColor = FLinearColor(0.9f, 0.28f, 0.08f);
	const FLinearColor EyeColor = FLinearColor(0.02f, 0.02f, 0.02f);
	const FLinearColor EyeWhiteColor = FLinearColor(0.95f, 0.9f, 0.7f);
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
	CameraBoom->TargetArmLength = 1000.f;
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

	// Faces the camera whatever way the crab was heading when it dug in (yaw 180 puts its front toward it).
	PeekRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Peek"));
	PeekRoot->SetupAttachment(GetCapsuleComponent());
	PeekRoot->SetUsingAbsoluteRotation(true);
	PeekRoot->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

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

	// The peek: what shows over the hole while the crab is dug in. Same frame as above (+X the front), with Z = 0 at the
	// sand. Bigger than life on purpose: the crab is a small thing on a big beach, and this has to read at a glance.
	AddPart(TEXT("PeekShell"), Sphere, PeekRoot, FVector(0.f, 0.f, 2.f), FRotator::ZeroRotator, FVector(1.f, 1.2f, 0.7f), &PeekShellParts);
	AddPart(TEXT("PeekClawSmall"), Sphere, PeekRoot, FVector(60.f, -66.f, 26.f), FRotator::ZeroRotator, FVector(0.44f, 0.3f, 0.3f), &PeekClawParts);
	AddPart(TEXT("PeekClawBig"), Sphere, PeekRoot, FVector(56.f, 68.f, 28.f), FRotator::ZeroRotator, FVector(0.7f, 0.44f, 0.4f), &PeekClawParts);
	AddPart(TEXT("PeekFinger"), Sphere, PeekRoot, FVector(98.f, 74.f, 34.f), FRotator::ZeroRotator, FVector(0.32f, 0.18f, 0.16f), &PeekClawParts);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const FString Tag = Side < 0 ? TEXT("L") : TEXT("R");
		const float Sign = static_cast<float>(Side);
		AddPart(TEXT("PeekStalk") + Tag, Cylinder, PeekRoot, FVector(34.f, Sign * 24.f, 40.f), FRotator::ZeroRotator, FVector(0.05f, 0.05f, 0.5f), &PeekShellParts);
		AddPart(TEXT("PeekEye") + Tag, Sphere, PeekRoot, FVector(36.f, Sign * 24.f, 68.f), FRotator::ZeroRotator, FVector(0.2f, 0.2f, 0.2f), &PeekEyeParts);
		AddPart(TEXT("PeekPupil") + Tag, Sphere, PeekRoot, FVector(45.f, Sign * 24.f, 69.f), FRotator::ZeroRotator, FVector(0.1f, 0.1f, 0.1f), &PeekPupilParts);
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
	Paint(PeekShellParts, ShellColor);
	Paint(PeekClawParts, ClawColor);
	Paint(PeekEyeParts, EyeWhiteColor);
	Paint(PeekPupilParts, EyeColor);

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

	if (CVarCrabPlainMaterial.GetValueOnGameThread() != 0 && BasicMaterial)
	{
		UMaterialInstanceDynamic* Plain = UMaterialInstanceDynamic::Create(BasicMaterial, this);
		Plain->SetVectorParameterValue(TEXT("Color"), ClawColor);
		for (int32 Slot = 0; Slot < MeshComponent->GetNumMaterials(); ++Slot)
		{
			MeshComponent->SetMaterial(Slot, Plain);
		}
	}
	if (CVarCrabAnimation.GetValueOnGameThread() == 0)
	{
		for (TObjectPtr<UAnimSequence>& Clip : Clips)
		{
			Clip = nullptr;
		}
	}
	else if (Clips[static_cast<int32>(ECrabAnim::Idle)])
	{
		MeshComponent->PlayAnimation(Clips[static_cast<int32>(ECrabAnim::Idle)], true);
	}
	const FBoxSphereBounds Bounds = Skeletal->GetBounds();
	UE_LOG(LogCrabSim, Log, TEXT("Using SK_FiddlerCrab, clips: idle=%d scuttle=%d dance=%d dash=%d, bounds extent %.1f,%.1f,%.1f origin %.1f,%.1f,%.1f, %d materials, %d bones"),
		Clips[0] != nullptr, Clips[1] != nullptr, Clips[2] != nullptr, Clips[3] != nullptr,
		Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z, Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z,
		Skeletal->GetMaterials().Num(), Skeletal->GetRefSkeleton().GetNum());
}

void ACrabPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyColors();
	PeekRoot->SetVisibility(false, true);
	TryUseSkeletalMesh();
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_READY"));
}

float ACrabPawn::VisualBase() const
{
	return VisualBaseZ + ModelFeetOffset * (DisplayScale - 1.f);
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

void ACrabPawn::LogEvent(const TCHAR* Name, const FString& Detail) const
{
	// The live tests read the name and t=. Anything after t= is detail for a person.
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_EVENT %s t=%.2f%s%s"), Name, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
		Detail.IsEmpty() ? TEXT("") : TEXT(" "), *Detail);
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

void ACrabPawn::SetMoveTarget(const FVector& WorldPoint, int32 EnterBurrowIndex, int32 FeedPatchIndex)
{
	// A crab in its burrow stays put until it is told to come out (ExitBurrow). A won round ignores every order.
	if (IsInBurrow() || bRoundOver)
	{
		return;
	}
	// Holding the button on the patch it is already sifting keeps it sifting.
	if (FeedPatchIndex != INDEX_NONE && FeedPatchIndex == FeedingPatch && EnterBurrowIndex == INDEX_NONE)
	{
		return;
	}
	CancelDig();
	StopFeeding();
	MoveTarget = FVector(WorldPoint.X, WorldPoint.Y, GetFeetZ());
	PendingBurrow = EnterBurrowIndex;
	PendingPatch = FeedPatchIndex;
	bHasTarget = true;
	TargetMarker->SetWorldLocation(MoveTarget + FVector(0.f, 0.f, 3.f));
	TargetMarker->SetHiddenInGame(false);
}

void ACrabPawn::ClearMoveTarget()
{
	bHasTarget = false;
	PendingBurrow = INDEX_NONE;
	PendingPatch = INDEX_NONE;
	TargetMarker->SetHiddenInGame(true);
}

bool ACrabPawn::TryDash(const FVector& TowardWorldPoint)
{
	if (IsDashing() || DashCooldownRemaining > 0.f || bRoundOver)
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
	StopFeeding();
	CancelDig();

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
	if (IsInBurrow() || IsDashing() || bRoundOver || WaterDepth > CrabSurvival::SurgeDepth)
	{
		return false;
	}
	ClearMoveTarget();
	StopFeeding();
	CancelDig();
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
	StopFeeding();
	CancelDig();
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
	CancelMolt(TEXT("left"));
	CurrentBurrow = INDEX_NONE;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	LogEvent(TEXT("burrow_exit"));
}

void ACrabPawn::UpdateBurrowSink(float DeltaSeconds)
{
	// A molting crab, and one that has just molted, stays half out where it can be seen.
	const float Goal = IsInBurrow() ? ((bMolting || MoltAfterglow > 0.f) ? MoltSink : 1.f) : 0.f;
	BurrowSink = FMath::FInterpConstantTo(BurrowSink, Goal, DeltaSeconds, SinkSpeed);

	const FVector Offset(0.f, 0.f, -BurrowSink * BurrowSinkDistance);
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualBase()) + Offset);
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

void ACrabPawn::UpdatePeek(float DeltaSeconds)
{
	// Once the model has sunk out of sight the peek comes up in its place. A molting crab, and one that has just molted,
	// is shown half out by UpdateBurrowSink instead, so it waits.
	const bool bShow = IsInBurrow() && !bMolting && MoltAfterglow <= 0.f && BurrowSink > 0.9f;
	PeekBlend = IsInBurrow() ? FMath::FInterpTo(PeekBlend, bShow ? 1.f : 0.f, DeltaSeconds, PeekEase) : 0.f;
	PeekClock += DeltaSeconds;

	const bool bVisible = PeekBlend > 0.01f;
	PeekRoot->SetVisibility(bVisible, true);
	if (!bVisible)
	{
		return;
	}
	// The parts below the sand are hidden by it, so rising is just moving up. It grows with the crab, and is drawn bigger than it.
	const float Bob = FMath::Sin(2.f * PI * PeekBobHz * PeekClock) * PeekBobSize * PeekBlend;
	PeekRoot->SetRelativeLocation(FVector(0.f, 0.f, -CapsuleHalfHeight + 1.f - (1.f - PeekBlend) * PeekRise + Bob));
	PeekRoot->SetRelativeScale3D(FVector(ShownGrowth * PeekSize));
}

float ACrabPawn::GetPeekTop() const
{
	float Top = -BIG_NUMBER;
	for (const TObjectPtr<UStaticMeshComponent>& Part : PeekShellParts)
	{
		Top = FMath::Max(Top, static_cast<float>(Part->Bounds.Origin.Z + Part->Bounds.BoxExtent.Z));
	}
	for (const TObjectPtr<UStaticMeshComponent>& Part : PeekEyeParts)
	{
		Top = FMath::Max(Top, static_cast<float>(Part->Bounds.Origin.Z + Part->Bounds.BoxExtent.Z));
	}
	return Top;
}

// --- Go-to buttons -----------------------------------------------------------------------

CrabGoto::EFoodResult ACrabPawn::CheckGoToFood(int32* OutPatch) const
{
	const ACrabBeach* Beach = GetBeach();
	TArray<CrabGoto::FPatch> Patches;
	if (Beach)
	{
		const FVector Location = GetActorLocation();
		for (const FCrabFoodPatch& Patch : Beach->GetFoodPatches())
		{
			CrabGoto::FPatch Candidate;
			Candidate.Distance = FVector::Dist2D(Location, Patch.Location);
			Candidate.Richness = Patch.Richness;
			Candidate.WaterDepth = Beach->GetWaterDepthAt(Patch.Location.X, Patch.Location.Y);
			Candidate.SecondsUntilSoaked = Beach->GetSecondsUntilWaterDeeperThan(Patch.Location.X, Patch.Location.Y, CrabFood::SoakDepth);
			Patches.Add(Candidate);
		}
	}
	int32 Chosen = INDEX_NONE;
	const CrabGoto::EFoodResult Result = CrabGoto::EvaluateFood(bRoundOver, Food, Patches, Chosen);
	if (OutPatch)
	{
		*OutPatch = Chosen;
	}
	return Result;
}

CrabGoto::EBurrowResult ACrabPawn::CheckGoToBurrow(int32* OutBurrow) const
{
	const ACrabBeach* Beach = GetBeach();
	TArray<CrabGoto::FBurrow> Burrows;
	if (Beach)
	{
		const FVector Location = GetActorLocation();
		for (int32 Index = 0; Index < Beach->GetBurrows().Num(); ++Index)
		{
			const FCrabBurrow& Burrow = Beach->GetBurrows()[Index];
			CrabGoto::FBurrow Candidate;
			Candidate.Distance = FVector::Dist2D(Location, Burrow.Location);
			Candidate.FloorZ = Burrow.Location.Z;
			Candidate.bFlooded = Beach->IsBurrowFlooded(Index);
			Candidate.SecondsUntilFlooded = Beach->GetSecondsUntilWaterDeeperThan(Burrow.Location.X, Burrow.Location.Y, Burrow.FloodDepth);
			Burrows.Add(Candidate);
		}
	}
	int32 Chosen = INDEX_NONE;
	const CrabGoto::EBurrowResult Result = CrabGoto::EvaluateBurrow(bRoundOver, IsInBurrow(), Burrows, Chosen);
	if (OutBurrow)
	{
		*OutBurrow = Chosen;
	}
	return Result;
}

bool ACrabPawn::GoToFood()
{
	int32 Patch = INDEX_NONE;
	const CrabGoto::EFoodResult Result = CheckGoToFood(&Patch);
	const ACrabBeach* Beach = GetBeach();
	if (Result != CrabGoto::EFoodResult::Ok || !Beach)
	{
		const FString Reason = CrabGoto::ReasonText(Result);
		if (!Reason.IsEmpty())
		{
			SetMessage(Reason);
			LogEvent(TEXT("goto_refused"), FString::Printf(TEXT("reason=%s"), *Reason));
		}
		return false;
	}

	// The same order as clicking the patch: out of the burrow if in one, then walk there and feed.
	const FVector Where = Beach->GetFoodPatches()[Patch].Location;
	LogEvent(TEXT("goto_food"), FString::Printf(TEXT("patch=%d distance=%.0f"), Patch, FVector::Dist2D(GetActorLocation(), Where)));
	ExitBurrow();
	StopDance();
	SetMoveTarget(Where, INDEX_NONE, Patch);
	return true;
}

bool ACrabPawn::GoToBurrow()
{
	int32 Burrow = INDEX_NONE;
	const CrabGoto::EBurrowResult Result = CheckGoToBurrow(&Burrow);
	const ACrabBeach* Beach = GetBeach();
	if (Result != CrabGoto::EBurrowResult::Ok || !Beach)
	{
		const FString Reason = CrabGoto::ReasonText(Result);
		if (!Reason.IsEmpty())
		{
			SetMessage(Reason);
			LogEvent(TEXT("goto_refused"), FString::Printf(TEXT("reason=%s"), *Reason));
		}
		return false;
	}

	const FVector Where = Beach->GetBurrows()[Burrow].Location;
	LogEvent(TEXT("goto_burrow"), FString::Printf(TEXT("burrow=%d distance=%.0f"), Burrow, FVector::Dist2D(GetActorLocation(), Where)));
	StopDance();
	SetMoveTarget(Where, Burrow, INDEX_NONE);
	return true;
}

// --- Food -----------------------------------------------------------------------------

void ACrabPawn::SetFood(float NewFood)
{
	Food = FMath::Clamp(NewFood, 0.f, 1.f);
}

bool ACrabPawn::StartFeeding(int32 PatchIndex)
{
	const ACrabBeach* Beach = GetBeach();
	if (!Beach || !Beach->GetFoodPatches().IsValidIndex(PatchIndex))
	{
		return false;
	}
	if (PatchIndex == FeedingPatch)
	{
		return true;
	}
	if (IsInBurrow() || IsDashing() || bDancing || bDigging || bRoundOver || WaterDepth > CrabSurvival::SurgeDepth)
	{
		return false;
	}

	const FCrabFoodPatch& Patch = Beach->GetFoodPatches()[PatchIndex];
	if (FVector::Dist2D(GetActorLocation(), Patch.Location) > Patch.Radius)
	{
		return false;
	}
	if (CrabFood::IsEmpty(Patch.Richness))
	{
		SetMessage(TEXT("Patch empty"));
		return false;
	}
	if (CrabFood::IsFull(Food))
	{
		SetMessage(TEXT("Not hungry"));
		return false;
	}

	StopFeeding();
	FeedingPatch = PatchIndex;
	FeedGained = 0.f;
	LogEvent(TEXT("food_begin"), FString::Printf(TEXT("patch=%d richness=%.2f food=%.3f"), PatchIndex, Patch.Richness, Food));
	return true;
}

void ACrabPawn::StopFeeding()
{
	if (FeedingPatch == INDEX_NONE)
	{
		return;
	}
	const int32 Patch = FeedingPatch;
	FeedingPatch = INDEX_NONE;
	LogEvent(TEXT("food_end"), FString::Printf(TEXT("patch=%d amount=%.3f food=%.3f"), Patch, FeedGained, Food));
}

void ACrabPawn::EndFeeding(const TCHAR* Text)
{
	StopFeeding();
	SetMessage(Text);
}

// --- Digging ------------------------------------------------------------------------------

CrabDig::EResult ACrabPawn::CheckDig() const
{
	const ACrabBeach* Beach = GetBeach();
	if (!Beach)
	{
		return CrabDig::EResult::NoGround;
	}

	const FVector Location = GetActorLocation();
	CrabDig::FSpot Spot;
	Spot.Food = Food;
	Spot.WaterDepth = IsInBurrow() ? 0.f : Beach->GetWaterDepthAt(Location.X, Location.Y);
	Spot.bInSurge = bWasInSurge;
	Spot.bInBurrow = IsInBurrow();
	Spot.DugCount = Beach->GetDugBurrowCount();
	Spot.NearestBurrowDistance = Beach->GetNearestBurrowDistance(Location);
	Spot.NearestPatchDistance = Beach->GetNearestFoodPatchDistance(Location);
	return CrabDig::Evaluate(Spot);
}

bool ACrabPawn::StartDig()
{
	if (bDigging)
	{
		return true;
	}
	if (bRoundOver)
	{
		return false;
	}
	const CrabDig::EResult Result = CheckDig();
	if (Result != CrabDig::EResult::Ok)
	{
		SetMessage(FString::Printf(TEXT("Cannot dig: %s"), CrabDig::ReasonText(Result)));
		return false;
	}

	StopDance();
	StopFeeding();
	ClearMoveTarget();
	DashTimeRemaining = 0.f;
	bDigging = true;
	DigElapsed = 0.f;
	LogEvent(TEXT("dig_begin"), FString::Printf(TEXT("food=%.3f"), Food));
	return true;
}

void ACrabPawn::CancelDig(const TCHAR* Reason)
{
	if (!bDigging)
	{
		return;
	}
	bDigging = false;
	DigElapsed = 0.f;
	SetMessage(TEXT("Dig cancelled"));
	LogEvent(TEXT("dig_cancel"), FString::Printf(TEXT("reason=%s"), Reason));
}

void ACrabPawn::FinishDig()
{
	ACrabBeach* Beach = GetBeach();
	const int32 Index = Beach ? Beach->AddDugBurrow(GetActorLocation()) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		CancelDig(TEXT("no room"));
		return;
	}
	bDigging = false;
	DigElapsed = 0.f;
	++RoundDug;
	Food = FMath::Max(Food - CrabDig::FoodCost, 0.f);
	SetMessage(TEXT("Burrow dug"));
	LogEvent(TEXT("dig_done"), FString::Printf(TEXT("burrow=%d dug=%d food=%.3f"), Index, Beach->GetDugBurrowCount(), Food));
}

void ACrabPawn::UpdateForaging(float DeltaSeconds)
{
	// The results panel holds everything still, hunger too.
	if (bRoundOver)
	{
		return;
	}
	Food = CrabFood::FoodAfterDrain(Food, DeltaSeconds);

	ACrabBeach* Beach = GetBeach();
	if (!Beach)
	{
		StopFeeding();
		CancelDig(TEXT("no ground"));
		return;
	}

	if (IsFeeding())
	{
		const FCrabFoodPatch* Patch = Beach->GetFoodPatches().IsValidIndex(FeedingPatch) ? &Beach->GetFoodPatches()[FeedingPatch] : nullptr;
		if (!Patch || IsInBurrow() || IsDashing() || bDancing || FVector::Dist2D(GetActorLocation(), Patch->Location) > Patch->Radius + 40.f)
		{
			StopFeeding();
		}
		else
		{
			const float Moved = Beach->TakeFood(FeedingPatch, CrabFood::FeedTransfer(Patch->Richness, Food, DeltaSeconds));
			Food = FMath::Min(Food + Moved, 1.f);
			FeedGained += Moved;
			RoundFoodEaten += Moved;
			if (CrabFood::IsEmpty(Patch->Richness))
			{
				EndFeeding(TEXT("Patch empty"));
			}
			else if (CrabFood::IsFull(Food))
			{
				EndFeeding(TEXT("Fed"));
			}
		}
	}

	if (bDigging)
	{
		const FVector Location = GetActorLocation();
		if (IsInBurrow() || bWasInSurge || Beach->GetWaterDepthAt(Location.X, Location.Y) > 0.f)
		{
			CancelDig(TEXT("water"));
		}
		else
		{
			DigElapsed += DeltaSeconds;
			if (DigElapsed >= CrabDig::Duration)
			{
				FinishDig();
			}
		}
	}
}

// --- Molting -------------------------------------------------------------------------------

CrabMolt::EResult ACrabPawn::CheckMolt() const
{
	CrabMolt::FState State;
	State.Food = Food;
	State.bInBurrow = IsInBurrow();
	State.bInProgress = bMolting;
	State.bRoundOver = bRoundOver;
	return CrabMolt::Evaluate(State);
}

bool ACrabPawn::StartMolt()
{
	if (bMolting)
	{
		return true;
	}
	const CrabMolt::EResult Result = CheckMolt();
	if (Result != CrabMolt::EResult::Ok)
	{
		const FString Reason = CrabMolt::ReasonText(Result);
		if (!Reason.IsEmpty())
		{
			SetMessage(Reason);
			LogEvent(TEXT("molt_refused"), FString::Printf(TEXT("reason=%s"), *Reason));
		}
		return false;
	}

	bMolting = true;
	MoltElapsed = 0.f;
	SetMessage(TEXT("Molting: stay in the burrow"), 3.f);
	LogEvent(TEXT("molt_begin"), FString::Printf(TEXT("burrow=%d food=%.3f"), CurrentBurrow, Food));
	return true;
}

void ACrabPawn::CancelMolt(const TCHAR* Reason)
{
	if (!bMolting)
	{
		return;
	}
	bMolting = false;
	MoltElapsed = 0.f;
	SetMessage(TEXT("Molt cancelled"));
	LogEvent(TEXT("molt_cancel"), FString::Printf(TEXT("reason=%s"), Reason));
}

void ACrabPawn::FinishMolt()
{
	bMolting = false;
	MoltElapsed = 0.f;
	Food = CrabMolt::FoodAfterMolt(Food);
	++Molts;
	Grip = CrabMolt::ClampGrip(1.f, IsSoft());
	MoltAfterglow = MoltAfterglowSeconds;
	SetMessage(FString::Printf(TEXT("Molted (%d of %d)"), Molts, CrabMolt::Tuning::MoltsToWin), 3.f);
	LogEvent(TEXT("molt_done"), FString::Printf(TEXT("molts=%d scale=%.3f food=%.3f"), Molts, GetGrowthScale(), Food));
	if (CrabMolt::IsWon(Molts))
	{
		WinRound();
	}
}

void ACrabPawn::BeginSoft()
{
	const bool bWasSoft = IsSoft();
	SoftRemaining = CrabMolt::Tuning::SoftDuration;
	Grip = CrabMolt::ClampGrip(Grip, true);
	if (!bWasSoft)
	{
		LogEvent(TEXT("soft_begin"), FString::Printf(TEXT("seconds=%.0f"), SoftRemaining));
	}
}

void ACrabPawn::UpdateMolting(float DeltaSeconds)
{
	if (SoftRemaining > 0.f)
	{
		SoftRemaining = CrabMolt::SoftAfter(SoftRemaining, DeltaSeconds);
		if (SoftRemaining <= 0.f)
		{
			LogEvent(TEXT("soft_end"));
		}
	}
	MoltAfterglow = FMath::Max(0.f, MoltAfterglow - DeltaSeconds);

	if (!bMolting)
	{
		return;
	}
	if (!IsInBurrow())
	{
		CancelMolt(TEXT("left"));
		return;
	}
	MoltElapsed += DeltaSeconds;
	if (MoltElapsed >= CrabMolt::Tuning::Duration)
	{
		FinishMolt();
	}
}

void ACrabPawn::UpdateGrowth(float DeltaSeconds)
{
	// A molt swells the crab over a second or so instead of popping it bigger, and a molting crab pulses.
	ShownGrowth = FMath::FInterpTo(ShownGrowth, GetGrowthScale(), DeltaSeconds, GrowthEase);
	MoltBlend = FMath::FInterpTo(MoltBlend, bMolting ? 1.f : 0.f, DeltaSeconds, 5.f);
	if (bMolting)
	{
		MoltClock += DeltaSeconds;
	}
	DisplayScale = ShownGrowth * (1.f + MoltPulseSize * FMath::Sin(2.f * PI * MoltPulseHz * MoltClock) * MoltBlend);

	// The look only: the capsule, so speed, reach and click radii, stays as it was.
	Visual->SetRelativeScale3D(FVector(DisplayScale));
	GetMesh()->SetRelativeScale3D(FVector(DisplayScale));
}

// --- The round -------------------------------------------------------------------------------

void ACrabPawn::WinRound()
{
	bRoundOver = true;
	bNewBest = BestSeconds <= 0.f || RoundSeconds < BestSeconds;
	BestSeconds = CrabMolt::BestAfter(BestSeconds, RoundSeconds);
	StopDance();
	StopFeeding();
	CancelDig(TEXT("round over"));
	ClearMoveTarget();
	DashTimeRemaining = 0.f;
	if (ACrabBeach* Beach = GetBeach())
	{
		Beach->SetTideFrozen(true);
	}
	LogEvent(TEXT("round_won"), FString::Printf(TEXT("time=%.1f molts=%d dug=%d eaten=%.3f best=%.1f"),
		RoundSeconds, Molts, RoundDug, RoundFoodEaten, BestSeconds));
}

void ACrabPawn::EatenByGull()
{
	if (bRoundOver)
	{
		return;
	}
	bRoundOver = true;
	bEaten = true;
	bNewBest = false;
	StopDance();
	StopFeeding();
	CancelDig(TEXT("eaten"));
	CancelMolt(TEXT("eaten"));
	ClearMoveTarget();
	DashTimeRemaining = 0.f;
	GetCharacterMovement()->StopMovementImmediately();
	if (ACrabBeach* Beach = GetBeach())
	{
		Beach->SetTideFrozen(true);
	}
	LogEvent(TEXT("round_eaten"), FString::Printf(TEXT("time=%.1f molts=%d dug=%d eaten=%.3f best=%.1f"),
		RoundSeconds, Molts, RoundDug, RoundFoodEaten, BestSeconds));
}

void ACrabPawn::StartNewRound()
{
	ACrabBeach* Beach = GetBeach();
	CancelMolt(TEXT("new round"));
	ExitBurrow();
	StopDance();
	StopFeeding();
	CancelDig(TEXT("new round"));
	ClearMoveTarget();
	DashTimeRemaining = 0.f;
	DashCooldownRemaining = 0.f;

	bRoundOver = false;
	bNewBest = false;
	bEaten = false;
	++RoundIndex;
	RoundSeconds = 0.f;
	RoundDug = 0;
	RoundFoodEaten = 0.f;
	Molts = 0;
	SweptCount = 0;
	SurgeCount = 0;
	bWasInSurge = false;
	SoftRemaining = 0.f;
	MoltAfterglow = 0.f;
	MoltBlend = 0.f;
	PeekBlend = 0.f;
	ShownGrowth = 1.f;
	DisplayScale = 1.f;
	Food = CrabFood::StartFood;
	Grip = 1.f;
	WaterDepth = 0.f;
	BurrowSink = 0.f;

	if (Beach)
	{
		Beach->ResetForNewRound();
	}
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Ground = Beach ? Beach->GetGroundHeight(RoundStartXY.X, RoundStartXY.Y) : GetActorLocation().Z - Half;
	SetActorLocation(FVector(RoundStartXY.X, RoundStartXY.Y, Ground + Half + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator::ZeroRotator);
	GetCharacterMovement()->StopMovementImmediately();

	SetMessage(TEXT("New round"));
	LogEvent(TEXT("round_new"));
}

void ACrabPawn::ApplyTestFood()
{
	const float Start = CVarStartFood.GetValueOnGameThread();
	if (Start >= 0.f && Start != AppliedStartFood)
	{
		AppliedStartFood = Start;
		SetFood(Start);
	}
	const float Floor = CVarFoodFloor.GetValueOnGameThread();
	if (Floor >= 0.f && !bRoundOver)
	{
		SetFood(FMath::Max(Food, Floor));
	}
}

// --- Survival ---------------------------------------------------------------------

float ACrabPawn::GetGroundSpeed() const
{
	return GetCharacterMovement()->Velocity.Size2D();
}

void ACrabPawn::SetGrip(float NewGrip)
{
	Grip = CrabMolt::ClampGrip(NewGrip, IsSoft());
}

void ACrabPawn::SweepOut()
{
	ACrabBeach* Beach = GetBeach();
	StopDance();
	StopFeeding();
	CancelDig(TEXT("swept"));
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
			// A flood that catches the crab mid-molt leaves it soft.
			const bool bMidMolt = bMolting;
			CancelMolt(TEXT("flood"));
			ExitBurrow();
			SetMessage(bMidMolt ? TEXT("Flooded out mid-molt! Soft for a while") : TEXT("Flooded out!"), 3.f);
			LogEvent(TEXT("flooded_out"));
			if (bMidMolt)
			{
				BeginSoft();
			}
		}
	}
	else
	{
		WaterDepth = Beach->GetWaterDepthAt(Location.X, Location.Y);
	}

	const bool bInSurge = WaterDepth > CrabSurvival::SurgeDepth;
	// Hysteresis: the swell rides across the threshold, so the surge only counts as over once the
	// water has clearly dropped away, not the moment it dips below the line.
	if (bInSurge && !bWasInSurge)
	{
		++SurgeCount;
		bWasInSurge = true;
		// Advice, not news: it must not talk over "Flooded out!" or "Swept out!".
		SetMessage(TEXT("The tide has you! Get to higher ground"), 3.f, /*bReplaceCurrent=*/false);
		LogEvent(TEXT("surge_begin"));
	}
	else if (bWasInSurge && WaterDepth < CrabSurvival::SurgeDepth - SurgeHysteresis)
	{
		bWasInSurge = false;
	}

	const float Drain = CrabSurvival::GripDrainPerSecond(WaterDepth);
	const float Regen = CrabSurvival::GripRegenPerSecond(WaterDepth, IsInBurrow());
	Grip = FMath::Clamp(Grip + (Regen - Drain) * DeltaSeconds, 0.f, CrabMolt::GripCap(IsSoft()));

	if (bInSurge)
	{
		StopDance();
		StopFeeding();
		CancelDig(TEXT("surge"));
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
			const int32 Patch = PendingPatch;
			ClearMoveTarget();
			if (Burrow != INDEX_NONE)
			{
				EnterBurrow(Burrow);
			}
			else if (Patch != INDEX_NONE)
			{
				StartFeeding(Patch);
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
	Visual->SetRelativeLocation(FVector(0.f, 0.f, VisualBase() + Bounce));
	Visual->SetRelativeRotation(FRotator(0.f, 0.f, Rock));
	for (int32 Index = 0; Index < ClawParts.Num() && Index < ClawBaseLocations.Num(); ++Index)
	{
		const float Alternate = Index == 0 ? 0.f : PI;
		const float Lift = (0.5f + 0.5f * FMath::Sin(PI * Beat + Alternate)) * 55.f * DanceBlend;
		ClawParts[Index]->SetRelativeLocation(ClawBaseLocations[Index] + FVector(0.f, 0.f, Lift));
	}
}

void ACrabPawn::UpdateWorkPose(float DeltaSeconds)
{
	FeedBlend = FMath::FInterpTo(FeedBlend, IsFeeding() ? 1.f : 0.f, DeltaSeconds, 8.f);
	DigBlend = FMath::FInterpTo(DigBlend, bDigging ? 1.f : 0.f, DeltaSeconds, 8.f);
	if (IsFeeding() || bDigging)
	{
		WorkClock += DeltaSeconds;
	}
	if (BurrowSink > 0.f || (FeedBlend < 0.01f && DigBlend < 0.01f))
	{
		return;
	}

	// Sifting: the crab dips toward the mud and its small claw scoops to its mouth 2.5 times a second.
	// Digging: it shudders and settles into the sand. Stand-ins until the clips exist. They add to the
	// pose UpdateProceduralDance and UpdateBurrowSink just set this frame.
	const float Scoop = 0.5f + 0.5f * FMath::Sin(2.f * PI * 2.5f * WorkClock);
	const float Shudder = FMath::Sin(2.f * PI * 7.f * WorkClock);
	const float Dip = 4.f * FeedBlend + (5.f + 2.f * Shudder) * DigBlend;
	const float Pitch = -9.f * FeedBlend * (0.6f + 0.4f * Scoop);
	const float Roll = 3.f * Shudder * DigBlend;

	if (bUseSkeletalMesh)
	{
		GetMesh()->SetRelativeRotation(FRotator(Pitch, 0.f, Roll));
		GetMesh()->AddRelativeLocation(FVector(0.f, 0.f, -Dip));
		return;
	}

	Visual->AddRelativeLocation(FVector(0.f, 0.f, -Dip));
	Visual->SetRelativeRotation(FRotator(Pitch, 0.f, Visual->GetRelativeRotation().Roll + Roll));
	if (ClawParts.Num() > 0 && ClawBaseLocations.Num() > 0)
	{
		// The first claw is the small one.
		ClawParts[0]->SetRelativeLocation(ClawBaseLocations[0] + FVector(-28.f, 55.f, 8.f) * Scoop * FeedBlend);
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

	if (!bRoundOver)
	{
		RoundSeconds += DeltaSeconds;
	}
	ApplyTestFood();
	UpdateSurvival(DeltaSeconds);
	UpdateMolting(DeltaSeconds);
	UpdateGrowth(DeltaSeconds);
	UpdateBurrowSink(DeltaSeconds);
	UpdatePeek(DeltaSeconds);
	UpdateWalking(DeltaSeconds);
	UpdateForaging(DeltaSeconds);
	UpdateAnimation(DeltaSeconds);
	UpdateWorkPose(DeltaSeconds);

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
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_STATE t=%.2f loc=%.1f,%.1f,%.1f yaw=%.1f speed=%.1f target=%s dash=%d grip=%.2f depth=%.1f water=%.1f tide=%.2f burrow=%d dance=%d anim=%s skel=%d swept=%d food=%.3f feeding=%d dig=%.2f dug=%d molts=%d molt=%.2f soft=%.1f over=%d scale=%.3f eaten=%d"),
		GetWorld()->GetTimeSeconds(), Location.X, Location.Y, Location.Z,
		FRotator::NormalizeAxis(GetActorRotation().Yaw), GetCharacterMovement()->Velocity.Size2D(), *Target, IsDashing() ? 1 : 0,
		Grip, WaterDepth, Beach ? Beach->GetSurfaceLevel() : 0.f, Beach ? Beach->GetTideFraction() : 0.f,
		CurrentBurrow, bDancing ? 1 : 0, AnimName(AnimState), bUseSkeletalMesh ? 1 : 0, SweptCount,
		Food, FeedingPatch, GetDigProgress(), Beach ? Beach->GetDugBurrowCount() : 0,
		Molts, GetMoltProgress(), SoftRemaining, bRoundOver ? 1 : 0, ShownGrowth, bEaten ? 1 : 0);
}
