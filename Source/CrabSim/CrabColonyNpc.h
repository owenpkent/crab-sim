// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrabPawn.h"
#include "CrabColonyNpc.generated.h"

class UAnimSequence;
class UCrabCarryComponent;
class UMaterialInterface;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
enum class ECarry : uint8;

/**
 * A colony crab's body, and nothing else: no rules, no jobs, no player input. Whatever runs the colony (a job of
 * its own, wired up later) poses it entirely through SetPose/SetAnim/SetCarrying/SetSizeScale. The same skeletal
 * fiddler crab as the player when Content/Crab exists (ACrabPawn::TryUseSkeletalMesh's pattern, copied and
 * trimmed: this actor has no capsule, never peeks over a hole or works a burrow, so it does not need the parts
 * that pattern builds for those), a plain engine-shape crab when the art does not exist, both tinted a shade
 * greyer and browner than the player's vivid shell (M_CrabShell's "Tint" parameter when the skeletal mesh has it)
 * so a colony crab is told from the player at a glance. Kinematic: no physics, no collision; SetPose is its whole
 * motion.
 */
UCLASS()
class CRABSIM_API ACrabColonyNpc : public AActor
{
	GENERATED_BODY()

public:
	ACrabColonyNpc();

	virtual void BeginPlay() override;

	/** Puts the crab's feet at WorldLocation, facing FacingYaw (degrees, world yaw). Its whole motion: no physics. */
	void SetPose(const FVector& WorldLocation, float FacingYaw);

	/** Which clip plays (single-node, looped, as ACrabPawn plays them). Dig has no clip yet: pass Scuttle for it. */
	void SetAnim(ECrabAnim NewAnim);
	ECrabAnim GetAnim() const { return Anim; }

	/** Shows a pellet or a food wad held at the claws, or hides it (None). */
	void SetCarrying(ECarry NewCarry);

	/** 0.55 for a newly hatched crab, up to 1 for a full-grown one. Scales about the feet, which SetPose places. */
	void SetSizeScale(float NewScale);

private:
	void BuildVisual();
	void TryUseSkeletalMesh();
	/** The shape-built fallback has no clips to play: SetAnim instead swaps in a fixed claw pose, cheap and static. */
	void ApplyShapeAnimPose(ECrabAnim NewAnim);

	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<USceneComponent> Visual;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ShellParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ClawParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> EyeParts;

	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<UCrabCarryComponent> Carry;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Clips[4];

	TArray<FVector> ClawBaseLocations;
	ECrabAnim Anim = ECrabAnim::Idle;
	bool bUseSkeletalMesh = false;
};
