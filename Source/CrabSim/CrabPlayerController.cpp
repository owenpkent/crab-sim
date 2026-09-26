// SPDX-License-Identifier: Apache-2.0
#include "CrabPlayerController.h"
#include "CrabBeach.h"
#include "CrabPawn.h"

#include "CrabSim.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
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

void ACrabPlayerController::ResolveTarget(const ACrabPawn& Crab, const FVector& Point, FVector& OutTarget, int32& OutBurrow) const
{
	OutTarget = Point;
	OutBurrow = INDEX_NONE;
	if (const ACrabBeach* Beach = Crab.GetBeach())
	{
		const int32 Burrow = Beach->FindBurrowNear(Point, BurrowClickRadius);
		if (Burrow != INDEX_NONE)
		{
			OutBurrow = Burrow;
			OutTarget = Beach->GetBurrows()[Burrow].Location;
		}
	}
}

void ACrabPlayerController::HandleClick(ACrabPawn& Crab, const FVector& Point)
{
	const ACrabBeach* Beach = Crab.GetBeach();

	if (Crab.IsInBurrow())
	{
		// Clicking the hole you are in keeps you in it. Anywhere else, you come out and head there.
		const int32 Hole = Crab.GetCurrentBurrow();
		if (Beach && Beach->GetBurrows().IsValidIndex(Hole) && FVector::Dist2D(Point, Beach->GetBurrows()[Hole].Location) <= BurrowClickRadius)
		{
			return;
		}
		Crab.ExitBurrow();
	}
	else
	{
		FVector Unused;
		int32 Burrow = INDEX_NONE;
		ResolveTarget(Crab, Point, Unused, Burrow);
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
	ResolveTarget(Crab, Point, Target, Burrow);
	Crab.SetMoveTarget(Target, Burrow);
}

void ACrabPlayerController::HandleHold(ACrabPawn& Crab, const FVector& Point)
{
	if (Crab.IsInBurrow() || bSwallowHold)
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
	ResolveTarget(Crab, Point, Target, Burrow);
	Crab.SetMoveTarget(Target, Burrow);
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

	if (bHavePoint)
	{
		if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			bSwallowHold = false;
			HandleClick(*Crab, Point);
		}
		else if (IsInputKeyDown(EKeys::LeftMouseButton))
		{
			HandleHold(*Crab, Point);
		}

		if (WasInputKeyJustPressed(EKeys::RightMouseButton))
		{
			Crab->TryDash(Point);
		}
	}
}
