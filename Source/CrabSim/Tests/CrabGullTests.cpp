// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabGull.h"
#include "CrabGullMath.h"
#include "CrabHudMath.h"
#include "CrabMoltMath.h"
#include "CrabPlayerController.h"
#include "CrabSimGameMode.h"

#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

using namespace UE::CrabSim::Tests;

namespace
{
	const FVector2D GullView(1280.f, 720.f);

	/** A beach frozen at low water, the crab on it, a gull, and the pointer controller (which does not possess the crab). */
	struct FGullRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;
		ACrabGull* Gull = nullptr;
		ACrabPlayerController* Controller = nullptr;

		explicit FGullRig(float X = 0.f, float Y = 0.f, bool bFreezeTide = true)
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Beach->SetTideClock(LowTide);
					Beach->SetTideFrozen(bFreezeTide);
					Crab = SpawnCrab(World, *Beach, X, Y);
					Gull = World.SpawnActor<ACrabGull>();
					Controller = World.SpawnActor<ACrabPlayerController>();
				}
				if (Crab)
				{
					Settle(World);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Crab && Gull && Controller; }

		/** Ticks at 60 frames a second until Done holds or Seconds pass. True if it held. */
		bool RunUntil(TFunctionRef<bool()> Done, float Seconds)
		{
			for (int32 Frame = 0; Frame < FMath::CeilToInt(Seconds * 60.f); ++Frame)
			{
				World.TickN(1, 1.f / 60.f);
				if (Done())
				{
					return true;
				}
			}
			return Done();
		}

		bool IsPhase(CrabGull::EPhase Phase) const { return Gull->GetPhase() == Phase; }
		/** Landing, and the glide is over: the gull stands on the ground at its spot, eating. */
		bool IsDown() const { return IsPhase(CrabGull::EPhase::Landing) && Gull->GetState().PhaseSeconds >= CrabGull::Tuning::LandGlideSeconds; }

		/** Three molts in the highest burrow: the round is won. */
		void WinTheRound()
		{
			Crab->SetFood(1.f);
			Crab->EnterBurrow(HighBurrow);
			for (int32 Molt = 0; Molt < CrabMolt::Tuning::MoltsToWin; ++Molt)
			{
				Crab->SetFood(1.f);
				Crab->StartMolt();
				World.TickSeconds(CrabMolt::Tuning::Duration + 0.3f);
			}
		}
	};

	/** Sets a console variable for the length of a test and puts it back. */
	struct FCVarScope
	{
		IConsoleVariable* Variable = nullptr;
		int32 Before = 0;

		FCVarScope(const TCHAR* Name, int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Before = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		~FCVarScope()
		{
			if (Variable)
			{
				Variable->Set(Before, ECVF_SetByCode);
			}
		}
	};

	int32 CountGulls(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACrabGull> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullGameModeTest, "CrabSim.Gull.TheGameModePutsOneGullInTheWorldOutOfSight", TestFlags)
bool FCrabGullGameModeTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world"), World.IsReady()))
	{
		return false;
	}
	ACrabSimGameMode* Mode = World.SpawnActor<ACrabSimGameMode>();
	if (!TestNotNull(TEXT("game mode"), Mode))
	{
		return false;
	}
	TestNotNull(TEXT("it has a gull"), Mode->GetGull());
	TestEqual(TEXT("one in the world"), CountGulls(World.GetTestWorld()), 1);
	TestTrue(TEXT("nothing to see yet"), Mode->GetGull() && Mode->GetGull()->IsHidden() && !Mode->GetGull()->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullVisualTest, "CrabSim.Gull.TheGullFlapsInTheAirAndFoldsItsWingsOnTheGround", TestFlags)
bool FCrabGullVisualTest::RunTest(const FString& Parameters)
{
	FGullRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("out of sight to begin with"), Rig.Gull->IsHidden());
	TestFalse(TEXT("and no ring"), Rig.Gull->IsRingShown());

	TArray<UStaticMeshComponent*> Parts;
	Rig.Gull->GetComponents<UStaticMeshComponent>(Parts);
	bool bAllShapes = true;
	for (const UStaticMeshComponent* Part : Parts)
	{
		bAllShapes = bAllShapes && Part->GetStaticMesh() != nullptr;
	}
	TestTrue(TEXT("built from engine shapes: a body, a head, a bill, two wings, legs, the shadow and the ring"), Parts.Num() >= 24 && bAllShapes);

	Rig.Gull->SpawnNow();
	Rig.World.TickSeconds(0.5f);
	TestTrue(TEXT("circling"), Rig.IsPhase(CrabGull::EPhase::Circling));
	TestFalse(TEXT("and in sight"), Rig.Gull->IsHidden());
	TestFalse(TEXT("no ring while it is in the air"), Rig.Gull->IsRingShown());
	TestTrue(TEXT("it is up in the air over the flats"), Rig.Gull->GetActorLocation().Z > Rig.Beach->GetGroundHeight(Rig.Gull->GetActorLocation().X, Rig.Gull->GetActorLocation().Y) + 200.f);

	float Lowest = BIG_NUMBER;
	float Highest = -BIG_NUMBER;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Rig.World.TickN(1, 1.f / 60.f);
		Lowest = FMath::Min(Lowest, Rig.Gull->GetWingTilt());
		Highest = FMath::Max(Highest, Rig.Gull->GetWingTilt());
	}
	TestTrue(*FString::Printf(TEXT("the wings flap while it circles (%.0f to %.0f degrees)"), Lowest, Highest), Highest - Lowest > 20.f);
	TestTrue(TEXT("with the tips held up, not down"), Highest > 0.f);

	Rig.RunUntil([&] { return Rig.IsDown(); }, 30.f);
	Rig.World.TickSeconds(1.5f);
	TestTrue(TEXT("down on the sand"), Rig.Gull->GetActorLocation().Z < Rig.Beach->GetGroundHeight(Rig.Gull->GetActorLocation().X, Rig.Gull->GetActorLocation().Y) + 5.f);
	TestTrue(TEXT("the wings are folded"), Rig.Gull->GetWingTilt() < -60.f);
	TestTrue(TEXT("and a ring shows under it, so it can be seen on the sand"), Rig.Gull->IsRingShown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullEatsPatchTest, "CrabSim.Gull.AGullThatLandsOnAPatchEatsIt", TestFlags)
