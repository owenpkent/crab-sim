// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CrabOrbitMath.h"
#include "CrabStickMath.h"
#include "CrabPlayerController.generated.h"

class ACrabPawn;

/** Where the pointer is on screen: a pixel, in a view of this size. */
struct FCrabPointer
{
	FVector2D Screen = FVector2D::ZeroVector;
	FVector2D View = FVector2D::ZeroVector;
};

/**
 * Pointer-first control. Nothing needs a key, the wheel or a timing window.
 *
 * - Click the ground: walk there and stop.
 * - Hold the left button: follow the cursor.
 * - Click a burrow: walk to it and dig in. Click away from it to come out.
 * - Click a food patch: walk to it and sift it until it is bare, the crab is full or it moves on.
 *   A burrow or patch is a big target on screen at any camera range: an ellipse at least 90 by 60 px
 *   (CrabPick), on top of its world radius. Priority: burrow, then the crab, then patch, then ground.
 * - Click the HUD's FOOD button: walk to the best food patch and feed. BURROW: walk to the
 *   safest burrow and dig in. Same as clicking them, but they need no aim and work when the target is off screen.
 * - Click the HUD's dig button: dig a new burrow where the crab stands. Any walk cancels it.
 * - Click the HUD's molt button, in a burrow: molt. Leaving the burrow cancels it.
 * - Click the new round button on the results panel: start again.
 * - Click the crab: start or stop its dance.
 * - Right click: dash toward the cursor, when the button comes up.
 * - Hold the right button and drag: orbit the camera freely round the crab, sideways all the way round, up and
 *   down between low over the sand and near straight down. Past a few pixels of travel the press is an orbit,
 *   not a click, and does not dash (CrabOrbitMath.h).
 *
 * With CrabSim.OneStick 1, every step above is replaced except the buttons and the right button: the left
 * stick and left click instead drive a MENU/STEER scheme for a player with no other input. See GAME.md,
 * "One-stick mode", and CrabStickMath.h for the pure rules.
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
	 * it without a window. With the pointer's pixel, burrows and patches are also picked by their
	 * on-screen zones; without it, by their world radius alone.
	 */
	void HandleClick(ACrabPawn& Crab, const FVector& Point, const FCrabPointer* Pointer = nullptr);

	/** What holding the left button with the cursor at the point does, each frame. */
	void HandleHold(ACrabPawn& Crab, const FVector& Point, const FCrabPointer* Pointer = nullptr);

	/**
	 * A fresh left press at a screen pixel. On the FOOD, BURROW, dig or molt button it does that and swallows
	 * the rest of the press, so a button is never also a walk. With the results panel up, only the new round
	 * button does anything. Anywhere else it is a click on the ground at GroundPoint, when there is one (null
	 * when the cursor points at the sky).
	 */
	void HandleLeftPress(ACrabPawn& Crab, const FVector2D& ScreenPos, const FVector2D& ViewSize, const FVector* GroundPoint);

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

	/** A click this close to a burrow is a click on the burrow, uu. On screen the zone is never smaller than CrabPick's minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Input")
	float BurrowClickRadius = 100.f;

	/** How far ahead of the crab STEER aims its walk target, uu. Past ArrivalRadius, so it never arrives and stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|OneStick")
	float OneStickSteerAheadDistance = 400.f;

	/** How far ahead of the crab the MENU's DASH item aims TryDash, uu. Only the direction matters to TryDash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|OneStick")
	float OneStickDashAheadDistance = 300.f;

	/**
	 * Where a world point is on screen. Empty means the player's own camera. A test world has no viewport, so
	 * tests put a camera of their own here.
	 */
	TFunction<bool(const FVector&, FVector2D&)> ProjectToView;

	// --- One-stick mode, for the HUD -------------------------------------------------------------------

	CrabStick::EMode GetOneStickMode() const { return MenuState.GetMode(); }
	CrabStick::EMenuItem GetOneStickCursor() const { return MenuState.GetCursor(); }
	/** Taps in the open chain, 0 to 2, for the HUD's pips. */
	int32 GetOneStickChainCount() const { return MenuState.GetChainCount(); }

private:
	/** With CrabSim.StateLog on, logs where the crab and each burrow are on screen, so live tests can click them. */
	void LogScreenPositions(float DeltaTime);

	float ScreenLogTimer = 0.f;
	bool bSwallowHold = false;

	bool ProjectToPixel(const FVector& World, FVector2D& OutPixel) const;

	/**
	 * How deep in a target's click zone the pointer is: 1 or less is inside, more is outside. The zone is the
	 * circle of WorldRadius round the target on the ground, and, when the pointer's pixel is known and not on a
	 * HUD button, the ellipse it makes on screen at least as big as CrabPick's minimum. Either one counts.
	 */
	float ZoneScore(const FVector& Target, float WorldRadius, const FVector& Point, const FCrabPointer* Pointer) const;

	void ResolveTarget(const ACrabPawn& Crab, const FVector& Point, const FCrabPointer* Pointer, FVector& OutTarget, int32& OutBurrow, int32& OutPatch) const;

	// --- One-stick mode ---------------------------------------------------------------------------------

	/** Flips CrabSim.OneStick and logs onestick_on/off. GameUserSettings.ini is kept in step by a changed callback set up in BeginPlay. */
	void ToggleOneStick();

	/** Reads the stick and the click, steps the detectors and the menu state machine, dispatches a commit, drives STEER, and logs. */
	void UpdateOneStick(ACrabPawn& Crab, float DeltaTime);

	/** What committing an item does: the same as its HUD button, DANCE toggles, DASH dashes ahead. MOVE does nothing here: the menu state machine already entered STEER. */
	void DispatchOneStickSelect(ACrabPawn& Crab, CrabStick::EMenuItem Item);

	/** True if the pixel is on any button a mouse click must not also register as a one-stick tap for. */
	bool IsOverOneStickHudControl(const FVector2D& ViewSize, const FVector2D& ScreenPos, bool bRoundOver) const;

	/** A CRABSIM_EVENT line, with CrabSim.StateLog on: one-stick events, and orbit_start and orbit_end. */
	void LogInputEvent(const TCHAR* Name, const FString& Detail = FString()) const;

	/** The right button: a click dashes, a drag orbits the camera. */
	void UpdateRightButton(ACrabPawn& Crab, bool bHaveMouse, const FVector2D& Screen, bool bHavePoint, const FVector& Point);

	CrabOrbit::FDrag OrbitDrag;

	CrabStick::FStickTapDetector StickTapDetector;
	CrabStick::FClickTapDetector ClickTapDetector;
	CrabStick::FMenuState MenuState;
};
