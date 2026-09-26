// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CrabPlayerController.generated.h"

class ACrabPawn;

/**
 * Pointer-first control. Nothing needs a key, the wheel or a timing window.
 *
 * - Click the ground: walk there and stop.
 * - Hold the left button: follow the cursor.
 * - Click a burrow: walk to it and dig in. Click away from it to come out.
 * - Click the crab: start or stop its dance.
 * - Right click: dash toward the cursor.
 */
UCLASS()
class CRABSIM_API ACrabPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACrabPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** The cursor projected onto the flat ground at the crab's feet. False when there is no cursor or it points at the sky. */
	bool GetCursorGroundPoint(FVector& OutPoint) const;

	/**
	 * What a fresh left click on the ground point does. Public so tests can drive
	 * it without a window.
	 */
	void HandleClick(ACrabPawn& Crab, const FVector& Point);

	/** What holding the left button with the cursor at the point does, each frame. */
	void HandleHold(ACrabPawn& Crab, const FVector& Point);

	/**
	 * The left button came up. A click on the crab toggles its dance and then swallows the rest of
	 * that press, so a long press or a wobbling hand does not turn into a walk. This ends the swallow.
	 */
	void NotifyLeftButtonReleased() { bSwallowHold = false; }

	/** Farthest a click can send the crab from where it stands, uu. Keeps a click near the horizon sane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Input")
	float MaxClickDistance = 3000.f;

	/** A click this close to the crab is a click on the crab, uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Input")
	float DanceClickRadius = 80.f;

	/** A click this close to a burrow is a click on the burrow, uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Input")
	float BurrowClickRadius = 100.f;

private:
	/** With CrabSim.StateLog on, logs where the crab and each burrow are on screen, so live tests can click them. */
	void LogScreenPositions(float DeltaTime);

	float ScreenLogTimer = 0.f;
	bool bSwallowHold = false;

	void ResolveTarget(const ACrabPawn& Crab, const FVector& Point, FVector& OutTarget, int32& OutBurrow) const;
};
