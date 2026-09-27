// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CrabDigMath.h"
#include "CrabFoodMath.h"
#include "CrabGotoMath.h"
#include "CrabMoltMath.h"
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
 * travel. It can dash, dance, dig into a burrow, sift food patches, dig new
 * burrows, molt in a burrow until it is fully grown, and it has to keep its grip against the tide. The visual is the skeletal fiddler crab from /Game/Crab
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
	 * With a burrow index the crab digs in when it gets there, with a food patch index it starts
	 * feeding. Any new target stops feeding and cancels a dig, except the patch it is already feeding on.
	 */
	void SetMoveTarget(const FVector& WorldPoint, int32 EnterBurrowIndex = INDEX_NONE, int32 FeedPatchIndex = INDEX_NONE);
	void ClearMoveTarget();
	bool HasMoveTarget() const { return bHasTarget; }
	FVector GetMoveTarget() const { return MoveTarget; }

	/**
	 * Scales ordinary walking speed, 0 to 1 (clamped): one-stick STEER sets this from how far the stick is
	 * pushed (CrabStick::SteerSpeedMultiplier). SetMoveTarget and ClearMoveTarget both reset it to 1, so a
	 * stale value can never leak into a mouse walk, a go-to walk, a dash (which has its own speed and never
	 * reads this at all), or anything the tide or a gull does to the crab.
	 */
	void SetWalkSpeedMultiplier(float NewMultiplier) { WalkSpeedMultiplier = FMath::Clamp(NewMultiplier, 0.f, 1.f); }
	float GetWalkSpeedMultiplier() const { return WalkSpeedMultiplier; }

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

	// --- Food -----------------------------------------------------------------

	/** 0 empty, 1 full. Low food only stops the crab digging. */
	float GetFood() const { return Food; }
	void SetFood(float NewFood);

	/**
	 * Sift a food patch where the crab stands. False, with a message, if the patch is bare, the crab is
	 * full, or it is not on the patch. The food moves from the patch into the crab while it stands still.
	 */
	bool StartFeeding(int32 PatchIndex);
	void StopFeeding();
	bool IsFeeding() const { return FeedingPatch != INDEX_NONE; }
	int32 GetFeedingPatch() const { return FeedingPatch; }

	// --- Go-to buttons -----------------------------------------------------------

	/** Ok if the FOOD button may be pressed, with the patch it would go to in OutPatch, otherwise why not. */
	CrabGoto::EFoodResult CheckGoToFood(int32* OutPatch = nullptr) const;
	/** Ok if the BURROW button may be pressed, with the burrow it would go to in OutBurrow, otherwise why not. */
	CrabGoto::EBurrowResult CheckGoToBurrow(int32* OutBurrow = nullptr) const;

	/** Walk to the best food patch (rich and not too far) and feed there: the same as clicking it. False, with a message, if CheckGoToFood refuses. */
	bool GoToFood();
	/** Walk to the safest burrow the crab can reach and dig in: the same as clicking it. False, with a message, if CheckGoToBurrow refuses. */
	bool GoToBurrow();

	// --- Digging ----------------------------------------------------------------

	/** Ok if the crab could start digging a burrow right here, otherwise why not. */
	CrabDig::EResult CheckDig() const;

	/** Start digging a burrow where the crab stands. False, with a message, if CheckDig refuses. Stops a walk, dash, dance or feed. */
	bool StartDig();
	/** Give up the dig. It costs nothing. */
	void CancelDig(const TCHAR* Reason = TEXT("moved"));
	bool IsDigging() const { return bDigging; }
	/** 0 to 1 through the dig. */
	float GetDigProgress() const { return bDigging ? CrabDig::Progress(DigElapsed) : 0.f; }

	// --- Molting ----------------------------------------------------------------

	/** Ok if the crab could begin a molt right now, otherwise why not. */
	CrabMolt::EResult CheckMolt() const;

	/** Begin a molt in the burrow the crab is in. False, with a message, if CheckMolt refuses. */
	bool StartMolt();
	/** Give up the molt. It costs nothing. Leaving the burrow does this. */
	void CancelMolt(const TCHAR* Reason = TEXT("left"));
	bool IsMolting() const { return bMolting; }
	/** 0 to 1 through the molt. */
	float GetMoltProgress() const { return bMolting ? CrabMolt::Progress(MoltElapsed) : 0.f; }
	/** Molts finished this round. */
	int32 GetMolts() const { return Molts; }
	/** The size the crab is growing to, as a multiple of its starting size. Only its look changes. */
	float GetGrowthScale() const { return CrabMolt::GrowthScale(Molts); }
	/** The size it looks now, easing toward GetGrowthScale after a molt. */
	float GetShownGrowth() const { return ShownGrowth; }
	/** Soft after a flood forced it out of a molt: its grip cannot rise above half for a while. */
	bool IsSoft() const { return SoftRemaining > 0.f; }
	float GetSoftRemaining() const { return SoftRemaining; }

	// --- Peek ---------------------------------------------------------------------------

	/** 0 out of sight, 1 showing over the edge of the hole: a crab in its burrow (not molting) keeps an eye out. */
	float GetPeekBlend() const { return PeekBlend; }
	bool IsPeeking() const { return PeekBlend > 0.01f; }
	/** World Z of the highest of the peek's parts. */
	float GetPeekTop() const;

	// --- The round ------------------------------------------------------------------

	/** Won at three molts, or lost to a gull. The results panel shows and the tide, feeding, digging and walking are ignored until a new round. */
	bool IsRoundOver() const { return bRoundOver; }
	/** The round ended because a gull caught the crab, not because it is fully grown. */
	bool IsEaten() const { return bEaten; }
	/** Which round this is: 0 for the first, one more with each new round. The gull seeds itself with it. */
	int32 GetRoundIndex() const { return RoundIndex; }
	/** A gull caught the crab: the round ends, with no best time. Ignored once the round is over. */
	void EatenByGull();
	/** Seconds since the round began. Stops when it is won. */
	float GetRoundSeconds() const { return RoundSeconds; }
	int32 GetRoundDug() const { return RoundDug; }
	float GetRoundFoodEaten() const { return RoundFoodEaten; }
	/** The best winning time this session, seconds. 0 before the first win. */
	float GetBestSeconds() const { return BestSeconds; }
	bool IsNewBest() const { return bNewBest; }

	/** Reset the world and the crab: tide, food, patches, dug burrows, molts, and the crab back at the start. */
	void StartNewRound();
	/** Where a new round puts the crab, world XY. The game mode sets it. */
	void SetRoundStart(const FVector2D& XY) { RoundStartXY = XY; }

	// --- Survival -------------------------------------------------------------

	/** How fast the crab is moving over the ground now, uu/s. */
	float GetGroundSpeed() const;

	/** 1 full, 0 swept away. */
	float GetGrip() const { return Grip; }
	void SetGrip(float NewGrip);
	float GetWaterDepth() const { return WaterDepth; }
	int32 GetSweptCount() const { return SweptCount; }
	/** How many separate times the surge has taken hold of the crab. */
	int32 GetSurgeCount() const { return SurgeCount; }

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
	void UpdateForaging(float DeltaSeconds);
	void UpdateMolting(float DeltaSeconds);
	void UpdateGrowth(float DeltaSeconds);
	void UpdatePeek(float DeltaSeconds);
	void ApplyTestFood();
	void UpdateAnimation(float DeltaSeconds);
	void UpdateProceduralDance(float DeltaSeconds);
	void UpdateWorkPose(float DeltaSeconds);
	/** Stop feeding and say why on the HUD. */
	void EndFeeding(const TCHAR* Text);
	void FinishDig();
	void FinishMolt();
	void BeginSoft();
	void WinRound();
	/** Z of the shape-built visual's origin: it grows around its middle, so its feet are put back on the ground. */
	float VisualBase() const;
	void SetAnimState(ECrabAnim NewState);
	void SweepOut();
	/** Show a line on the HUD. With bReplaceCurrent false it waits its turn: it is dropped if another message is still showing. */
	void SetMessage(const FString& Text, float Seconds = 2.5f, bool bReplaceCurrent = true);
	void LogEvent(const TCHAR* Name, const FString& Detail = FString()) const;
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

	/** The stand-in that shows over the hole while the crab is in a burrow: the model itself is sunk out of sight. */
	UPROPERTY(VisibleAnywhere, Category = "Crab")
	TObjectPtr<USceneComponent> PeekRoot;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PeekShellParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PeekClawParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PeekEyeParts;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PeekPupilParts;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Clips[4];

	mutable TWeakObjectPtr<ACrabBeach> BeachCache;

	TArray<FVector> ClawBaseLocations;
	FString Message;
	FVector MoveTarget = FVector::ZeroVector;
	float WalkSpeedMultiplier = 1.f;
	FVector DashDirection = FVector::ForwardVector;
	ECrabAnim AnimState = ECrabAnim::Idle;
	bool bHasTarget = false;
	bool bDancing = false;
	bool bUseSkeletalMesh = false;
	bool bDigging = false;
	bool bMolting = false;
	bool bRoundOver = false;
	bool bNewBest = false;
	bool bEaten = false;
	int32 RoundIndex = 0;
	int32 PendingBurrow = INDEX_NONE;
	int32 PendingPatch = INDEX_NONE;
	int32 FeedingPatch = INDEX_NONE;
	int32 CurrentBurrow = INDEX_NONE;
	int32 SweptCount = 0;
	int32 SurgeCount = 0;
	int32 Molts = 0;
	int32 RoundDug = 0;
	float DashTimeRemaining = 0.f;
	float DashCooldownRemaining = 0.f;
	float StateLogTimer = 0.f;
	float Grip = 1.f;
	float Food = CrabFood::StartFood;
	float FeedGained = 0.f;
	float DigElapsed = 0.f;
	float MoltElapsed = 0.f;
	float MoltClock = 0.f;
	float MoltBlend = 0.f;
	/** Seconds the crab stays showing after a molt, before it settles out of sight. */
	float MoltAfterglow = 0.f;
	float PeekBlend = 0.f;
	float PeekClock = 0.f;
	float SoftRemaining = 0.f;
	float ShownGrowth = 1.f;
	float DisplayScale = 1.f;
	float RoundSeconds = 0.f;
	float RoundFoodEaten = 0.f;
	float BestSeconds = 0.f;
	float AppliedStartFood = -1.f;
	FVector2D RoundStartXY = FVector2D::ZeroVector;
	float FeedBlend = 0.f;
	float DigBlend = 0.f;
	float WorkClock = 0.f;
	float WaterDepth = 0.f;
	float BurrowSink = 0.f;
	float DanceClock = 0.f;
	float DanceBlend = 0.f;
	float MessageTimeRemaining = 0.f;
	float MessageDuration = 1.f;
	bool bWasInSurge = false;
};
