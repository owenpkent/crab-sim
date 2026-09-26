// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabPlayerController.h"

using namespace UE::CrabSim::Tests;

namespace
{
	/**
	 * A beach, a crab, and the pointer controller. The controller does not possess
	 * the crab: its click and hold handlers only need the crab and the beach, so
	 * tests can drive them directly without a window or a cursor.
	 */
	struct FControllerRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;
		ACrabPlayerController* Controller = nullptr;

		explicit FControllerRig(float X = 0.f, float Y = 0.f)
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Crab = SpawnCrab(World, *Beach, X, Y);
					Controller = World.SpawnActor<ACrabPlayerController>();
				}
				if (Crab)
				{
					Settle(World);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Crab && Controller; }
		FVector Ground(float DX, float DY) const
		{
			const FVector At = Crab->GetActorLocation();
			return FVector(At.X + DX, At.Y + DY, Crab->GetFeetZ());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerClickWalksTest, "CrabSim.Controller.ClickingGroundSendsTheCrabThere", TestFlags)
bool FCrabControllerClickWalksTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Point = Rig.Ground(0.f, 600.f);
	Rig.Controller->HandleClick(*Rig.Crab, Point);
	TestTrue(TEXT("target set"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("at the clicked point"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Point) < 1.f);
	TestFalse(TEXT("not dancing"), Rig.Crab->IsDancing());

	Rig.World.TickSeconds(2.f);
	TestTrue(TEXT("and it walks there"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Point) < 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerClickCrabDancesTest, "CrabSim.Controller.ClickingTheCrabTogglesTheDance", TestFlags)
bool FCrabControllerClickCrabDancesTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(30.f, 20.f));
	TestTrue(TEXT("a click on the crab starts the dance"), Rig.Crab->IsDancing());
	TestFalse(TEXT("and does not send it anywhere"), Rig.Crab->HasMoveTarget());

	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(-20.f, 40.f));
	TestFalse(TEXT("a second click on it stops the dance"), Rig.Crab->IsDancing());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerClickAwayStopsDanceTest, "CrabSim.Controller.ClickingAwayEndsTheDanceAndWalks", TestFlags)
bool FCrabControllerClickAwayStopsDanceTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->StartDance();
	Rig.World.TickSeconds(0.3f);
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(0.f, 700.f));
	TestFalse(TEXT("the dance is over"), Rig.Crab->IsDancing());
	TestTrue(TEXT("and the crab is walking"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerRadiusTest, "CrabSim.Controller.DanceClickRadiusIsRespected", TestFlags)
bool FCrabControllerRadiusTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Controller->DanceClickRadius = 10.f;
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(50.f, 0.f));
	TestFalse(TEXT("50 away is outside a 10 radius"), Rig.Crab->IsDancing());
	TestTrue(TEXT("so it walks"), Rig.Crab->HasMoveTarget());

	Rig.Crab->ClearMoveTarget();
	Rig.Controller->DanceClickRadius = 200.f;
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(150.f, 0.f));
	TestTrue(TEXT("150 away is inside a 200 radius"), Rig.Crab->IsDancing());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerBurrowClickTest, "CrabSim.Controller.ClickingABurrowWalksThereAndDigsIn", TestFlags)
bool FCrabControllerBurrowClickTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig(-700.f, -300.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Beach->GetBurrows()[1].Location;
	// Click a little off the centre: it should snap to the burrow.
	Rig.Controller->HandleClick(*Rig.Crab, Hole + FVector(45.f, -30.f, 0.f));
	TestTrue(TEXT("walking"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("the target snapped to the burrow centre"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Hole) < 1.f);

	Rig.World.TickSeconds(3.f);
	TestTrue(TEXT("dug in on arrival"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("the burrow that was clicked"), Rig.Crab->GetCurrentBurrow(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerBurrowPriorityTest, "CrabSim.Controller.ABurrowBeatsTheCrabForAClickBetweenThem", TestFlags)
bool FCrabControllerBurrowPriorityTest::RunTest(const FString& Parameters)
{
	// The crab stands on a burrow. A click on it means dig in, not dance.
	FControllerRig Rig(-1000.f, -520.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Crab->GetActorLocation());
	Rig.World.TickSeconds(0.5f);
	TestFalse(TEXT("no dance"), Rig.Crab->IsDancing());
	TestTrue(TEXT("dug in"), Rig.Crab->IsInBurrow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerBurrowStayTest, "CrabSim.Controller.ClickingYourOwnHoleKeepsYouInAndAwayBringsYouOut", TestFlags)
bool FCrabControllerBurrowStayTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(0.5f);
	const FVector Hole = Rig.Beach->GetBurrows()[HighBurrow].Location;

	Rig.Controller->HandleClick(*Rig.Crab, Hole + FVector(30.f, 0.f, 0.f));
	TestTrue(TEXT("still in after clicking the hole"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("and nowhere to walk"), Rig.Crab->HasMoveTarget());

	Rig.Controller->HandleClick(*Rig.Crab, Hole + FVector(0.f, 900.f, 0.f));
	TestFalse(TEXT("clicking elsewhere brings the crab out"), Rig.Crab->IsInBurrow());
	TestTrue(TEXT("and sends it there"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerHoldFollowsTest, "CrabSim.Controller.HoldingFollowsTheCursor", TestFlags)
bool FCrabControllerHoldFollowsTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector A = Rig.Ground(0.f, 500.f);
	const FVector B = Rig.Ground(400.f, -300.f);
	Rig.Controller->HandleClick(*Rig.Crab, A);
	Rig.Controller->HandleHold(*Rig.Crab, A);
	TestTrue(TEXT("target is where the cursor is"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), A) < 1.f);
	Rig.Controller->HandleHold(*Rig.Crab, B);
	TestTrue(TEXT("and follows it when it moves"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), B) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerHoldDanceTest, "CrabSim.Controller.HoldingOnTheCrabKeepsTheDanceDraggingAwayEndsIt", TestFlags)
bool FCrabControllerHoldDanceTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(10.f, 10.f));
	TestTrue(TEXT("dancing"), Rig.Crab->IsDancing());

	// Still holding, cursor still on the crab: the dance goes on, and the crab does not wander off.
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Rig.Controller->HandleHold(*Rig.Crab, Rig.Ground(15.f, -10.f));
		Rig.World.TickN(1, 1.f / 60.f);
	}
	TestTrue(TEXT("still dancing while the button is held on the crab"), Rig.Crab->IsDancing());
	TestFalse(TEXT("no walking target"), Rig.Crab->HasMoveTarget());

	// Drag away and it follows.
	Rig.Controller->HandleHold(*Rig.Crab, Rig.Ground(0.f, 500.f));
	TestFalse(TEXT("dragging away ends the dance"), Rig.Crab->IsDancing());
	TestTrue(TEXT("and starts walking"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerHoldInBurrowTest, "CrabSim.Controller.HoldingDoesNotDigTheCrabOut", TestFlags)
bool FCrabControllerHoldInBurrowTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(0.5f);
	// A held button left over from the click that sent the crab here must not undo it.
	Rig.Controller->HandleHold(*Rig.Crab, Rig.Ground(0.f, 900.f));
	TestTrue(TEXT("still in the burrow"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("with no target"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerHoldNearBurrowTest, "CrabSim.Controller.HoldingOnABurrowStillDigsIn", TestFlags)
bool FCrabControllerHoldNearBurrowTest::RunTest(const FString& Parameters)
{
	FControllerRig Rig(-700.f, -300.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Beach->GetBurrows()[1].Location;
	// Press on the burrow and keep holding while walking: the burrow intent must survive every frame's re-targeting.
	Rig.Controller->HandleClick(*Rig.Crab, Hole);
	for (int32 Frame = 0; Frame < 200 && !Rig.Crab->IsInBurrow(); ++Frame)
	{
		Rig.Controller->HandleHold(*Rig.Crab, Hole + FVector(20.f, 10.f, 0.f));
		Rig.World.TickN(1, 1.f / 60.f);
	}
	TestTrue(TEXT("dug in while the button was held"), Rig.Crab->IsInBurrow());
	return true;
}