bool FCrabGullEatsPatchTest::RunTest(const FString& Parameters)
{
	FGullRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	// From the start point the only patch 1400 to 1800 uu away is patch 5, at the low flats.
	const float Fresh = Rig.Beach->GetFoodPatches()[5].Richness;
	Rig.Gull->SpawnNow();
	TestTrue(TEXT("it lands"), Rig.RunUntil([&] { return Rig.IsDown(); }, 30.f));
	TestEqual(TEXT("on patch 5"), Rig.Gull->GetState().Patch, 5);
	TestTrue(TEXT("in the middle of it"), FVector2D::Distance(Rig.Gull->GetGroundLocation(), FVector2D(Rig.Beach->GetFoodPatches()[5].Location.X, Rig.Beach->GetFoodPatches()[5].Location.Y)) < 5.0);
	TestTrue(TEXT("the patch is still whole as it lands"), Rig.Beach->GetFoodPatches()[5].Richness > Fresh - 0.02f);

	TestTrue(TEXT("it sets out after its meal"), Rig.RunUntil([&] { return Rig.IsPhase(CrabGull::EPhase::Stalking); }, 30.f));
	const float Left = Rig.Beach->GetFoodPatches()[5].Richness;
	TestNearlyEqual(TEXT("the patch lost the meal"), Fresh - Left, CrabGull::Tuning::EatRate * CrabGull::Tuning::LandEatSeconds, 0.02f);
	for (int32 Index = 0; Index < Rig.Beach->GetFoodPatches().Num(); ++Index)
	{
		if (Index != 5)
		{
			TestNearlyEqual(*FString::Printf(TEXT("patch %d is untouched"), Index), Rig.Beach->GetFoodPatches()[Index].Richness, Rig.Beach->GetFoodPatches()[Index].FullRichness, 1e-4f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullCatchTest, "CrabSim.Gull.ACrabThatIgnoresTheGullIsEatenAndTheRoundEnds", TestFlags)
bool FCrabGullCatchTest::RunTest(const FString& Parameters)
{
	FGullRig Rig(0.f, 0.f, /*bFreezeTide=*/false);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(0.6f);
	Rig.Gull->SpawnNow();
	TestTrue(TEXT("the gull comes and catches the crab standing where it is"), Rig.RunUntil([&] { return Rig.Crab->IsRoundOver(); }, 70.f));
	TestTrue(TEXT("the round is over"), Rig.Crab->IsRoundOver());
	TestTrue(TEXT("because it was eaten"), Rig.Crab->IsEaten());
	TestTrue(TEXT("the gull holds it"), Rig.IsPhase(CrabGull::EPhase::Caught));
	TestTrue(TEXT("within reach"), Rig.Gull->GetCrabDistance() <= CrabGull::Tuning::CatchRadius);
	TestTrue(TEXT("the tide stands still"), Rig.Beach->IsTideFrozen());
	TestNearlyEqual(TEXT("no best time for a round that was lost"), Rig.Crab->GetBestSeconds(), 0.f, 1e-6f);
	TestFalse(TEXT("and no new best"), Rig.Crab->IsNewBest());
	const float Time = Rig.Crab->GetRoundSeconds();
	TestTrue(*FString::Printf(TEXT("the round took as long as the gull did (%.1f s)"), Time), Time > CrabGull::EarliestCatchSeconds() && Time < 60.f);

	// Everything is held still, as under the panel of a won round.
	const FVector Where = Rig.Crab->GetActorLocation();
	Rig.Crab->SetMoveTarget(Where + FVector(600.f, 0.f, 0.f));
	TestFalse(TEXT("orders are ignored"), Rig.Crab->HasMoveTarget());
	Rig.World.TickSeconds(2.f);
	TestTrue(TEXT("and time stands still"), FMath::IsNearlyEqual(Rig.Crab->GetRoundSeconds(), Time, 0.001f));
	TestTrue(TEXT("the crab has not moved"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Where) < 1.f);

	// The button on the panel starts a new round.
	Rig.Controller->HandleLeftPress(*Rig.Crab, FVector2D(50.f, 50.f), GullView, nullptr);
	TestTrue(TEXT("a click away from the button does nothing"), Rig.Crab->IsRoundOver());
	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Controller->HandleLeftPress(*Rig.Crab, CrabHud::NewRoundButtonRect(GullView.X, GullView.Y).GetCenter(), GullView, nullptr);
	TestFalse(TEXT("the button starts it again"), Rig.Crab->IsRoundOver());
	TestFalse(TEXT("no longer eaten"), Rig.Crab->IsEaten());
	TestEqual(TEXT("molts are back to none"), Rig.Crab->GetMolts(), 0);
	TestNearlyEqual(TEXT("the clock starts over"), Rig.Crab->GetRoundSeconds(), 0.f, 1e-6f);
	TestFalse(TEXT("the tide runs again"), Rig.Beach->IsTideFrozen());
	Rig.World.TickSeconds(0.2f);
	TestFalse(TEXT("the gull is gone with the old round"), Rig.Gull->IsActive());
	TestEqual(TEXT("and starts counting again"), Rig.Gull->GetCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullEatenBestTest, "CrabSim.Gull.TheBestTimeIsKeptOnlyForFullyGrownRounds", TestFlags)
bool FCrabGullEatenBestTest::RunTest(const FString& Parameters)
{
	FGullRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.WinTheRound();
	if (!TestTrue(TEXT("won the round"), Rig.Crab->IsRoundOver()) || !TestFalse(TEXT("not eaten"), Rig.Crab->IsEaten()))
	{
		return false;
	}
	const float Best = Rig.Crab->GetBestSeconds();
	TestTrue(TEXT("a best time"), Best > 0.f);
	Rig.Crab->EatenByGull();
	TestFalse(TEXT("a crab that has won cannot then be eaten"), Rig.Crab->IsEaten());
	TestTrue(TEXT("and the win stands"), Rig.Crab->IsNewBest());

	Rig.Crab->StartNewRound();
	Rig.World.TickSeconds(1.f);
	Rig.Crab->EatenByGull();
	TestTrue(TEXT("eaten in the next round"), Rig.Crab->IsEaten() && Rig.Crab->IsRoundOver());
	TestNearlyEqual(TEXT("the best time from the round that was won is kept"), Rig.Crab->GetBestSeconds(), Best, 1e-4f);
	TestFalse(TEXT("and this round is not a best"), Rig.Crab->IsNewBest());
	TestEqual(TEXT("and the results panel counts the molts so far, none"), Rig.Crab->GetMolts(), 0);
	TestTrue(TEXT("a shorter eaten round does not replace it"), Rig.Crab->GetRoundSeconds() < Best);
	TestTrue(TEXT("Eaten twice is once"), (Rig.Crab->EatenByGull(), Rig.Crab->IsEaten()));

	FString Panel = CrabMolt::BestText(0.f);
	TestTrue(TEXT("with no win yet the panel says so, not 0:00"), Panel.Contains(TEXT("none")));
	TestEqual(TEXT("and with one it gives the time"), CrabMolt::BestText(125.f), FString(TEXT("2:05")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullBurrowAnswerTest, "CrabSim.Gull.PressingBurrowWhenItLandsIsNeverCaught", TestFlags)
bool FCrabGullBurrowAnswerTest::RunTest(const FString& Parameters)
{
	FGullRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Gull->SpawnNow();
	TestTrue(TEXT("the gull lands"), Rig.RunUntil([&] { return Rig.IsDown(); }, 30.f));
	TestTrue(TEXT("far off"), Rig.Gull->GetCrabDistance() > CrabGull::Tuning::LandMinDistance - 5.f);

	TestTrue(TEXT("the BURROW button works"), Rig.Crab->GoToBurrow());
	bool bDugIn = false;
	bool bLunged = false;
	Rig.RunUntil([&]
	{
		bDugIn = bDugIn || Rig.Crab->IsInBurrow();
		bLunged = bLunged || Rig.IsPhase(CrabGull::EPhase::Lunging);
		return Rig.Crab->IsRoundOver() || Rig.IsPhase(CrabGull::EPhase::Leaving) || Rig.IsPhase(CrabGull::EPhase::Absent);
	}, 60.f);
	TestFalse(TEXT("it was never caught"), Rig.Crab->IsEaten());
	TestTrue(TEXT("the crab dug in"), bDugIn);
	TestFalse(TEXT("the gull never lunged: the crab was walking, then hidden"), bLunged);
	TestTrue(TEXT("and the gull gave up on the burrow, five seconds after"), Rig.IsPhase(CrabGull::EPhase::Leaving) && Rig.Gull->GetState().LeaveReason == CrabGull::ELeave::BurrowWait);
	TestTrue(TEXT("the crab is still in its hole"), Rig.Crab->IsInBurrow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullDanceTest, "CrabSim.Gull.ADanceFromFarEnoughScaresTheGullAway", TestFlags)
bool FCrabGullDanceTest::RunTest(const FString& Parameters)
{
	FGullRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("the crab dances"), Rig.Crab->StartDance());
	Rig.Gull->SpawnNow();
	TestTrue(TEXT("the gull is scared off"), Rig.RunUntil([&] { return Rig.IsPhase(CrabGull::EPhase::Leaving) || Rig.Crab->IsRoundOver(); }, 70.f));
	TestFalse(TEXT("the crab was not eaten"), Rig.Crab->IsEaten());
	TestTrue(TEXT("it left because it was scared"), Rig.Gull->GetState().LeaveReason == CrabGull::ELeave::Scared);
	TestTrue(TEXT("with the crab still dancing"), Rig.Crab->IsDancing());
	TestTrue(TEXT("and it is not back for a minute"), Rig.Gull->GetState().QuietUntil >= Rig.Crab->GetRoundSeconds() + CrabGull::Tuning::ScareCooldown - 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullMovingTest, "CrabSim.Gull.AWalkingCrabInTheWorldIsNeverCaught", TestFlags)
bool FCrabGullMovingTest::RunTest(const FString& Parameters)
{
	FGullRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Gull->SpawnNow();
	// The crab walks to and fro across the flats, never standing for more than a moment, for well over the gull's whole hunt.
	Rig.RunUntil([&] { return Rig.IsPhase(CrabGull::EPhase::Stalking); }, 40.f);
	int32 Legs = 0;
	while (Legs < 24 && !Rig.Crab->IsRoundOver() && !Rig.IsPhase(CrabGull::EPhase::Leaving))
	{
		const float Side = (Legs % 2 == 0) ? 1.f : -1.f;
		Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(0.f, Side * 900.f, 0.f));
		Rig.RunUntil([&] { return !Rig.Crab->HasMoveTarget() || Rig.Crab->IsRoundOver(); }, 8.f);
		++Legs;
	}
	TestFalse(TEXT("never eaten"), Rig.Crab->IsEaten());
	TestTrue(TEXT("the gull gave up in the end"), Rig.IsPhase(CrabGull::EPhase::Leaving) && Rig.Gull->GetState().LeaveReason == CrabGull::ELeave::GaveUp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullSpawnTest, "CrabSim.Gull.AGullComesAfterTheFirstMinuteAndTheSwitchesWork", TestFlags)
bool FCrabGullSpawnTest::RunTest(const FString& Parameters)
{
	{
		FGullRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.World.TickSeconds(CrabGull::Tuning::FirstMinute - 3.f);
		TestFalse(TEXT("no gull in the first minute"), Rig.Gull->IsActive());
		TestTrue(TEXT("the first gull comes just after it"), Rig.RunUntil([&] { return Rig.Gull->IsActive(); }, 8.f));
		TestEqual(TEXT("one"), Rig.Gull->GetCount(), 1);
		TestTrue(TEXT("at that moment the round is a minute old"), Rig.Crab->GetRoundSeconds() >= CrabGull::Tuning::FirstMinute - 0.1f);
	}
	{
		FCVarScope Off(TEXT("CrabSim.Gulls"), 0);
		FGullRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Gull->SpawnNow();
		Rig.World.TickSeconds(65.f);
		TestFalse(TEXT("with CrabSim.Gulls 0 no gull comes, on request or by the rules"), Rig.Gull->IsActive());
	}
	{
		FCVarScope Force(TEXT("CrabSim.GullForce"), 1);
		FGullRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.World.TickSeconds(0.3f);
		TestTrue(TEXT("with CrabSim.GullForce 1 a gull comes at once"), Rig.IsPhase(CrabGull::EPhase::Circling));
	}
	return true;
}
