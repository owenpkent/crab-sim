// SPDX-License-Identifier: Apache-2.0
#include "CrabColonyNpc.h"
#include "CrabCarryComponent.h"
#include "CrabShapeVisual.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace
{
	// The shape-built body below is modelled for feet 40 uu under its own origin, same as ACrabPawn::BuildVisual
	// (ModelFeetOffset there): this actor's root is the feet themselves (SetPose puts it right on the ground), so
	// Visual is raised by that much to put the model back on its feet.
	constexpr float ModelFeetOffset = 40.f;
	// Held in front of the claws (ACrabPawn's sit at local X=80, Z=-6 off a Visual raised 40 uu: X=80, Z=34 off the
	// feet), pushed a little further out so it reads apart from them rather than between them.
	const FVector CarryOffset = FVector(95.f, 0.f, 34.f);
	// How far the shape-built fallback lifts a claw to read as a wave, cheap and static (see ApplyShapeAnimPose).
	constexpr float DanceClawLift = 55.f;

	// A shade greyer and browner than ACrabPawn's vivid shell (0.75/0.16/0.06) and claw (0.9/0.28/0.08) colours,
	// so a colony crab is told from the player even standing right next to it. Kept close to neutral (R, G and B
	// within a few hundredths of each other) rather than just a darker red: M_CrabShell's "Tint" (confirmed live,
	// CrabSim.ColonyPreview: a pure green test value turned the shell green) reads as a blend with the shell's own
	// warm base texture, so a tint that is itself reddish would still come out reddish, just dimmer.
	const FLinearColor ColonyShellColor = FLinearColor(0.34f, 0.3f, 0.27f);
	const FLinearColor ColonyClawColor = FLinearColor(0.4f, 0.35f, 0.28f);
	const FLinearColor ColonyEyeColor = FLinearColor(0.02f, 0.02f, 0.02f);

	const TCHAR* SkeletalMeshPackage = TEXT("/Game/Crab/Meshes/SK_FiddlerCrab");
	const TCHAR* ClipNames[] = {TEXT("Idle"), TEXT("Scuttle"), TEXT("Dance"), TEXT("Dash")};
}

ACrabColonyNpc::ACrabColonyNpc()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetVisibility(false);

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(GetRootComponent());
	Visual->SetRelativeLocation(FVector(0.f, 0.f, ModelFeetOffset));

	Carry = CreateDefaultSubobject<UCrabCarryComponent>(TEXT("Carry"));
	Carry->SetupAttachment(GetRootComponent());
	Carry->SetRelativeLocation(CarryOffset);
}

void ACrabColonyNpc::BeginPlay()
{
	Super::BeginPlay();
	// BuildVisual makes its parts with NewObject, not CreateDefaultSubobject (CrabShapeVisual::AddTintedShape is
	// shared with UCrabColonyViewComponent and ACrabBeach, which both build well after construction too), so it
	// has to run here rather than in the constructor: NewObject asserts if it is called while the actor is still
	// being constructed.
	BuildVisual();
	TryUseSkeletalMesh();
}

