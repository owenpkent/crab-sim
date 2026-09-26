// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CrabPawn.generated.h"

class UCameraComponent;
class UMaterialInterface;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * The player's crab. Walks toward a target point and scuttles: it turns so a
 * side faces the way it travels, and side-on travel is faster than forward
 * travel. A dash is a short sideways burst on a cooldown. The visual is built
 * from engine basic shapes so the project needs no imported art yet.
 */
UCLASS()
class CRABSIM_API ACrabPawn : public ACharacter
{
	GENERATED_BODY()

public:
	ACrabPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Walk toward this point. Height is ignored. Keeps walking until it arrives or is cleared. */
	void SetMoveTarget(const FVector& WorldPoint);
	void ClearMoveTarget();
	bool HasMoveTarget() const { return bHasTarget; }
	FVector GetMoveTarget() const { return MoveTarget; }

	/** Start a dash toward the point. False while dashing or on cooldown. */
	bool TryDash(const FVector& TowardWorldPoint);
	bool IsDashing() const { return DashTimeRemaining > 0.f; }
	float GetDashCooldownRemaining() const { return DashCooldownRemaining; }

	/** World Z of the crab's feet, for projecting the cursor onto the ground. */
	float GetFeetZ() const;

	/** Speed while walking forward or backward, uu/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Movement")
	float ForwardSpeed = 250.f;

	/** Speed while scuttling sideways, uu/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Movement")
	float SideSpeed = 450.f;

	/** How fast the crab swings to put a side on its heading, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Movement")
	float TurnRate = 540.f;

	/** Within this distance of the target the crab counts as arrived, uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Movement")
	float ArrivalRadius = 30.f;

	/** Dash speed as a multiple of SideSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Dash")
	float DashSpeedMultiplier = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Dash")
	float DashDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Dash")
	float DashCooldown = 1.2f;

private:
	void BuildVisual();
	void ApplyColors();
	void LogState() const;

	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<USceneComponent> Visual;

	/** Flat ring on the ground where the crab is headed. Not attached to the crab's rotation. */
	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<UStaticMeshComponent> TargetMarker;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ShellParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ClawParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> EyeParts;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	FVector MoveTarget = FVector::ZeroVector;
	FVector DashDirection = FVector::ForwardVector;
	bool bHasTarget = false;
	float DashTimeRemaining = 0.f;
	float DashCooldownRemaining = 0.f;
	float StateLogTimer = 0.f;
};
