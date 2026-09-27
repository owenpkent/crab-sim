// SPDX-License-Identifier: Apache-2.0
#include "CrabPlayerController.h"
#include "CrabBeach.h"
#include "CrabGull.h"
#include "CrabHudMath.h"
#include "CrabPawn.h"
#include "CrabPickMath.h"

#include "CrabSim.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Math/Plane.h"

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
	if (GroundPoint)
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
	Line += FString::Printf(TEXT(" dig=%.0f,%.0f molt=%.0f,%.0f newround=%.0f,%.0f gofood=%.0f,%.0f goburrow=%.0f,%.0f"),
		DigCentre.X, DigCentre.Y, MoltCentre.X, MoltCentre.Y, NewRoundCentre.X, NewRoundCentre.Y, FoodCentre.X, FoodCentre.Y, BurrowCentre.X, BurrowCentre.Y);
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

	const FCrabPointer Pointer{Screen, View};
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && bHaveMouse)
	{
		HandleLeftPress(*Crab, Screen, View, bHavePoint ? &Point : nullptr);
	}
	else if (bHavePoint)
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

	if (bHavePoint && WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		Crab->TryDash(Point);
	}
}
