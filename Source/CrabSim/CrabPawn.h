// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CrabPawn.generated.h"

class ACrabBeach;
class UAnimSequence;
class UCameraComponent;
class UMaterialInterface;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** Which clip the crab is playing. */
UENUM()
enum class ECrabAnim : uint8
{
	Idle,
	Scuttle,
	Dance,
	Dash,
};

/**
 * The player's crab. Walks toward a target point and scuttles: it turns so a
 * side faces the way it travels, and side-on travel is faster than forward
 * travel. It can dash, dance, dig into a burrow, and it has to keep its grip
 * against the tide. The visual is the skeletal fiddler crab from /Game/Crab
 * when that exists, and a crab built from engine basic shapes when it does not.
 */
UCLASS()
class CRABSIM_API ACrabPawn : public ACharacter
{
	GENERATED_BODY()

public:
	ACrabPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Walking ------------------------------------------------------------

	/**
	 * Walk toward this point. Height is ignored. Keeps walking until it arrives or is cleared.
	 * With a burrow index the crab digs in when it gets there.
	 */
	void SetMoveTarget(const FVector& WorldPoint, int32 EnterBurrowIndex = INDEX_NONE);
	void ClearMoveTarget();
	bool HasMoveTarget() const { return bHasTarget; }
	FVector GetMoveTarget() const { return MoveTarget; }

	/** Start a dash toward the point. False while dashing or on cooldown. Digs out of a burrow and stops a dance first. */
	bool TryDash(const FVector& TowardWorldPoint);
	bool IsDashing() const { return DashTimeRemaining > 0.f; }
	float GetDashCooldownRemaining() const { return DashCooldownRemaining; }

	/** World Z of the crab's feet, for projecting the cursor onto the ground. */
	float GetFeetZ() const;

	// --- Dance --------------------------------------------------------------

	/** Face the camera and wave. False if the crab cannot dance right now: in a burrow, in the surge, or dashing. */
	bool StartDance();
	void StopDance();
	bool ToggleDance();
	bool IsDancing() const { return bDancing; }

	// --- Burrows ------------------------------------------------------------

	/** Dig into a burrow. False if there is no such burrow or it is flooded. */
	bool EnterBurrow(int32 BurrowIndex);
	void ExitBurrow();
	bool IsInBurrow() const { return CurrentBurrow != INDEX_NONE; }
	int32 GetCurrentBurrow() const { return CurrentBurrow; }
	/** 0 standing on the sand, 1 fully underground. */
	float GetBurrowSink() const { return BurrowSink; }

	// --- Survival -------------------------------------------------------------

	/** 1 full, 0 swept away. */
	float GetGrip() const { return Grip; }
	float GetWaterDepth() const { return WaterDepth; }
	int32 GetSweptCount() const { return SweptCount; }

	/** The beach the crab lives on, found on first use. Null if there is none. */
	ACrabBeach* GetBeach() const;

	// --- Feedback -------------------------------------------------------------

	const FString& GetMessage() const { return Message; }
	/** 1 while a message is fresh, fading to 0 as it expires. */
	float GetMessageAlpha() const;
	ECrabAnim GetAnimState() const { return AnimState; }
	bool IsUsingSkeletalMesh() const { return bUseSkeletalMesh; }

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

	/** The crab's facing while it dances, yaw degrees. 180 faces the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Dance")
	float DanceFacingYaw = 180.f;

	/** How far a crab sinks into its burrow, uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Burrow")
	float BurrowSinkDistance = 110.f;

private:
	void BuildVisual();
	void ApplyColors();
	void TryUseSkeletalMesh();
	void UpdateSurvival(float DeltaSeconds);
	void UpdateBurrowSink(float DeltaSeconds);
	void UpdateWalking(float DeltaSeconds);
	void UpdateAnimation(float DeltaSeconds);
	void UpdateProceduralDance(float DeltaSeconds);
	void SetAnimState(ECrabAnim NewState);
	void SweepOut();
	/** Show a line on the HUD. With bReplaceCurrent false it waits its turn: it is dropped if another message is still showing. */
	void SetMessage(const FString& Text, float Seconds = 2.5f, bool bReplaceCurrent = true);
	void LogEvent(const TCHAR* Name) const;
	void LogState() const;
	static const TCHAR* AnimName(ECrabAnim State);

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

	UPROPERTY()
	TObjectPtr<UAnimSequence> Clips[4];

	mutable TWeakObjectPtr<ACrabBeach> BeachCache;

	TArray<FVector> ClawBaseLocations;
	FString Message;
	FVector MoveTarget = FVector::ZeroVector;
	FVector DashDirection = FVector::ForwardVector;
	ECrabAnim AnimState = ECrabAnim::Idle;
	bool bHasTarget = false;
	bool bDancing = false;
	bool bUseSkeletalMesh = false;
	int32 PendingBurrow = INDEX_NONE;
	int32 CurrentBurrow = INDEX_NONE;
	int32 SweptCount = 0;
	float DashTimeRemaining = 0.f;
	float DashCooldownRemaining = 0.f;
	float StateLogTimer = 0.f;
	float Grip = 1.f;
	float WaterDepth = 0.f;
	float BurrowSink = 0.f;
	float DanceClock = 0.f;
	float DanceBlend = 0.f;
	float MessageTimeRemaining = 0.f;
	float MessageDuration = 1.f;
	bool bWasInSurge = false;
};
