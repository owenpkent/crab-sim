// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabFoodMath.h"
#include "CrabGotoMath.h"
#include "CrabHudMath.h"
#include "CrabPlayerController.h"

using namespace UE::CrabSim::Tests;

namespace
{
	const FVector2D GotoView(1280.f, 720.f);

	/** A beach, a crab standing on it, and the pointer controller (which does not possess the crab). */
	struct FGotoRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;
		ACrabPlayerController* Controller = nullptr;

		explicit FGotoRig(float X = 0.f, float Y = 0.f)
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
		FVector2D FoodButton() const { return CrabHud::FoodButtonRect(GotoView.X, GotoView.Y).GetCenter(); }
		FVector2D BurrowButton() const { return CrabHud::BurrowButtonRect(GotoView.X, GotoView.Y).GetCenter(); }

		/** The patch nearest the crab, whatever it holds. */
		int32 NearestPatch() const
		{
			int32 Best = INDEX_NONE;
			float BestDistance = BIG_NUMBER;
			for (int32 Index = 0; Index < Beach->GetFoodPatches().Num(); ++Index)
			{
				const float Distance = FVector::Dist2D(Crab->GetActorLocation(), Beach->GetFoodPatches()[Index].Location);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Index;
				}
			}
			return Best;
		}

		void Teleport(float X, float Y)
		{
			Crab->SetActorLocation(FVector(X, Y, Beach->GetGroundHeight(X, Y) + 70.f), false, nullptr, ETeleportType::TeleportPhysics);
			Settle(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoFoodWalksAndFeedsTest, "CrabSim.Goto.PressingFoodWalksToTheNearestPatchAndFeeds", TestFlags)
bool FCrabGotoFoodWalksAndFeedsTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	const int32 Nearest = Rig.NearestPatch();
	const FVector Patch = Rig.Beach->GetFoodPatches()[Nearest].Location;
	const FVector Behind = Rig.Ground(0.f, 900.f);
	const float FoodBefore = Rig.Crab->GetFood();

	TestEqual(TEXT("the button is on"), Rig.Crab->CheckGoToFood(), CrabGoto::EFoodResult::Ok);
	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.FoodButton(), GotoView, &Behind);
	TestTrue(TEXT("the crab is sent walking"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("to the middle of the nearest patch, not to the ground behind the button"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Patch) < 1.f);

	// The rest of the press, held on the button, is not a walk to the ground behind it.
	Rig.Controller->HandleHold(*Rig.Crab, Behind);
	TestTrue(TEXT("holding the button changes nothing"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Patch) < 1.f);

	Rig.World.TickSeconds(FVector::Dist2D(Rig.Crab->GetActorLocation(), Patch) / 300.f + 3.f);
	TestTrue(TEXT("and feeds when it gets there"), Rig.Crab->IsFeeding());
	TestEqual(TEXT("on that patch"), Rig.Crab->GetFeedingPatch(), Nearest);
	Rig.World.TickSeconds(3.f);
	TestTrue(TEXT("and the food goes up"), Rig.Crab->GetFood() > FoodBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoFoodSkipsTest, "CrabSim.Goto.FoodSkipsBarePatchesAndSaysWhyWhenThereAreNone", TestFlags)
bool FCrabGotoFoodSkipsTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	const int32 First = Rig.NearestPatch();
	Rig.Beach->TakeFood(First, 10.f);
	int32 Chosen = INDEX_NONE;
	TestEqual(TEXT("with the nearest bare the button is still on"), Rig.Crab->CheckGoToFood(&Chosen), CrabGoto::EFoodResult::Ok);
	TestTrue(TEXT("and it picks another patch"), Chosen != First && Chosen != INDEX_NONE);
	const FVector Behind = Rig.Ground(0.f, 900.f);
	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.FoodButton(), GotoView, &Behind);
	TestTrue(TEXT("and walks to it"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Rig.Beach->GetFoodPatches()[Chosen].Location) < 1.f);

	for (int32 Index = 0; Index < Rig.Beach->GetFoodPatches().Num(); ++Index)
	{
		Rig.Beach->TakeFood(Index, 10.f);
	}
	Rig.Crab->ClearMoveTarget();
	TestEqual(TEXT("every patch bare: no patch left"), Rig.Crab->CheckGoToFood(&Chosen), CrabGoto::EFoodResult::NoPatch);
	TestEqual(TEXT("and none is named"), Chosen, static_cast<int32>(INDEX_NONE));
	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.FoodButton(), GotoView, &Behind);
	TestFalse(TEXT("a press on the greyed button walks nowhere"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("No dry food left")));

	Rig.Crab->SetFood(1.f);
	TestEqual(TEXT("a full crab has no use for food"), Rig.Crab->CheckGoToFood(), CrabGoto::EFoodResult::NotHungry);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoFoodTideTest, "CrabSim.Goto.FoodNeverPicksAPatchTheSeaHasOrIsAboutToTake", TestFlags)
bool FCrabGotoFoodTideTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	// Whatever the tide is doing, the patch chosen is dry and stays dry for the walk plus the lead.
	for (const float Clock : {0.f, 15.f, 25.f, 35.f, 45.f, 60.f, 75.f, 90.f, 120.f, 160.f})
	{
		Rig.Beach->SetTideClock(Clock);
		Rig.Crab->SetFood(0.4f);
		int32 Chosen = INDEX_NONE;
		const CrabGoto::EFoodResult Result = Rig.Crab->CheckGoToFood(&Chosen);
		if (Result != CrabGoto::EFoodResult::Ok)
		{
			TestEqual(*FString::Printf(TEXT("at %.0f s no patch means no patch, nothing else"), Clock), Result, CrabGoto::EFoodResult::NoPatch);
			continue;
		}
		const FCrabFoodPatch& Patch = Rig.Beach->GetFoodPatches()[Chosen];
		const float Depth = Rig.Beach->GetWaterDepthAt(Patch.Location.X, Patch.Location.Y);
		const float Soaked = Rig.Beach->GetSecondsUntilWaterDeeperThan(Patch.Location.X, Patch.Location.Y, CrabFood::SoakDepth);
		const float Walk = CrabGoto::ArrivalSeconds(FVector::Dist2D(Rig.Crab->GetActorLocation(), Patch.Location));
		TestTrue(*FString::Printf(TEXT("at %.0f s patch %d is dry (%.1f uu)"), Clock, Chosen, Depth), Depth <= CrabFood::SoakDepth);
		TestTrue(*FString::Printf(TEXT("and stays so for the walk and the lead (%.1f s of %.1f)"), Soaked, Walk + CrabGoto::Tuning::FeedLeadSeconds), Soaked >= Walk + CrabGoto::Tuning::FeedLeadSeconds);
	}

	// A patch two seconds from the water is not worth the walk, though it is where the crab stands.
	Rig.Teleport(1700.f, -420.f);
	Rig.Beach->SetTideClock(LowTide);
	int32 Chosen = INDEX_NONE;
	Rig.Crab->CheckGoToFood(&Chosen);
	TestEqual(TEXT("at low water the crab's own patch is the nearest"), Chosen, 5);
	Rig.Beach->SetTideClock(24.f);
	TestTrue(TEXT("just before it floods, the patch is about to be soaked"), Rig.Beach->GetSecondsUntilWaterDeeperThan(1700.f, -420.f, CrabFood::SoakDepth) < CrabGoto::Tuning::FeedLeadSeconds);
	Rig.Crab->CheckGoToFood(&Chosen);
	TestTrue(TEXT("so the button picks another"), Chosen != 5);

	// The clock the beach counts in runs at the tide speed, and a frozen tide never floods anything new.
	Rig.Beach->SetTideClock(LowTide);
	Rig.Beach->SetTideFrozen(true);
	TestEqual(TEXT("a frozen tide at low water never reaches the patch"), Rig.Beach->GetSecondsUntilWaterDeeperThan(1700.f, -420.f, CrabFood::SoakDepth), static_cast<float>(BIG_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoBurrowDigsInTest, "CrabSim.Goto.PressingBurrowWalksToTheSafestBurrowAndDigsIn", TestFlags)
bool FCrabGotoBurrowDigsInTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	const FVector Behind = Rig.Ground(0.f, 900.f);
	int32 Chosen = INDEX_NONE;
	TestEqual(TEXT("the button is on"), Rig.Crab->CheckGoToBurrow(&Chosen), CrabGoto::EBurrowResult::Ok);
	TestEqual(TEXT("it picks the highest burrow, the one the sea never floods"), Chosen, HighBurrow);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.BurrowButton(), GotoView, &Behind);
	TestTrue(TEXT("the crab is sent walking"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("to the burrow, not to the ground behind the button"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Rig.Beach->GetBurrows()[HighBurrow].Location) < 1.f);

	Rig.World.TickSeconds(20.f);
	TestTrue(TEXT("and it digs in"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("in that burrow"), Rig.Crab->GetCurrentBurrow(), HighBurrow);

	// Already in one: the button is grey, says why, and is not a click on the ground either.
	TestEqual(TEXT("in a burrow the button is grey"), Rig.Crab->CheckGoToBurrow(), CrabGoto::EBurrowResult::InBurrow);
	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.BurrowButton(), GotoView, &Behind);
	TestTrue(TEXT("a press on it leaves the crab in"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("with no walk"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("and a reason"), Rig.Crab->GetMessage().Contains(TEXT("Already dug in")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoBurrowFloodTest, "CrabSim.Goto.BurrowNeverPicksAHoleTheSeaHasOrWillHaveTakenOnArrival", TestFlags)
bool FCrabGotoBurrowFloodTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig(1900.f, -900.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	for (const float Clock : {0.f, 30.f, 50.f, 60.f, 70.f, 90.f, 110.f, 130.f})
	{
		Rig.Beach->SetTideClock(Clock);
		int32 Chosen = INDEX_NONE;
		const CrabGoto::EBurrowResult Result = Rig.Crab->CheckGoToBurrow(&Chosen);
		if (!TestEqual(*FString::Printf(TEXT("at %.0f s the button is on (the high burrow never floods)"), Clock), Result, CrabGoto::EBurrowResult::Ok))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("burrow %d is not flooded at %.0f s"), Chosen, Clock), Rig.Beach->IsBurrowFlooded(Chosen));
		const FCrabBurrow& Burrow = Rig.Beach->GetBurrows()[Chosen];
		const float Walk = CrabGoto::ArrivalSeconds(FVector::Dist2D(Rig.Crab->GetActorLocation(), Burrow.Location));
		TestTrue(TEXT("and stays dry until the crab is in"), Rig.Beach->GetSecondsUntilWaterDeeperThan(Burrow.Location.X, Burrow.Location.Y, Burrow.FloodDepth) >= Walk);
	}

	// With the crab standing at the lowest burrow at high water, that one is flooded and the button goes elsewhere.
	Rig.Beach->SetTideClock(HighTide);
	TestTrue(TEXT("the low burrow is flooded at high water"), Rig.Beach->IsBurrowFlooded(LowBurrow));
	int32 Chosen = INDEX_NONE;
	Rig.Crab->CheckGoToBurrow(&Chosen);
	TestTrue(TEXT("so it is not the pick"), Chosen != LowBurrow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoFromBurrowTest, "CrabSim.Goto.PressingFoodInABurrowComesOutAndWalksToTheFood", TestFlags)
bool FCrabGotoFromBurrowTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(0.5f);
	TestTrue(TEXT("dug in"), Rig.Crab->IsInBurrow());
	const FVector Behind = Rig.Ground(0.f, 900.f);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.FoodButton(), GotoView, &Behind);
	TestFalse(TEXT("the press brings the crab out"), Rig.Crab->IsInBurrow());
	TestTrue(TEXT("and sends it to a patch"), Rig.Crab->HasMoveTarget());
	int32 Chosen = INDEX_NONE;
	Rig.Crab->CheckGoToFood(&Chosen);
	TestTrue(TEXT("the nearest one"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Rig.Beach->GetFoodPatches()[Rig.NearestPatch()].Location) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoStopsFeedingTest, "CrabSim.Goto.PressingBurrowWhileFeedingStopsTheFeedAndGoes", TestFlags)
bool FCrabGotoStopsFeedingTest::RunTest(const FString& Parameters)
{
	FGotoRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	const int32 Nearest = Rig.NearestPatch();
	const FVector Patch = Rig.Beach->GetFoodPatches()[Nearest].Location;
	Rig.Teleport(Patch.X, Patch.Y);
	Rig.Crab->SetMoveTarget(Patch, INDEX_NONE, Nearest);
	Rig.World.TickSeconds(1.5f);
	TestTrue(TEXT("feeding"), Rig.Crab->IsFeeding());

	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.BurrowButton(), GotoView, nullptr);
	TestFalse(TEXT("a press on BURROW stops the feed"), Rig.Crab->IsFeeding());
	TestTrue(TEXT("and heads for a burrow"), Rig.Crab->HasMoveTarget());
	return true;
}
