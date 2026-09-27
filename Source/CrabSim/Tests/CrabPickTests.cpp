// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabHudMath.h"
#include "CrabPickMath.h"
#include "CrabPlayerController.h"

using namespace UE::CrabSim::Tests;

namespace
{
	const FVector2D PickView(1280.f, 720.f);

	/**
	 * A beach, a crab, the pointer controller, and a camera of our own for it: +Y is right on screen, +X is up it
	 * (foreshortened to 0.6), everything PixelsPerUu big, the crab in the middle. A test world has no viewport.
	 */
	struct FPickRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;
		ACrabPlayerController* Controller = nullptr;
		FVector CrabAt = FVector::ZeroVector;
		float PixelsPerUu = 0.3f;

		explicit FPickRig(float X = 0.f, float Y = 0.f, float PerUu = 0.3f)
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
					Beach->SetTideClock(LowTide);
					UseCamera(PerUu);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Crab && Controller; }

		void UseCamera(float PerUu)
		{
			PixelsPerUu = PerUu;
			CrabAt = Crab->GetActorLocation();
			const FVector At = CrabAt;
			Controller->ProjectToView = [At, PerUu](const FVector& World, FVector2D& OutPixel)
			{
				OutPixel = FVector2D(640.f + (World.Y - At.Y) * PerUu, 360.f - (World.X - At.X) * PerUu * 0.6f);
				return true;
			};
		}

		FVector2D PixelOf(const FVector& Where) const
		{
			FVector2D Pixel;
			Controller->ProjectToView(Where, Pixel);
			return Pixel;
		}

		/** The ground the cursor would point at for a pixel: the camera run backwards. */
		FVector GroundOf(const FVector2D& Pixel) const
		{
			return FVector(CrabAt.X - (Pixel.Y - 360.f) / (PixelsPerUu * 0.6f), CrabAt.Y + (Pixel.X - 640.f) / PixelsPerUu, Crab->GetFeetZ());
		}

		void Press(const FVector2D& Pixel)
		{
			const FVector Ground = GroundOf(Pixel);
			Controller->NotifyLeftButtonReleased();
			Controller->HandleLeftPress(*Crab, Pixel, PickView, &Ground);
		}

		FVector Burrow(int32 Index) const { return Beach->GetBurrows()[Index].Location; }
		FVector Patch(int32 Index) const { return Beach->GetFoodPatches()[Index].Location; }
		bool HeadsFor(const FVector& Where) const { return Crab->HasMoveTarget() && FVector::Dist2D(Crab->GetMoveTarget(), Where) < 1.f; }
		void Clear() { Crab->ClearMoveTarget(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickBurrowEdgeTest, "CrabSim.Pick.AClickAtTheMinimumEdgeOfABurrowZoneSelectsItAndOneJustOutsideWalks", TestFlags)
bool FCrabPickBurrowEdgeTest::RunTest(const FString& Parameters)
{
	FPickRig Rig(-700.f, -300.f, 0.3f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Burrow(1);
	const FVector2D Middle = Rig.PixelOf(Hole);
	TestTrue(TEXT("the hole is on screen and clear of the buttons"), !CrabHud::HitsAnyButton(PickView.X, PickView.Y, Middle) && Middle.X > 100.f && Middle.X < 1100.f && Middle.Y > 100.f && Middle.Y < 600.f);
	// At 0.3 px/uu the world radius (100 uu) is 30 px across and 18 down: the minimum, 45 by 30, is what counts.

	Rig.Press(Middle + FVector2D(44.f, 0.f));
	TestTrue(TEXT("44 px to the side, inside the 90 px zone but 147 uu away on the ground: the burrow"), Rig.HeadsFor(Hole));
	Rig.Clear();
	Rig.Press(Middle + FVector2D(-44.f, 0.f));
	TestTrue(TEXT("and the other side"), Rig.HeadsFor(Hole));
	Rig.Clear();
	Rig.Press(Middle + FVector2D(0.f, 29.f));
	TestTrue(TEXT("29 px below, inside the 60 px zone: the burrow"), Rig.HeadsFor(Hole));
	Rig.Clear();
	Rig.Press(Middle + FVector2D(0.f, -29.f));
	TestTrue(TEXT("and above"), Rig.HeadsFor(Hole));
	Rig.Clear();

	const FVector2D Outside = Middle + FVector2D(47.f, 0.f);
	Rig.Press(Outside);
	TestTrue(TEXT("47 px to the side, just outside: a walk"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("to the ground under the cursor"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Rig.GroundOf(Outside)) < 1.f);
	TestFalse(TEXT("not to the burrow"), Rig.HeadsFor(Hole));
	Rig.Clear();
	Rig.Press(Middle + FVector2D(0.f, 32.f));
	TestFalse(TEXT("32 px below, just outside: not the burrow"), Rig.HeadsFor(Hole));
	TestTrue(TEXT("a walk"), Rig.Crab->HasMoveTarget());
	Rig.Clear();
	Rig.Press(Middle + FVector2D(40.f, 26.f));
	TestFalse(TEXT("the corner of the 90 by 60 box is outside: the zone is an ellipse"), Rig.HeadsFor(Hole));
	TestTrue(TEXT("and walks"), Rig.Crab->HasMoveTarget());

	// A click inside the zone digs in, all the same.
	Rig.Clear();
	Rig.Press(Middle + FVector2D(44.f, 0.f));
	Rig.World.TickSeconds(6.f);
	TestTrue(TEXT("the crab digs in when it gets there"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("in the burrow that was clicked"), Rig.Crab->GetCurrentBurrow(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickPatchEdgeTest, "CrabSim.Pick.AClickAtTheMinimumEdgeOfAPatchZoneSelectsItAndOneJustOutsideWalks", TestFlags)
bool FCrabPickPatchEdgeTest::RunTest(const FString& Parameters)
{
	// Far off: 0.15 px/uu makes the patch's 200 uu 30 px across, under the 45 px minimum.
	FPickRig Rig(0.f, 0.f, 0.15f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Where = Rig.Patch(3);
	const FVector2D Middle = Rig.PixelOf(Where);
	TestFalse(TEXT("the patch is clear of the buttons"), CrabHud::HitsAnyButton(PickView.X, PickView.Y, Middle));

	Rig.Press(Middle + FVector2D(44.f, 0.f));
	TestTrue(TEXT("44 px to the side: the patch (293 uu away on the ground, beyond its 200 uu radius)"), Rig.HeadsFor(Where));
	Rig.Clear();
	Rig.Press(Middle + FVector2D(0.f, 29.f));
	TestTrue(TEXT("29 px below: the patch"), Rig.HeadsFor(Where));
	Rig.Clear();
	Rig.Press(Middle + FVector2D(47.f, 0.f));
	TestFalse(TEXT("47 px to the side: a walk"), Rig.HeadsFor(Where));
	TestTrue(TEXT("still a walk"), Rig.Crab->HasMoveTarget());
	Rig.Clear();
	Rig.Press(Middle + FVector2D(0.f, -32.f));
	TestFalse(TEXT("32 px above: a walk"), Rig.HeadsFor(Where));
	TestTrue(TEXT("still a walk"), Rig.Crab->HasMoveTarget());

	// The patch order is a feed: the crab walks there and sifts.
	Rig.Clear();
	Rig.Press(Middle + FVector2D(44.f, 0.f));
	Rig.World.TickSeconds(FVector::Dist2D(Rig.Crab->GetActorLocation(), Where) / 300.f + 3.f);
	TestTrue(TEXT("and feeds when it gets there"), Rig.Crab->IsFeeding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickWorldRadiusTest, "CrabSim.Pick.TheWorldRadiusStillCountsWhenItIsTheLargerOnScreen", TestFlags)
bool FCrabPickWorldRadiusTest::RunTest(const FString& Parameters)
{
	// Close up: 1.5 px/uu makes the burrow's 100 uu 150 px wide (a half of 150), far over the minimum.
	FPickRig Rig(-700.f, -300.f, 1.5f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Burrow(1);
	const FVector Near = Hole + FVector(0.f, 90.f, 0.f);
	Rig.Press(Rig.PixelOf(Near));
	TestTrue(TEXT("90 uu from the middle, 135 px on screen: still the burrow, as before"), Rig.HeadsFor(Hole));
	Rig.Clear();
	const FVector Far = Hole + FVector(0.f, 130.f, 0.f);
	Rig.Press(Rig.PixelOf(Far));
	TestFalse(TEXT("130 uu, 195 px: outside both zones, a walk"), Rig.HeadsFor(Hole));
	TestTrue(TEXT("a walk"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickPriorityTest, "CrabSim.Pick.ABurrowStillBeatsAPatchUnderTheSameClick", TestFlags)
bool FCrabPickPriorityTest::RunTest(const FString& Parameters)
{
	// At 0.1 px/uu every zone is its 90 by 60 minimum, and burrow1 and patch2 are 43 px apart on screen: their zones overlap.
	FPickRig Rig(-700.f, -300.f, 0.1f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Burrow(1);
	const FVector Food = Rig.Patch(2);
	const FVector2D HoleAt = Rig.PixelOf(Hole);
	const FVector2D FoodAt = Rig.PixelOf(Food);
	const FVector2D Half(45.f, 30.f);
	const FVector2D Between = (HoleAt + FoodAt) * 0.5f;
	TestTrue(TEXT("a pixel between them is inside both zones"), CrabPick::EllipseDistance(Between, HoleAt, Half) < 1.f && CrabPick::EllipseDistance(Between, FoodAt, Half) < 1.f);
	TestTrue(TEXT("the patch's middle is outside the burrow's zone"), CrabPick::EllipseDistance(FoodAt, HoleAt, Half) > 1.f);

	Rig.Press(Between);
	TestTrue(TEXT("a click inside both: the burrow"), Rig.HeadsFor(Hole));
	Rig.Clear();
	Rig.Press(FoodAt);
	TestTrue(TEXT("a click on the patch's middle, outside the burrow's zone: the patch"), Rig.HeadsFor(Food));
	Rig.Clear();
	Rig.Press(HoleAt);
	TestTrue(TEXT("a click on the burrow's middle: the burrow"), Rig.HeadsFor(Hole));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickButtonTest, "CrabSim.Pick.AZoneNeverReachesUnderAHudButton", TestFlags)
bool FCrabPickButtonTest::RunTest(const FString& Parameters)
{
	// Slide the camera so burrow1 sits under the FOOD button.
	FPickRig Rig(-700.f, -300.f, 0.3f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Burrow(1);
	const FVector2D Button = CrabHud::FoodButtonRect(PickView.X, PickView.Y).GetCenter();
	const FVector2D Shift = Button - Rig.PixelOf(Hole);
	const FVector At = Rig.CrabAt;
	Rig.Controller->ProjectToView = [At, Shift](const FVector& World, FVector2D& OutPixel)
	{
		OutPixel = FVector2D(640.f + (World.Y - At.Y) * 0.3f, 360.f - (World.X - At.X) * 0.18f) + Shift;
		return true;
	};
	TestTrue(TEXT("the burrow is under the button now"), CrabHud::HitsFoodButton(PickView.X, PickView.Y, Rig.PixelOf(Hole)));

	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Crab->SetFood(0.4f);
	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, PickView, &Hole);
	TestFalse(TEXT("a press on the button is a press on the button, not on the burrow under it"), Rig.HeadsFor(Hole));
	TestTrue(TEXT("it went to the food"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickHoldTest, "CrabSim.Pick.HoldingInsideAZoneKeepsTheBurrowOrder", TestFlags)
bool FCrabPickHoldTest::RunTest(const FString& Parameters)
{
	FPickRig Rig(-700.f, -300.f, 0.3f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Hole = Rig.Burrow(1);
	const FVector2D Held = Rig.PixelOf(Hole) + FVector2D(40.f, 8.f);
	const FVector Ground = Rig.GroundOf(Held);
	const FCrabPointer Pointer{Held, PickView};
	Rig.Controller->HandleClick(*Rig.Crab, Ground, &Pointer);
	for (int32 Frame = 0; Frame < 300 && !Rig.Crab->IsInBurrow(); ++Frame)
	{
		Rig.Controller->HandleHold(*Rig.Crab, Ground, &Pointer);
		Rig.World.TickN(1, 1.f / 60.f);
	}
	TestTrue(TEXT("dug in while the button was held on the zone's rim"), Rig.Crab->IsInBurrow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickOwnHoleTest, "CrabSim.Pick.TheHoleYouAreInKeepsYouInAcrossItsWholeZone", TestFlags)
bool FCrabPickOwnHoleTest::RunTest(const FString& Parameters)
{
	FPickRig Rig(-2450.f, 350.f, 0.3f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(0.5f);
	Rig.UseCamera(0.3f);
	const FVector2D Middle = Rig.PixelOf(Rig.Burrow(HighBurrow));

	Rig.Press(Middle + FVector2D(44.f, 0.f));
	TestTrue(TEXT("a click 44 px from the hole you are in keeps you in"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("with nowhere to walk"), Rig.Crab->HasMoveTarget());
	Rig.Press(Middle + FVector2D(60.f, 0.f));
	TestFalse(TEXT("60 px away brings you out"), Rig.Crab->IsInBurrow());
	TestTrue(TEXT("and walks"), Rig.Crab->HasMoveTarget());
	return true;
}
