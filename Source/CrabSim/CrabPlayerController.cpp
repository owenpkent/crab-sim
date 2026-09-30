// SPDX-License-Identifier: Apache-2.0
#include "CrabPlayerController.h"
#include "CrabBeach.h"
#include "CrabGull.h"
#include "CrabHudMath.h"
#include "CrabPawn.h"
#include "CrabPickMath.h"

#include "CrabSim.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Math/Plane.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* OneStickIniSection = TEXT("CrabSim");
	const TCHAR* OneStickIniKey = TEXT("OneStick");

	/**
	 * Automated runs (test.sh, live-test.sh, record.sh all pass -unattended) neither read nor write the saved
	 * choice: a live test turning one-stick on must not leak into the headless suites or the player's own game.
	 */
	bool UsesSavedOneStickSetting()
	{
		return GConfig && !FApp::IsUnattended();
	}

	/** Keeps GameUserSettings.ini in step with the CVar, however it was changed: console, F2, or the HUD button. */
	void SaveOneStickSetting(IConsoleVariable* Var)
	{
		if (UsesSavedOneStickSetting() && Var)
		{
			GConfig->SetBool(OneStickIniSection, OneStickIniKey, Var->GetInt() != 0, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
}

static TAutoConsoleVariable<int32> CVarOneStick(
	TEXT("CrabSim.OneStick"), 0,
	TEXT("1 drives the crab from one analog stick (or a lone left click) through a MENU/STEER scheme, for a player with no other input. See GAME.md, \"One-stick mode\"."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStickTapOuter(
	TEXT("CrabSim.StickTapOuter"), CrabStick::FTuning().Outer,
	TEXT("Stick magnitude that arms a one-stick tap. Every one-stick threshold is a CVar, for a player with tremor or limited stick travel."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStickTapInner(
	TEXT("CrabSim.StickTapInner"), CrabStick::FTuning().Inner,
	TEXT("Stick magnitude a one-stick tap must fall back under to complete, below StickTapOuter so a tremor wobbling round one threshold cannot fire a tap."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStickTapMax(
	TEXT("CrabSim.StickTapMax"), CrabStick::FTuning().MaxTapSeconds,
	TEXT("Longest a one-stick reach may take, Outer to under Inner, and still count as a tap, seconds. Held longer it is not a tap: only steering, or nothing in the menu."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStickTapGap(
	TEXT("CrabSim.StickTapGap"), CrabStick::FTuning().GapSeconds,
	TEXT("One-stick taps this close together, end to start, chain together. The chain closes, committing a pending select, when this passes with no new tap."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarOrbitStartPixels(
	TEXT("CrabSim.OrbitStartPixels"), CrabOrbit::FTuning().StartPixels,
	TEXT("How far the pointer must move with the right button down, px, before the press orbits the camera instead of dashing."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarOrbitDegreesPerPixel(
	TEXT("CrabSim.OrbitDegreesPerPixel"), CrabOrbit::FTuning().DegreesPerPixel,
	TEXT("Camera yaw per pixel of sideways pointer travel while orbiting, degrees. Negative turns the other way."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarOrbitPitchDegreesPerPixel(
	TEXT("CrabSim.OrbitPitchDegreesPerPixel"), CrabOrbit::FTuning().PitchDegreesPerPixel,
	TEXT("Camera pitch per pixel of up and down pointer travel while orbiting, degrees. Pointer up lowers the camera. Negative swaps up and down."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarStickMinSpeed(
	TEXT("CrabSim.StickMinSpeed"), CrabStick::FTuning().MinSpeed,
	TEXT("One-stick STEER's walk speed multiplier at Inner deflection, ramping up to 1 at full deflection (CrabStick::SteerSpeedMultiplier)."),
	ECVF_Default);

ACrabPlayerController::ACrabPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ACrabPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Keep the cursor visible and free while a button is held. The default game
	// input mode hides it and captures it, which is right for a shooter and
	// wrong for a game played entirely by pointing.
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
	SetInputMode(Mode);

	// Read the saved one-stick choice once at startup, so a stick-only player does not need to turn it
	// on again every launch, then keep the ini in step with the CVar from here on, however it changes.
	static bool bOneStickPersistenceReady = false;
	if (!bOneStickPersistenceReady)
	{
		bOneStickPersistenceReady = true;
		IConsoleVariable* CVar = CVarOneStick.AsVariable();
		bool bSaved = false;
		if (UsesSavedOneStickSetting() && GConfig->GetBool(OneStickIniSection, OneStickIniKey, bSaved, GGameUserSettingsIni))
		{
			CVar->Set(bSaved ? 1 : 0, ECVF_SetByCode);
		}
		CVar->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&SaveOneStickSetting));
	}
}

bool ACrabPlayerController::GetCursorGroundPoint(FVector& OutPoint) const
{
	const ACrabPawn* Crab = Cast<ACrabPawn>(GetPawn());
	if (!Crab)
	{
		return false;
	}

	FVector Origin;
	FVector Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction) || Direction.Z > -KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Hit = FMath::RayPlaneIntersection(Origin, Direction, FPlane(FVector(0.f, 0.f, Crab->GetFeetZ()), FVector::UpVector));

	FVector Offset = Hit - Crab->GetActorLocation();
	Offset.Z = 0.f;
	if (Offset.Size() > MaxClickDistance)
	{
		Offset = Offset.GetSafeNormal() * MaxClickDistance;
	}
	OutPoint = FVector(Crab->GetActorLocation().X + Offset.X, Crab->GetActorLocation().Y + Offset.Y, Crab->GetFeetZ());
	return true;
}

bool ACrabPlayerController::ProjectToPixel(const FVector& World, FVector2D& OutPixel) const
{
	if (ProjectToView)
	{
		return ProjectToView(World, OutPixel);
	}
	return ProjectWorldLocationToScreen(World, OutPixel, false);
}

float ACrabPlayerController::ZoneScore(const FVector& Target, float WorldRadius, const FVector& Point, const FCrabPointer* Pointer) const
{
	float Score = FVector::Dist2D(Point, Target) / FMath::Max(WorldRadius, 1.f);
	if (Pointer && !CrabHud::HitsAnyButton(Pointer->View.X, Pointer->View.Y, Pointer->Screen))
	{
		float OnScreen = 0.f;
		const auto Project = [this](const FVector& World, FVector2D& Pixel) { return ProjectToPixel(World, Pixel); };
		if (CrabPick::ZoneScore(Project, Target, WorldRadius, CrabHud::ScaleForHeight(Pointer->View.Y), Pointer->Screen, OnScreen))
		{
			Score = FMath::Min(Score, OnScreen);
		}
	}
	return Score;
}

void ACrabPlayerController::ResolveTarget(const ACrabPawn& Crab, const FVector& Point, const FCrabPointer* Pointer, FVector& OutTarget, int32& OutBurrow, int32& OutPatch) const
{
	OutTarget = Point;
	OutBurrow = INDEX_NONE;
	OutPatch = INDEX_NONE;
	if (const ACrabBeach* Beach = Crab.GetBeach())
	{
		// The deepest zone the point is in wins. Burrows come first, so a burrow beats a patch under it.
		float BestBurrow = 1.f;
		for (int32 Index = 0; Index < Beach->GetBurrows().Num(); ++Index)
		{
			const float Score = ZoneScore(Beach->GetBurrows()[Index].Location, BurrowClickRadius, Point, Pointer);
			if (Score <= BestBurrow)
			{
				BestBurrow = Score;
				OutBurrow = Index;
			}
		}
		if (OutBurrow != INDEX_NONE)
		{
			OutTarget = Beach->GetBurrows()[OutBurrow].Location;
			return;
		}
		float BestPatch = BIG_NUMBER;
		for (int32 Index = 0; Index < Beach->GetFoodPatches().Num(); ++Index)
		{
			const FCrabFoodPatch& Patch = Beach->GetFoodPatches()[Index];
			const float Score = ZoneScore(Patch.Location, Patch.ClickRadius, Point, Pointer);
			if (Score <= 1.f && Score < BestPatch)
			{
				BestPatch = Score;
				OutPatch = Index;
			}
		}
		if (OutPatch != INDEX_NONE)
		{
			OutTarget = Beach->GetFoodPatches()[OutPatch].Location;
		}
	}
}

void ACrabPlayerController::HandleClick(ACrabPawn& Crab, const FVector& Point, const FCrabPointer* Pointer)
{
	// The results panel is up: the world is held still until the button starts a new round.
	if (Crab.IsRoundOver())
	{
		return;
	}
	const ACrabBeach* Beach = Crab.GetBeach();

	if (Crab.IsInBurrow())
	{
		// Clicking the hole you are in keeps you in it. Anywhere else, you come out and head there.
		const int32 Hole = Crab.GetCurrentBurrow();
		if (Beach && Beach->GetBurrows().IsValidIndex(Hole) && ZoneScore(Beach->GetBurrows()[Hole].Location, BurrowClickRadius, Point, Pointer) <= 1.f)
		{
			return;
		}
		Crab.ExitBurrow();
	}
	else
	{
		FVector Unused;
		int32 Burrow = INDEX_NONE;
		int32 Patch = INDEX_NONE;
		ResolveTarget(Crab, Point, Pointer, Unused, Burrow, Patch);
		// The crab itself beats the patch it stands on: a click on the crab is a dance, not a feed.
		if (Burrow == INDEX_NONE && FVector::Dist2D(Point, Crab.GetActorLocation()) <= DanceClickRadius)
		{
			Crab.ToggleDance();
			bSwallowHold = true;
			return;
		}
	}

	Crab.StopDance();
	FVector Target;
	int32 Burrow = INDEX_NONE;
	int32 Patch = INDEX_NONE;
	ResolveTarget(Crab, Point, Pointer, Target, Burrow, Patch);
	Crab.SetMoveTarget(Target, Burrow, Patch);
}

void ACrabPlayerController::HandleLeftPress(ACrabPawn& Crab, const FVector2D& ScreenPos, const FVector2D& ViewSize, const FVector* GroundPoint)
{
	// Works in every state (round over or not, one-stick on or off): it is how a stick-only player turns
	// this on in the first place, and clicking it must never also do anything else.
	if (CrabHud::HitsOneStickButton(ViewSize.X, ViewSize.Y, ScreenPos))
	{
		ToggleOneStick();
		bSwallowHold = true;
		return;
	}
	if (Crab.IsRoundOver())
	{
		if (CrabHud::HitsNewRoundButton(ViewSize.X, ViewSize.Y, ScreenPos))
		{
			Crab.StartNewRound();
		}
		bSwallowHold = true;
		return;
	}
	if (CrabHud::HitsMoltButton(ViewSize.X, ViewSize.Y, ScreenPos))
	{
		Crab.StartMolt();
		bSwallowHold = true;
		return;
	}
	if (CrabHud::HitsDigButton(ViewSize.X, ViewSize.Y, ScreenPos))
	{
		Crab.StartDig();
		bSwallowHold = true;
		return;
	}
	if (CrabHud::HitsFoodButton(ViewSize.X, ViewSize.Y, ScreenPos))
	{
		Crab.GoToFood();
		bSwallowHold = true;
		return;
	}
	if (CrabHud::HitsBurrowButton(ViewSize.X, ViewSize.Y, ScreenPos))
	{
		Crab.GoToBurrow();
		bSwallowHold = true;
		return;
	}
	// A one-stick left click is a tap, fed to UpdateOneStick every frame: it never also walks the crab.
	if (GroundPoint && CVarOneStick.GetValueOnGameThread() == 0)
	{
		bSwallowHold = false;
		const FCrabPointer Pointer{ScreenPos, ViewSize};
		HandleClick(Crab, *GroundPoint, &Pointer);
	}
}

void ACrabPlayerController::HandleHold(ACrabPawn& Crab, const FVector& Point, const FCrabPointer* Pointer)
{
	if (Crab.IsInBurrow() || Crab.IsRoundOver() || bSwallowHold)
	{
		return;
	}
	if (Crab.IsDancing())
	{
		// Holding on the crab keeps the dance going. Dragging away from it ends the dance and follows.
		if (FVector::Dist2D(Point, Crab.GetActorLocation()) <= DanceClickRadius)
		{
			return;
		}
		Crab.StopDance();
	}

	FVector Target;
	int32 Burrow = INDEX_NONE;
	int32 Patch = INDEX_NONE;
	ResolveTarget(Crab, Point, Pointer, Target, Burrow, Patch);
	Crab.SetMoveTarget(Target, Burrow, Patch);
}

void ACrabPlayerController::LogScreenPositions(float DeltaTime)
{
	ScreenLogTimer += DeltaTime;
	if (ScreenLogTimer < 0.5f)
	{
		return;
	}
	ScreenLogTimer = 0.f;

	const IConsoleVariable* StateLog = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.StateLog"));
	const ACrabPawn* Crab = Cast<ACrabPawn>(GetPawn());
	if (!StateLog || StateLog->GetInt() == 0 || !Crab)
	{
		return;
	}

	int32 ViewX = 0;
	int32 ViewY = 0;
	GetViewportSize(ViewX, ViewY);

	auto Pixels = [this](const FVector& World) -> FString
	{
		FVector2D Screen;
		if (ProjectWorldLocationToScreen(World, Screen, false))
		{
			return FString::Printf(TEXT("%.0f,%.0f"), Screen.X, Screen.Y);
		}
		return FString(TEXT("-1,-1"));
	};

	FString Line = FString::Printf(TEXT("CRABSIM_SCREEN t=%.2f view=%dx%d crab=%s"), GetWorld()->GetTimeSeconds(), ViewX, ViewY, *Pixels(Crab->GetActorLocation()));
	if (const ACrabBeach* Beach = Crab->GetBeach())
	{
		for (int32 Index = 0; Index < Beach->GetBurrows().Num(); ++Index)
		{
			Line += FString::Printf(TEXT(" burrow%d=%s"), Index, *Pixels(Beach->GetBurrows()[Index].Location));
		}
		for (int32 Index = 0; Index < Beach->GetFoodPatches().Num(); ++Index)
		{
			Line += FString::Printf(TEXT(" patch%d=%s"), Index, *Pixels(Beach->GetFoodPatches()[Index].Location));
		}
	}
	const FVector2D DigCentre = CrabHud::DigButtonRect(ViewX, ViewY).GetCenter();
	const FVector2D MoltCentre = CrabHud::MoltButtonRect(ViewX, ViewY).GetCenter();
	const FVector2D NewRoundCentre = CrabHud::NewRoundButtonRect(ViewX, ViewY).GetCenter();
	const FVector2D FoodCentre = CrabHud::FoodButtonRect(ViewX, ViewY).GetCenter();
	const FVector2D BurrowCentre = CrabHud::BurrowButtonRect(ViewX, ViewY).GetCenter();
	const FVector2D OneStickCentre = CrabHud::OneStickButtonRect(ViewX, ViewY).GetCenter();
	Line += FString::Printf(TEXT(" dig=%.0f,%.0f molt=%.0f,%.0f newround=%.0f,%.0f gofood=%.0f,%.0f goburrow=%.0f,%.0f onestick=%.0f,%.0f"),
		DigCentre.X, DigCentre.Y, MoltCentre.X, MoltCentre.Y, NewRoundCentre.X, NewRoundCentre.Y, FoodCentre.X, FoodCentre.Y, BurrowCentre.X, BurrowCentre.Y,
		OneStickCentre.X, OneStickCentre.Y);
	// Where a gull that is coming is on screen (its body), when it is in front of the camera.
	TActorIterator<ACrabGull> GullIt(GetWorld());
	FVector2D GullPixel;
	if (GullIt && GullIt->IsWarned() && ProjectWorldLocationToScreen(GullIt->GetActorLocation() + FVector(0.f, 0.f, 60.f), GullPixel, false))
	{
		Line += FString::Printf(TEXT(" gull=%.0f,%.0f"), GullPixel.X, GullPixel.Y);
	}
	UE_LOG(LogCrabSim, Log, TEXT("%s"), *Line);
}

void ACrabPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	LogScreenPositions(DeltaTime);

	ACrabPawn* Crab = Cast<ACrabPawn>(GetPawn());
	if (!Crab)
	{
		return;
	}

	if (WasInputKeyJustReleased(EKeys::LeftMouseButton) || !IsInputKeyDown(EKeys::LeftMouseButton))
	{
		NotifyLeftButtonReleased();
	}

	FVector Point;
	const bool bHavePoint = GetCursorGroundPoint(Point);

	float MouseX = 0.f;
	float MouseY = 0.f;
	int32 ViewX = 0;
	int32 ViewY = 0;
	const bool bHaveMouse = GetMousePosition(MouseX, MouseY);
	GetViewportSize(ViewX, ViewY);
	const FVector2D Screen(MouseX, MouseY);
	const FVector2D View(ViewX, ViewY);

	const bool bOneStick = CVarOneStick.GetValueOnGameThread() != 0;

	const FCrabPointer Pointer{Screen, View};
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && bHaveMouse)
	{
		HandleLeftPress(*Crab, Screen, View, bHavePoint ? &Point : nullptr);
	}
	else if (bHavePoint && !bOneStick)
	{
		// The cursor over a HUD button is over the button, not over the ground behind it.
		if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			bSwallowHold = false;
			HandleClick(*Crab, Point);
		}
		else if (IsInputKeyDown(EKeys::LeftMouseButton) && !(bHaveMouse && CrabHud::HitsAnyButton(View.X, View.Y, Screen)))
		{
			HandleHold(*Crab, Point, bHaveMouse ? &Pointer : nullptr);
		}
	}

	UpdateRightButton(*Crab, bHaveMouse, Screen, bHavePoint, Point);

	// F2 is a single key, for a player who types on an on-screen keyboard that sends one key at a time.
	if (WasInputKeyJustPressed(EKeys::F2))
	{
		ToggleOneStick();
	}
	if (bOneStick)
	{
		UpdateOneStick(*Crab, DeltaTime);
	}
}

void ACrabPlayerController::UpdateRightButton(ACrabPawn& Crab, bool bHaveMouse, const FVector2D& Screen, bool bHavePoint, const FVector& Point)
{
	CrabOrbit::FTuning Tuning;
	Tuning.StartPixels = CVarOrbitStartPixels.GetValueOnGameThread();
	Tuning.DegreesPerPixel = CVarOrbitDegreesPerPixel.GetValueOnGameThread();
	Tuning.PitchDegreesPerPixel = CVarOrbitPitchDegreesPerPixel.GetValueOnGameThread();

	// A press and release inside one frame still counts as a press: it comes up, and dashes, the next frame.
	const bool bDown = IsInputKeyDown(EKeys::RightMouseButton) || WasInputKeyJustPressed(EKeys::RightMouseButton);
	const bool bWasOrbiting = OrbitDrag.IsOrbiting();
	const CrabOrbit::FStep Step = OrbitDrag.Update(bDown, bHaveMouse ? Screen : OrbitDrag.GetLastPointer(), Tuning);
	if (Step.YawDelta != 0.f)
	{
		Crab.AddCameraYaw(Step.YawDelta);
	}
	if (Step.PitchDelta != 0.f)
	{
		Crab.AddCameraPitch(Step.PitchDelta);
	}
	if (OrbitDrag.IsOrbiting() != bWasOrbiting)
	{
		LogInputEvent(bWasOrbiting ? TEXT("orbit_end") : TEXT("orbit_start"), FString::Printf(TEXT("cam=%.1f pitch=%.1f"), Crab.GetCameraYaw(), Crab.GetCameraPitch()));
	}
	if (Step.bClick && bHavePoint)
	{
		Crab.TryDash(Point);
	}
}

// --- One-stick mode -------------------------------------------------------------------------------------

void ACrabPlayerController::ToggleOneStick()
{
	IConsoleVariable* CVar = CVarOneStick.AsVariable();
	const bool bNewValue = CVar->GetInt() == 0;
	CVar->Set(bNewValue ? 1 : 0, ECVF_SetByCode);
	LogInputEvent(bNewValue ? TEXT("onestick_on") : TEXT("onestick_off"));
	// Whichever way it was flipped, and however STEER was left, walking speed goes back to normal.
	if (ACrabPawn* Crab = Cast<ACrabPawn>(GetPawn()))
	{
		Crab->SetWalkSpeedMultiplier(1.f);
	}
}

bool ACrabPlayerController::IsOverOneStickHudControl(const FVector2D& ViewSize, const FVector2D& ScreenPos, bool bRoundOver) const
{
	if (CrabHud::HitsOneStickButton(ViewSize.X, ViewSize.Y, ScreenPos))
	{
		return true;
	}
	// The new round button only lives on the results panel, and every other button only while it is up.
	return bRoundOver ? CrabHud::HitsNewRoundButton(ViewSize.X, ViewSize.Y, ScreenPos) : CrabHud::HitsAnyButton(ViewSize.X, ViewSize.Y, ScreenPos);
}

void ACrabPlayerController::DispatchOneStickSelect(ACrabPawn& Crab, CrabStick::EMenuItem Item)
{
	switch (Item)
	{
	case CrabStick::EMenuItem::Move:
		// FMenuState has already entered STEER: nothing more to do here.
		break;
	case CrabStick::EMenuItem::Food:
		Crab.GoToFood();
		break;
	case CrabStick::EMenuItem::Burrow:
		Crab.GoToBurrow();
		break;
	case CrabStick::EMenuItem::Dig:
		Crab.StartDig();
		break;
	case CrabStick::EMenuItem::Molt:
		Crab.StartMolt();
		break;
	case CrabStick::EMenuItem::Dance:
		Crab.ToggleDance();
		break;
	case CrabStick::EMenuItem::Dash:
		{
			// The same TryDash a right click uses, aimed at a point ahead of the crab in its facing direction.
			const FVector Forward = Crab.GetActorRotation().Vector();
			Crab.TryDash(Crab.GetActorLocation() + Forward * OneStickDashAheadDistance);
		}
		break;
	case CrabStick::EMenuItem::NewRound:
		// The same as the results panel's own button: only reachable once the round is actually over.
		Crab.StartNewRound();
		break;
	default:
		break;
	}
}

void ACrabPlayerController::LogInputEvent(const TCHAR* Name, const FString& Detail) const
{
	const IConsoleVariable* StateLog = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.StateLog"));
	if (!StateLog || StateLog->GetInt() == 0)
	{
		return;
	}
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_EVENT %s t=%.2f%s%s"), Name, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
		Detail.IsEmpty() ? TEXT("") : TEXT(" "), *Detail);
}

void ACrabPlayerController::UpdateOneStick(ACrabPawn& Crab, float DeltaTime)
{
	CrabStick::FTuning Tuning;
	Tuning.Outer = CVarStickTapOuter.GetValueOnGameThread();
	Tuning.Inner = CVarStickTapInner.GetValueOnGameThread();
	Tuning.MaxTapSeconds = CVarStickTapMax.GetValueOnGameThread();
	Tuning.GapSeconds = CVarStickTapGap.GetValueOnGameThread();
	Tuning.MinSpeed = CVarStickMinSpeed.GetValueOnGameThread();
	StickTapDetector.Tuning = Tuning;
	ClickTapDetector.Tuning = Tuning;
	MenuState.Tuning = Tuning;

	float StickX = 0.f;
	float StickY = 0.f;
	GetInputAnalogStickState(EControllerAnalogStick::CAS_LeftStick, StickX, StickY);
	const FVector2D Stick(StickX, StickY);

	float MouseX = 0.f;
	float MouseY = 0.f;
	int32 ViewX = 0;
	int32 ViewY = 0;
	const bool bHaveMouse = GetMousePosition(MouseX, MouseY);
	GetViewportSize(ViewX, ViewY);
	const bool bOverHudControl = bHaveMouse && IsOverOneStickHudControl(FVector2D(ViewX, ViewY), FVector2D(MouseX, MouseY), Crab.IsRoundOver());
	// A click on a HUD button is the button's, same as when one-stick is off: it is never also a tap.
	const bool bClickDown = IsInputKeyDown(EKeys::LeftMouseButton) && !bOverHudControl;

	TArray<CrabStick::FTap> Taps;
	StickTapDetector.Update(DeltaTime, Stick, Taps);
	ClickTapDetector.Update(DeltaTime, bClickDown, Taps);
	for (const CrabStick::FTap& Tap : Taps)
	{
		LogInputEvent(TEXT("tap"), FString::Printf(TEXT("dir=%s"), CrabStick::TapDirText(Tap)));
	}

	const CrabStick::EMode PreviousMode = MenuState.GetMode();
	const CrabStick::EMenuItem PreviousCursor = MenuState.GetCursor();
	const CrabStick::FStepResult Result = MenuState.Update(DeltaTime, Taps, Crab.IsRoundOver());

	if (Result.bCursorMoved && MenuState.GetCursor() != PreviousCursor)
	{
		LogInputEvent(TEXT("cursor"), FString::Printf(TEXT("cursor=%s"), CrabStick::ItemLabel(MenuState.GetCursor())));
	}
	if (Result.bSelected)
	{
		LogInputEvent(TEXT("select"), FString::Printf(TEXT("select=%s"), CrabStick::ItemLabel(Result.SelectedItem)));
		DispatchOneStickSelect(Crab, Result.SelectedItem);
	}
	if (Result.bTripleTapped)
	{
		LogInputEvent(TEXT("toggle"));
	}
	if (Result.bModeChanged && MenuState.GetMode() != PreviousMode)
	{
		LogInputEvent(TEXT("mode"), FString::Printf(TEXT("mode=%s"), CrabStick::ModeLabel(MenuState.GetMode())));
		if (MenuState.GetMode() == CrabStick::EMode::Menu)
		{
			// Leaving STEER stops the crab: there is no cursor-restore equivalent for a walk under way.
			Crab.ClearMoveTarget();
		}
	}

	if (MenuState.GetMode() == CrabStick::EMode::Steer)
	{
		// Screen-relative: stick up drives the camera's forward and stick right its right, whatever way the
		// camera has been orbited (at yaw 0, world +X and +Y), so up on the stick is up on screen.
		const float Magnitude = Stick.Size();
		FVector2D Direction = CrabOrbit::ScreenToWorld(Stick, Crab.GetCameraYaw());
		if (Magnitude >= Tuning.Inner && Direction.Normalize())
		{
			const FVector Target = Crab.GetActorLocation() + FVector(Direction.X, Direction.Y, 0.f) * OneStickSteerAheadDistance;
			Crab.SetMoveTarget(Target);
			// SetMoveTarget just reset this to 1: put back how far the stick is actually pushed.
			Crab.SetWalkSpeedMultiplier(CrabStick::SteerSpeedMultiplier(Magnitude, Tuning));
		}
		else
		{
			Crab.ClearMoveTarget();
		}
	}
}
