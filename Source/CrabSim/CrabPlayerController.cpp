// SPDX-License-Identifier: Apache-2.0
#include "CrabPlayerController.h"
#include "CrabPawn.h"

#include "Engine/GameViewportClient.h"
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

void ACrabPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	ACrabPawn* Crab = Cast<ACrabPawn>(GetPawn());
	if (!Crab)
	{
		return;
	}

	FVector Point;
	if (IsInputKeyDown(EKeys::LeftMouseButton) && GetCursorGroundPoint(Point))
	{
		Crab->SetMoveTarget(Point);
	}

	if (WasInputKeyJustPressed(EKeys::RightMouseButton) && GetCursorGroundPoint(Point))
	{
		Crab->TryDash(Point);
	}
}