void ACrabColonyNpc::BuildVisual()
{
	BasicMaterial = CrabShapeVisual::LoadBasicMaterial();
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));
	UStaticMesh* Cube = CrabShapeVisual::LoadEngineShape(TEXT("Cube"));
	UStaticMesh* Cylinder = CrabShapeVisual::LoadEngineShape(TEXT("Cylinder"));

	auto AddPart = [this](UStaticMesh* Mesh_, const FVector& Loc, const FRotator& Rot, const FVector& Scale,
		TArray<TObjectPtr<UStaticMeshComponent>>& Group, const FLinearColor& Color)
	{
		Group.Add(CrabShapeVisual::AddTintedShape(this, Visual, Mesh_, Loc, Rot, Scale, BasicMaterial, Color));
	};

	// Same frame and proportions as ACrabPawn::BuildVisual (+X the front, +/-Y the sides it scuttles on), minus
	// the peek and the target marker: this actor never dips into a burrow's hole or takes a move-target click.
	AddPart(Sphere, FVector(0.f, 0.f, -8.f), FRotator::ZeroRotator, FVector(0.95f, 1.25f, 0.5f), ShellParts, ColonyShellColor);

	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float Sign = static_cast<float>(Side);

		AddPart(Cylinder, FVector(42.f, Sign * 52.f, -6.f), FRotator(0.f, Sign * -25.f, 90.f), FVector(0.1f, 0.1f, 0.5f), ShellParts, ColonyShellColor);
		AddPart(Sphere, FVector(80.f, Sign * 75.f, -6.f), FRotator::ZeroRotator, FVector(0.5f, 0.32f, 0.22f), ClawParts, ColonyClawColor);

		AddPart(Cylinder, FVector(38.f, Sign * 20.f, 10.f), FRotator::ZeroRotator, FVector(0.04f, 0.04f, 0.25f), ShellParts, ColonyShellColor);
		AddPart(Sphere, FVector(38.f, Sign * 20.f, 25.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.12f), EyeParts, ColonyEyeColor);

		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			AddPart(Cube, FVector(-32.f + Leg * 20.f, Sign * 78.f, -26.f), FRotator(0.f, 0.f, Sign * -35.f), FVector(0.07f, 0.6f, 0.06f), ShellParts, ColonyShellColor);
		}
	}

	for (const UStaticMeshComponent* Claw : ClawParts)
	{
		ClawBaseLocations.Add(Claw->GetRelativeLocation());
	}
}

void ACrabColonyNpc::TryUseSkeletalMesh()
{
	// A trimmed copy of ACrabPawn::TryUseSkeletalMesh's pattern: this actor has no ACharacter mesh or capsule to
	// hang it on and none of the pawn's debug CVars apply to it, so factoring the two together buys little.
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

	Mesh->SetSkeletalMesh(Skeletal);
	Mesh->SetVisibility(true);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Visual->SetVisibility(false, true);
	bUseSkeletalMesh = true;

	// M_CrabShell exposes a "Tint" vector parameter (the player's own colour); a colony crab overrides it to a
	// greyer, browner shade so the two are told apart. Harmless no-op if a future shell material drops it.
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInterface* Base = Mesh->GetMaterial(Slot))
		{
			UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Base, this);
			Tint->SetVectorParameterValue(TEXT("Tint"), ColonyShellColor);
			Mesh->SetMaterial(Slot, Tint);
		}
	}

	if (Clips[static_cast<int32>(ECrabAnim::Idle)])
	{
		Mesh->PlayAnimation(Clips[static_cast<int32>(ECrabAnim::Idle)], true);
	}
}

void ACrabColonyNpc::SetPose(const FVector& WorldLocation, float FacingYaw)
{
	SetActorLocationAndRotation(WorldLocation, FRotator(0.f, FacingYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
}

void ACrabColonyNpc::SetAnim(ECrabAnim NewAnim)
{
	Anim = NewAnim;
	if (bUseSkeletalMesh)
	{
		if (UAnimSequence* Clip = Clips[static_cast<int32>(NewAnim)])
		{
			Mesh->PlayAnimation(Clip, NewAnim != ECrabAnim::Dash);
		}
		return;
	}
	ApplyShapeAnimPose(NewAnim);
}

void ACrabColonyNpc::ApplyShapeAnimPose(ECrabAnim NewAnim)
{
	if (ClawParts.Num() < 2 || ClawBaseLocations.Num() < 2)
	{
		return;
	}
	// This actor is posed only from outside (SetPose) and never ticks itself, so the shape-built fallback cannot
	// play a clip: it tells Dance apart from everything else with a fixed, lifted claw instead, as if waving.
	const float Lift = NewAnim == ECrabAnim::Dance ? DanceClawLift : 0.f;
	ClawParts[0]->SetRelativeLocation(ClawBaseLocations[0] + FVector(0.f, 0.f, Lift * 0.6f));
	ClawParts[1]->SetRelativeLocation(ClawBaseLocations[1] + FVector(0.f, 0.f, Lift));
}

void ACrabColonyNpc::SetCarrying(ECarry NewCarry)
{
	if (Carry)
	{
		Carry->SetCarrying(NewCarry);
	}
}

void ACrabColonyNpc::SetSizeScale(float NewScale)
{
	GetRootComponent()->SetRelativeScale3D(FVector(NewScale));
}
