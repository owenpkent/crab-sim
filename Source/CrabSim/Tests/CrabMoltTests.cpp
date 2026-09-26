// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabDigMath.h"
#include "CrabFoodMath.h"
#include "CrabHudMath.h"
#include "CrabMoltMath.h"
#include "CrabPlayerController.h"
#include "CrabSurvivalMath.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"

using namespace UE::CrabSim::Tests;

namespace
{
	const FVector2D MoltView(1280.f, 720.f);

	/** A beach, the crab standing on it, and the pointer controller (which does not possess the crab). */
	struct FMoltRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;
		ACrabPlayerController* Controller = nullptr;

		explicit FMoltRig(float X = 0.f, float Y = 0.f)
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
		FVector2D MoltButton() const { return CrabHud::MoltButtonRect(MoltView.X, MoltView.Y).GetCenter(); }

		/** Dug into the highest burrow, which never floods, with the store full. */
		bool DigInHigh()
		{
			Crab->SetFood(1.f);
			return Crab->EnterBurrow(HighBurrow);
		}

		/** A whole molt, start to finish, from a full store. Returns whether it counted. */
		bool MoltOnce()
		{
			const int32 Before = Crab->GetMolts();
			Crab->SetFood(1.f);
			if (!Crab->StartMolt())
			{
				return false;
			}
			World.TickSeconds(CrabMolt::Tuning::Duration + 0.3f);
			return Crab->GetMolts() == Before + 1;
		}

		int32 CountMeshParts() const
		{
			TArray<UStaticMeshComponent*> Parts;
			Beach->GetComponents<UStaticMeshComponent>(Parts);
			return Parts.Num();
		}
	};
}

// --- Beginning and finishing ---------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltBeginTest, "CrabSim.Molt.BeginsInABurrowAndPaysOnlyWhenItIsDone", TestFlags)
bool FCrabMoltBeginTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	TestTrue(TEXT("dug into the high burrow with a full store"), Rig.DigInHigh());
	TestEqual(TEXT("a molt is allowed"), Rig.Crab->CheckMolt(), CrabMolt::EResult::Ok);
	TestEqual(TEXT("none yet"), Rig.Crab->GetMolts(), 0);

	TestTrue(TEXT("the molt begins"), Rig.Crab->StartMolt());
	TestTrue(TEXT("and reports it"), Rig.Crab->IsMolting());
	TestTrue(TEXT("with a message"), Rig.Crab->GetMessage().Contains(TEXT("Molting")));
	TestTrue(TEXT("still in the burrow"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("no walk"), Rig.Crab->HasMoveTarget());
	TestEqual(TEXT("and not allowed again while it is under way"), Rig.Crab->CheckMolt(), CrabMolt::EResult::InProgress);

	Rig.World.TickSeconds(5.f);
	TestTrue(TEXT("still molting at five seconds"), Rig.Crab->IsMolting());
	TestNearlyEqual(TEXT("halfway"), Rig.Crab->GetMoltProgress(), 0.5f, 0.05f);
	TestNearlyEqual(TEXT("nothing paid yet"), Rig.Crab->GetFood(), 1.f - CrabFood::DrainPerSecond * 5.f, 0.005f);
	TestEqual(TEXT("no molt counted yet"), Rig.Crab->GetMolts(), 0);
	TestTrue(TEXT("a second start does not restart it"), Rig.Crab->StartMolt());
	TestNearlyEqual(TEXT("progress carried on"), Rig.Crab->GetMoltProgress(), 0.5f, 0.05f);

	Rig.World.TickSeconds(5.3f);
	TestFalse(TEXT("done"), Rig.Crab->IsMolting());
	TestEqual(TEXT("one molt"), Rig.Crab->GetMolts(), 1);
	TestNearlyEqual(TEXT("it cost 0.80 food"), Rig.Crab->GetFood(), 1.f - CrabMolt::Tuning::FoodCost - CrabFood::DrainPerSecond * 10.3f, 0.01f);
	TestTrue(TEXT("and the crab says so"), Rig.Crab->GetMessage().Contains(TEXT("Molted (1 of 3)")));
	TestTrue(TEXT("still in the burrow"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("the round goes on"), Rig.Crab->IsRoundOver());
	TestNearlyEqual(TEXT("its progress is back to nothing"), Rig.Crab->GetMoltProgress(), 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltGripTest, "CrabSim.Molt.RefillsGripToFull", TestFlags)
bool FCrabMoltGripTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	TestTrue(TEXT("molting"), Rig.Crab->StartMolt());
	// A burrow gives grip back at half a second. Knock it down just before the end, so only the molt itself can fill it.
	Rig.World.TickSeconds(CrabMolt::Tuning::Duration - 0.1f);
	Rig.Crab->SetGrip(0.05f);
	TestNearlyEqual(TEXT("grip is nearly gone"), Rig.Crab->GetGrip(), 0.05f, 1e-4f);
	Rig.World.TickSeconds(0.15f);
	TestEqual(TEXT("the molt is done"), Rig.Crab->GetMolts(), 1);
	TestNearlyEqual(TEXT("grip is full"), Rig.Crab->GetGrip(), 1.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltLookTest, "CrabSim.Molt.TheCrabGrowsEightPercentInLookOnly", TestFlags)
bool FCrabMoltLookTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	const float Radius = Rig.Crab->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Rig.Crab->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Forward = Rig.Crab->ForwardSpeed;
	const float Side = Rig.Crab->SideSpeed;
	TestNearlyEqual(TEXT("it starts at its own size"), Rig.Crab->GetShownGrowth(), 1.f, 1e-4f);

	TestTrue(TEXT("first molt"), Rig.MoltOnce());
	Rig.World.TickSeconds(3.f);
	TestNearlyEqual(TEXT("the target grew eight percent"), Rig.Crab->GetGrowthScale(), 1.08f, 1e-4f);
	TestNearlyEqual(TEXT("and the crab has swelled to it"), Rig.Crab->GetShownGrowth(), 1.08f, 0.005f);
	TestNearlyEqual(TEXT("the mesh carries the scale"), static_cast<float>(Rig.Crab->GetMesh()->GetRelativeScale3D().X), Rig.Crab->GetShownGrowth(), 0.01f);
	TestNearlyEqual(TEXT("in all three axes"), static_cast<float>(Rig.Crab->GetMesh()->GetRelativeScale3D().Z), static_cast<float>(Rig.Crab->GetMesh()->GetRelativeScale3D().X), 1e-4f);

	TestTrue(TEXT("second molt"), Rig.MoltOnce());
	Rig.World.TickSeconds(3.f);
	TestNearlyEqual(TEXT("two molts"), Rig.Crab->GetShownGrowth(), 1.16f, 0.005f);

	TestNearlyEqual(TEXT("the capsule is the same width"), Rig.Crab->GetCapsuleComponent()->GetScaledCapsuleRadius(), Radius, 1e-4f);
	TestNearlyEqual(TEXT("and height, so click radii and collision are unchanged"), Rig.Crab->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), HalfHeight, 1e-4f);
	TestNearlyEqual(TEXT("forward speed is unchanged"), Rig.Crab->ForwardSpeed, Forward, 1e-4f);
	TestNearlyEqual(TEXT("and side speed"), Rig.Crab->SideSpeed, Side, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltEasesTest, "CrabSim.Molt.TheCrabSwellsToItsNewSizeNotAtOnce", TestFlags)
bool FCrabMoltEasesTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	TestTrue(TEXT("molt"), Rig.MoltOnce());
	const float Just = Rig.Crab->GetShownGrowth();
	TestTrue(*FString::Printf(TEXT("just after it is not at the new size yet (%.3f)"), Just), Just > 1.f && Just < 1.075f);
	Rig.World.TickSeconds(3.f);
	TestNearlyEqual(TEXT("and a few seconds on it is"), Rig.Crab->GetShownGrowth(), 1.08f, 0.003f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltSettlesTest, "CrabSim.Molt.AMoltingCrabSettlesIntoTheBurrowButStaysInSight", TestFlags)
bool FCrabMoltSettlesTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	Rig.World.TickSeconds(1.5f);
	TestTrue(*FString::Printf(TEXT("a crab just dug in is out of sight (sink %.2f)"), Rig.Crab->GetBurrowSink()), Rig.Crab->GetBurrowSink() > 0.97f);

	Rig.Crab->StartMolt();
	Rig.World.TickSeconds(2.f);
	const float Sink = Rig.Crab->GetBurrowSink();
	TestTrue(*FString::Printf(TEXT("a molting crab is half out (sink %.2f)"), Sink), Sink > 0.2f && Sink < 0.6f);

	// It pulses: the mesh's scale swings about the resting size, and the counted growth is left alone.
	float Lowest = 10.f;
	float Highest = 0.f;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Rig.World.TickN(1, 1.f / 30.f);
		const float Scale = static_cast<float>(Rig.Crab->GetMesh()->GetRelativeScale3D().X);
		Lowest = FMath::Min(Lowest, Scale);
		Highest = FMath::Max(Highest, Scale);
	}
	TestTrue(*FString::Printf(TEXT("the crab pulses (%.3f to %.3f)"), Lowest, Highest), Highest - Lowest > 0.04f && Highest - Lowest < 0.12f);
	TestNearlyEqual(TEXT("the pulse leaves the counted growth alone"), Rig.Crab->GetGrowthScale(), 1.f, 1e-5f);
	TestNearlyEqual(TEXT("and the settled size"), Rig.Crab->GetShownGrowth(), 1.f, 1e-4f);

	Rig.World.TickSeconds(CrabMolt::Tuning::Duration - 4.f + 0.1f);
	TestEqual(TEXT("done"), Rig.Crab->GetMolts(), 1);
	Rig.World.TickSeconds(0.5f);
	TestTrue(*FString::Printf(TEXT("it stays in view a moment after, so the growth shows (sink %.2f)"), Rig.Crab->GetBurrowSink()), Rig.Crab->GetBurrowSink() < 0.6f);
	Rig.World.TickSeconds(3.f);
	TestTrue(TEXT("then goes out of sight"), Rig.Crab->GetBurrowSink() > 0.97f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltDugBurrowTest, "CrabSim.Molt.WorksInADugBurrowToo", TestFlags)
bool FCrabMoltDugBurrowTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-1800.f, -1200.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->SetFood(1.f);
	TestTrue(TEXT("dig a burrow"), Rig.Crab->StartDig());
	Rig.World.TickSeconds(CrabDig::Duration + 0.3f);
	if (!TestEqual(TEXT("it was dug"), Rig.Beach->GetDugBurrowCount(), 1))
	{
		return false;
	}
	const int32 Hole = Rig.Beach->GetBurrows().Num() - 1;
	TestTrue(TEXT("it is a dug one"), Rig.Beach->GetBurrows()[Hole].bDug);
	TestEqual(TEXT("out in the open, no molt"), Rig.Crab->CheckMolt(), CrabMolt::EResult::NotInBurrow);

	Rig.Crab->SetFood(1.f);
	TestTrue(TEXT("dig into it"), Rig.Crab->EnterBurrow(Hole));
	TestEqual(TEXT("now a molt is allowed"), Rig.Crab->CheckMolt(), CrabMolt::EResult::Ok);
	TestTrue(TEXT("and it counts"), Rig.MoltOnce());
	return true;
}

// --- Refusals -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltRefusalsTest, "CrabSim.Molt.EachRefusalHasItsOwnMessage", TestFlags)
bool FCrabMoltRefusalsTest::RunTest(const FString& Parameters)
{
	// Out in the open, fed.
	{
		FMoltRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(1.f);
		TestEqual(TEXT("not in a burrow"), Rig.Crab->CheckMolt(), CrabMolt::EResult::NotInBurrow);
		TestFalse(TEXT("no molt"), Rig.Crab->StartMolt());
		TestFalse(TEXT("nothing under way"), Rig.Crab->IsMolting());
		TestEqual(TEXT("the message says why"), Rig.Crab->GetMessage(), FString(TEXT("Molt needs a burrow")));
		TestNearlyEqual(TEXT("and nothing was spent"), Rig.Crab->GetFood(), 1.f, 1e-4f);
	}
	// In a burrow, hungry.
	{
		FMoltRig Rig(-2450.f, 350.f);
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Beach->SetTideClock(LowTide);
		Rig.Crab->EnterBurrow(HighBurrow);
		Rig.Crab->SetFood(CrabMolt::Tuning::MinFood - 0.05f);
		TestEqual(TEXT("not enough food"), Rig.Crab->CheckMolt(), CrabMolt::EResult::NotEnoughFood);
		TestFalse(TEXT("no molt"), Rig.Crab->StartMolt());
		TestEqual(TEXT("the message says why"), Rig.Crab->GetMessage(), FString(TEXT("Molt needs more food")));
		TestFalse(TEXT("nothing under way"), Rig.Crab->IsMolting());
		TestTrue(TEXT("still in the burrow"), Rig.Crab->IsInBurrow());

		Rig.Crab->SetFood(CrabMolt::Tuning::MinFood);
		TestTrue(TEXT("exactly enough is enough"), Rig.Crab->StartMolt());
	}
	// Neither: the burrow is the first thing it needs.
	{
		FMoltRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(0.f);
		TestFalse(TEXT("no molt"), Rig.Crab->StartMolt());
		TestEqual(TEXT("the burrow comes first"), Rig.Crab->GetMessage(), FString(TEXT("Molt needs a burrow")));
	}
	return true;
}

// --- Cancelling -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltLeaveTest, "CrabSim.Molt.LeavingTheBurrowCancelsItForFree", TestFlags)
bool FCrabMoltLeaveTest::RunTest(const FString& Parameters)
{
	auto MoltThenDo = [](TFunctionRef<void(FMoltRig&)> Leave) -> bool
	{
		FMoltRig Rig(-2450.f, 350.f);
		if (!Rig.IsValid())
		{
			return false;
		}
		Rig.Beach->SetTideClock(LowTide);
		Rig.DigInHigh();
		Rig.Crab->StartMolt();
		Rig.World.TickSeconds(4.f);
		if (!Rig.Crab->IsMolting())
		{
			return false;
		}
		const float FoodBefore = Rig.Crab->GetFood();
		Leave(Rig);
		const bool bCancelled = !Rig.Crab->IsMolting() && !Rig.Crab->IsInBurrow();
		Rig.World.TickSeconds(11.f);
		const bool bNothing = Rig.Crab->GetMolts() == 0 && !Rig.Crab->IsSoft();
		const bool bFree = Rig.Crab->GetFood() > FoodBefore - CrabFood::DrainPerSecond * 11.5f - 0.001f;
		return bCancelled && bNothing && bFree;
	};

	TestTrue(TEXT("a click on the ground cancels it"), MoltThenDo([](FMoltRig& Rig) { Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(0.f, 500.f)); }));
	TestTrue(TEXT("a click on another burrow cancels it"), MoltThenDo([](FMoltRig& Rig) { Rig.Controller->HandleClick(*Rig.Crab, Rig.Beach->GetBurrows()[1].Location); }));
	TestTrue(TEXT("a click on a food patch cancels it"), MoltThenDo([](FMoltRig& Rig) { Rig.Controller->HandleClick(*Rig.Crab, Rig.Beach->GetFoodPatches()[0].Location); }));
	TestTrue(TEXT("a dash cancels it"), MoltThenDo([](FMoltRig& Rig) { Rig.Crab->TryDash(Rig.Ground(500.f, 0.f)); }));
	TestTrue(TEXT("digging out cancels it"), MoltThenDo([](FMoltRig& Rig) { Rig.Crab->ExitBurrow(); }));

	// The click that leaves says so, and the log line is there for the live test.
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	Rig.Crab->StartMolt();
	Rig.World.TickSeconds(3.f);
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(0.f, 500.f));
	TestTrue(TEXT("the message says it was cancelled"), Rig.Crab->GetMessage().Contains(TEXT("cancel")));
	TestTrue(TEXT("and the crab is walking off"), Rig.Crab->HasMoveTarget());

	// Starting over later begins from nothing, not from where it was.
	Rig.World.TickSeconds(0.5f);
	Rig.Crab->SetActorLocation(FVector(-2450.f, 350.f, Rig.Beach->GetGroundHeight(-2450.f, 350.f) + 60.f), false, nullptr, ETeleportType::TeleportPhysics);
	Rig.Crab->ClearMoveTarget();
	Rig.Crab->SetFood(1.f);
	TestTrue(TEXT("dig in again"), Rig.Crab->EnterBurrow(HighBurrow));
	TestTrue(TEXT("and molt again"), Rig.Crab->StartMolt());
	TestNearlyEqual(TEXT("from the start"), Rig.Crab->GetMoltProgress(), 0.f, 1e-4f);
	Rig.World.TickSeconds(CrabMolt::Tuning::Duration + 0.3f);
	TestEqual(TEXT("and it takes the whole ten seconds"), Rig.Crab->GetMolts(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltFloodTest, "CrabSim.Molt.AFloodMidMoltCancelsItAndLeavesTheCrabSoft", TestFlags)
bool FCrabMoltFloodTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(1900.f, -900.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->SetFood(1.f);
	TestTrue(TEXT("dug into the lowest burrow at low tide"), Rig.Crab->EnterBurrow(LowBurrow));
	TestTrue(TEXT("molting"), Rig.Crab->StartMolt());
	Rig.World.TickSeconds(3.f);
	TestTrue(TEXT("safe so far"), Rig.Crab->IsMolting() && Rig.Crab->IsInBurrow());
	TestFalse(TEXT("not soft"), Rig.Crab->IsSoft());
	const float FoodBefore = Rig.Crab->GetFood();

	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickN(2, 1.f / 60.f);
	TestFalse(TEXT("flooded out"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("the molt is off"), Rig.Crab->IsMolting());
	TestEqual(TEXT("no molt counted"), Rig.Crab->GetMolts(), 0);
	TestNearlyEqual(TEXT("no food spent"), Rig.Crab->GetFood(), FoodBefore, 0.002f);
	TestTrue(TEXT("soft"), Rig.Crab->IsSoft());
	TestNearlyEqual(TEXT("for about thirty seconds"), Rig.Crab->GetSoftRemaining(), CrabMolt::Tuning::SoftDuration, 0.2f);
	TestTrue(TEXT("told so"), Rig.Crab->GetMessage().Contains(TEXT("Flooded")));
	TestTrue(TEXT("and that it is soft"), Rig.Crab->GetMessage().Contains(TEXT("mid-molt")));

	// The water goes again (and stays away: thirty seconds is long enough for the tide to turn). A soft crab's grip climbs back only as far as half.
	Rig.Beach->SetTideClock(LowTide);
	Rig.Beach->SetTideFrozen(true);
	Rig.World.TickSeconds(6.f);
	TestTrue(TEXT("still soft"), Rig.Crab->IsSoft());
	TestTrue(*FString::Printf(TEXT("grip is at most half (%.3f)"), Rig.Crab->GetGrip()), Rig.Crab->GetGrip() <= CrabMolt::Tuning::SoftGripCap + 1e-4f);
	TestTrue(*FString::Printf(TEXT("and it does climb back to the cap (%.3f)"), Rig.Crab->GetGrip()), Rig.Crab->GetGrip() > CrabMolt::Tuning::SoftGripCap - 0.02f);

	// It is over after thirty seconds, and grip is free to recover.
	Rig.World.TickSeconds(CrabMolt::Tuning::SoftDuration - 6.f - 0.5f);
	TestTrue(TEXT("soft a moment before it ends"), Rig.Crab->IsSoft());
	TestTrue(TEXT("still capped"), Rig.Crab->GetGrip() <= CrabMolt::Tuning::SoftGripCap + 1e-4f);
	Rig.World.TickSeconds(1.f);
	TestFalse(TEXT("firm again"), Rig.Crab->IsSoft());
	TestNearlyEqual(TEXT("with no soft time left"), Rig.Crab->GetSoftRemaining(), 0.f, 1e-6f);
	Rig.World.TickSeconds(3.f);
	TestTrue(*FString::Printf(TEXT("grip recovers past half (%.3f)"), Rig.Crab->GetGrip()), Rig.Crab->GetGrip() > 0.7f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltFloodNotMoltingTest, "CrabSim.Molt.AFloodThatCatchesTheCrabNotMoltingLeavesItFirm", TestFlags)
bool FCrabMoltFloodNotMoltingTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(1900.f, -900.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->SetFood(1.f);
	TestTrue(TEXT("dug into the lowest burrow at low tide"), Rig.Crab->EnterBurrow(LowBurrow));
	Rig.World.TickSeconds(1.f);
	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickN(2, 1.f / 60.f);
	TestFalse(TEXT("flooded out"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("but it was not molting, so it is not soft"), Rig.Crab->IsSoft());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltSoftGripFullTest, "CrabSim.Molt.SoftDropsGripToHalfAtOnceAndAMoltDoesNotGetAroundIt", TestFlags)
bool FCrabMoltSoftGripFullTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(1900.f, -900.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->SetFood(1.f);
	Rig.Crab->EnterBurrow(LowBurrow);
	Rig.Crab->StartMolt();
	Rig.World.TickSeconds(2.f);
	TestNearlyEqual(TEXT("grip is full going in"), Rig.Crab->GetGrip(), 1.f, 1e-3f);
	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickN(2, 1.f / 60.f);
	TestTrue(*FString::Printf(TEXT("soft cut it to half at once (%.3f)"), Rig.Crab->GetGrip()), Rig.Crab->GetGrip() <= CrabMolt::Tuning::SoftGripCap + 1e-4f);

	// Soft, into the safe burrow, and a molt there: it finishes, but grip stays under the cap until the soft time is up.
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->SetFood(1.f);
	Rig.Crab->EnterBurrow(HighBurrow);
	TestTrue(TEXT("molting while soft"), Rig.Crab->StartMolt());
	Rig.World.TickSeconds(CrabMolt::Tuning::Duration + 0.3f);
	TestEqual(TEXT("the molt counted"), Rig.Crab->GetMolts(), 1);
	TestTrue(*FString::Printf(TEXT("but grip is still capped while soft (%.3f, soft %.1f s left)"), Rig.Crab->GetGrip(), Rig.Crab->GetSoftRemaining()),
		!Rig.Crab->IsSoft() || Rig.Crab->GetGrip() <= CrabMolt::Tuning::SoftGripCap + 1e-4f);
	return true;
}

// --- The HUD's molt button ------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltClickTest, "CrabSim.Hud.ClickingTheMoltButtonMoltsAndDoesNotWalkOrLeave", TestFlags)
bool FCrabHudMoltClickTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	// The ground the cursor points at, behind the button, is somewhere a click would walk to, and would dig the crab out.
	const FVector Behind = Rig.Ground(0.f, 900.f);
	const FVector Start = Rig.Crab->GetActorLocation();

	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.MoltButton(), MoltView, &Behind);
	TestTrue(TEXT("the molt started"), Rig.Crab->IsMolting());
	TestTrue(TEXT("the crab is still in its burrow"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("and the click was not a walk"), Rig.Crab->HasMoveTarget());

	// The rest of the press, held over the button or dragged off it, is not a walk either.
	Rig.Controller->HandleHold(*Rig.Crab, Behind);
	TestFalse(TEXT("holding the button does not follow the ground behind it"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("and the molt goes on"), Rig.Crab->IsMolting());

	Rig.World.TickSeconds(CrabMolt::Tuning::Duration + 0.3f);
	TestEqual(TEXT("the molt finished"), Rig.Crab->GetMolts(), 1);
	TestTrue(TEXT("with the crab where it was"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Start) < 25.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltRefusedTest, "CrabSim.Hud.AGreyedMoltButtonStillSwallowsTheClick", TestFlags)
bool FCrabHudMoltRefusedTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(1.f);
	const FVector Behind = Rig.Ground(0.f, 900.f);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.MoltButton(), MoltView, &Behind);
	TestFalse(TEXT("no molt out in the open"), Rig.Crab->IsMolting());
	TestFalse(TEXT("and no walk either"), Rig.Crab->HasMoveTarget());
	TestEqual(TEXT("but the reason on the message line"), Rig.Crab->GetMessage(), FString(TEXT("Molt needs a burrow")));

	// In a burrow but hungry: a refused click must not dig the crab out.
	FMoltRig Hungry(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Hungry.IsValid()))
	{
		return false;
	}
	Hungry.Beach->SetTideClock(LowTide);
	Hungry.Crab->EnterBurrow(HighBurrow);
	Hungry.Crab->SetFood(0.3f);
	const FVector HungryBehind = Hungry.Ground(0.f, 900.f);
	Hungry.Controller->HandleLeftPress(*Hungry.Crab, Hungry.MoltButton(), MoltView, &HungryBehind);
	TestFalse(TEXT("no molt without food"), Hungry.Crab->IsMolting());
	TestTrue(TEXT("still in the burrow"), Hungry.Crab->IsInBurrow());
	TestEqual(TEXT("the reason is food"), Hungry.Crab->GetMessage(), FString(TEXT("Molt needs more food")));

	// With no ground under the cursor at all (it points at the sky) the button still works.
	Hungry.Crab->SetFood(1.f);
	Hungry.Controller->NotifyLeftButtonReleased();
	Hungry.Controller->HandleLeftPress(*Hungry.Crab, Hungry.MoltButton(), MoltView, nullptr);
	TestTrue(TEXT("the molt starts even with no ground point"), Hungry.Crab->IsMolting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltElsewhereTest, "CrabSim.Hud.AClickAnywhereElseLeavesTheBurrowAndCancelsTheMolt", TestFlags)
bool FCrabHudMoltElsewhereTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.MoltButton(), MoltView, nullptr);
	TestTrue(TEXT("molting"), Rig.Crab->IsMolting());
	Rig.World.TickSeconds(2.f);

	const FVector Ground = Rig.Ground(0.f, 500.f);
	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Controller->HandleLeftPress(*Rig.Crab, FVector2D(640.f, 360.f), MoltView, &Ground);
	TestFalse(TEXT("a click in the middle cancelled it"), Rig.Crab->IsMolting());
	TestFalse(TEXT("and dug the crab out"), Rig.Crab->IsInBurrow());
	TestTrue(TEXT("and walks"), Rig.Crab->HasMoveTarget());
	return true;
}

// --- The round --------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabRoundWinTest, "CrabSim.Round.ThreeMoltsEndTheRound", TestFlags)
bool FCrabRoundWinTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	TestFalse(TEXT("a new round is not over"), Rig.Crab->IsRoundOver());
	TestNearlyEqual(TEXT("no best time yet"), Rig.Crab->GetBestSeconds(), 0.f, 1e-6f);

	TestTrue(TEXT("first molt"), Rig.MoltOnce());
	TestFalse(TEXT("one is not enough"), Rig.Crab->IsRoundOver());
	TestTrue(TEXT("second molt"), Rig.MoltOnce());
	TestFalse(TEXT("nor two"), Rig.Crab->IsRoundOver());
	TestTrue(TEXT("and the message counts"), Rig.Crab->GetMessage().Contains(TEXT("Molted (2 of 3)")));
	TestTrue(TEXT("third molt"), Rig.MoltOnce());
	TestTrue(TEXT("three win it"), Rig.Crab->IsRoundOver());
	TestEqual(TEXT("with three molts"), Rig.Crab->GetMolts(), 3);
	TestTrue(TEXT("the last message counts them all"), Rig.Crab->GetMessage().Contains(TEXT("Molted (3 of 3)")));
	TestNearlyEqual(TEXT("and the crab has grown to the top size"), Rig.Crab->GetGrowthScale(), 1.24f, 1e-4f);

	const float Time = Rig.Crab->GetRoundSeconds();
	TestTrue(*FString::Printf(TEXT("the round took about thirty-one seconds (%.1f)"), Time), Time > 30.f && Time < 34.f);
	TestNearlyEqual(TEXT("that is the best time"), Rig.Crab->GetBestSeconds(), Time, 1e-3f);
	TestTrue(TEXT("and a new best"), Rig.Crab->IsNewBest());
	TestEqual(TEXT("no burrows were dug"), Rig.Crab->GetRoundDug(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabRoundFrozenTest, "CrabSim.Round.TheTideFeedingDiggingAndWalkingAreHeldStillOnceItIsWon", TestFlags)
bool FCrabRoundFrozenTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	for (int32 Molt = 0; Molt < 3; ++Molt)
	{
		Rig.MoltOnce();
	}
	if (!TestTrue(TEXT("round won"), Rig.Crab->IsRoundOver()))
	{
		return false;
	}

	// The tide holds still.
	TestTrue(TEXT("the tide is frozen"), Rig.Beach->IsTideFrozen());
	const float Clock = Rig.Beach->GetTideClock();
	const float Time = Rig.Crab->GetRoundSeconds();
	Rig.Crab->SetFood(0.5f);
	Rig.World.TickSeconds(5.f);
	TestNearlyEqual(TEXT("the tide clock did not move"), Rig.Beach->GetTideClock(), Clock, 1e-4f);
	TestNearlyEqual(TEXT("the round clock did not move"), Rig.Crab->GetRoundSeconds(), Time, 1e-4f);
	TestNearlyEqual(TEXT("hunger does not drain"), Rig.Crab->GetFood(), 0.5f, 1e-5f);

	// Nothing else starts.
	TestFalse(TEXT("no feeding"), Rig.Crab->StartFeeding(0));
	TestFalse(TEXT("no digging"), Rig.Crab->StartDig());
	TestFalse(TEXT("no digging even from the check"), Rig.Crab->IsDigging());
	Rig.Crab->SetFood(1.f);
	TestFalse(TEXT("no molting"), Rig.Crab->StartMolt());
	TestEqual(TEXT("and it says nothing"), Rig.Crab->CheckMolt(), CrabMolt::EResult::RoundOver);
	TestFalse(TEXT("no dance"), Rig.Crab->StartDance());
	TestFalse(TEXT("no dash"), Rig.Crab->TryDash(Rig.Ground(400.f, 0.f)));
	Rig.Crab->SetMoveTarget(Rig.Ground(400.f, 0.f));
	TestFalse(TEXT("no walk"), Rig.Crab->HasMoveTarget());

	// A click on the world does nothing: the crab stays dug in.
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(0.f, 500.f));
	TestTrue(TEXT("still in the burrow"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("still no walk"), Rig.Crab->HasMoveTarget());
	const FVector Ground = Rig.Ground(0.f, 900.f);
	Rig.Controller->HandleLeftPress(*Rig.Crab, FVector2D(640.f, 360.f), MoltView, &Ground);
	TestFalse(TEXT("a press on the ground does nothing"), Rig.Crab->HasMoveTarget());
	Rig.Controller->HandleLeftPress(*Rig.Crab, Rig.MoltButton(), MoltView, &Ground);
	TestEqual(TEXT("nor does the molt button"), Rig.Crab->GetMolts(), 3);
	TestFalse(TEXT("and it starts nothing"), Rig.Crab->IsMolting());
	TestTrue(TEXT("the crab is still dug in"), Rig.Crab->IsInBurrow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabRoundStatsTest, "CrabSim.Round.TheResultsCountTimeMoltsDugBurrowsAndFoodEaten", TestFlags)
bool FCrabRoundStatsTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(1150.f, -80.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Beach->SetTideFrozen(true);
	TestNearlyEqual(TEXT("nothing eaten yet"), Rig.Crab->GetRoundFoodEaten(), 0.f, 1e-6f);

	TestTrue(TEXT("feed on a patch"), Rig.Crab->StartFeeding(4));
	Rig.World.TickSeconds(6.f);
	Rig.Crab->StopFeeding();
	const float Eaten = Rig.Crab->GetRoundFoodEaten();
	TestTrue(*FString::Printf(TEXT("food eaten was counted (%.3f)"), Eaten), Eaten > 0.08f && Eaten < 0.2f);

	// Dig one burrow, on clear sand well away from the patch.
	Rig.Crab->SetActorLocation(FVector(-1800.f, -1200.f, Rig.Beach->GetGroundHeight(-1800.f, -1200.f) + 60.f), false, nullptr, ETeleportType::TeleportPhysics);
	Rig.World.TickSeconds(0.6f);
	Rig.Crab->SetFood(1.f);
	TestTrue(TEXT("dig"), Rig.Crab->StartDig());
	Rig.World.TickSeconds(CrabDig::Duration + 0.3f);
	TestEqual(TEXT("one burrow dug"), Rig.Crab->GetRoundDug(), 1);

	// Then three molts in the burrow it dug.
	const int32 Hole = Rig.Beach->GetBurrows().Num() - 1;
	Rig.Beach->SetTideFrozen(false);
	Rig.Crab->EnterBurrow(Hole);
	for (int32 Molt = 0; Molt < 3; ++Molt)
	{
		Rig.MoltOnce();
	}
	TestTrue(TEXT("won"), Rig.Crab->IsRoundOver());
	TestEqual(TEXT("molts"), Rig.Crab->GetMolts(), 3);
	TestEqual(TEXT("dug burrows"), Rig.Crab->GetRoundDug(), 1);
	TestNearlyEqual(TEXT("food eaten"), Rig.Crab->GetRoundFoodEaten(), Eaten, 1e-4f);
	TestTrue(TEXT("and time taken counts the whole round"), Rig.Crab->GetRoundSeconds() > 6.f + CrabDig::Duration + 30.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabRoundNewRoundTest, "CrabSim.Round.ANewRoundResetsTheWorld", TestFlags)
bool FCrabRoundNewRoundTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(1150.f, -80.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const int32 PartsAtStart = Rig.CountMeshParts();
	Rig.Beach->SetTideClock(LowTide);
	Rig.Beach->SetTideFrozen(true);

	// Eat a patch down, dig two burrows, wash the crab about a bit.
	Rig.Crab->StartFeeding(4);
	Rig.World.TickSeconds(5.f);
	Rig.Crab->StopFeeding();
	const float Eaten = Rig.Beach->GetFoodPatches()[4].FullRichness - Rig.Beach->GetFoodPatches()[4].Richness;
	TestTrue(*FString::Printf(TEXT("the patch is eaten into (%.3f)"), Eaten), Eaten > 0.05f);

	const FVector2D Spots[] = {FVector2D(-1800.f, -1200.f), FVector2D(-1800.f, 1300.f)};
	for (const FVector2D& Spot : Spots)
	{
		Rig.Crab->SetActorLocation(FVector(Spot.X, Spot.Y, Rig.Beach->GetGroundHeight(Spot.X, Spot.Y) + 60.f), false, nullptr, ETeleportType::TeleportPhysics);
		Rig.World.TickSeconds(0.6f);
		Rig.Crab->SetFood(1.f);
		Rig.Crab->StartDig();
		Rig.World.TickSeconds(CrabDig::Duration + 0.3f);
	}
	TestEqual(TEXT("two burrows dug"), Rig.Beach->GetDugBurrowCount(), 2);
	TestEqual(TEXT("six burrows on the beach"), Rig.Beach->GetBurrows().Num(), 6);
	TestTrue(TEXT("with their holes and rims drawn"), Rig.CountMeshParts() > PartsAtStart + 15);

	// Win in a dug burrow with the tide running, so there is a tide to reset.
	Rig.Beach->SetTideFrozen(false);
	Rig.Beach->SetTideClock(20.f);
	Rig.World.TickSeconds(1.f);
	Rig.Crab->EnterBurrow(5);
	for (int32 Molt = 0; Molt < 3; ++Molt)
	{
		if (!Rig.MoltOnce())
		{
			break;
		}
	}
	if (!TestTrue(TEXT("won the round"), Rig.Crab->IsRoundOver()))
	{
		return false;
	}
	const float FirstTime = Rig.Crab->GetRoundSeconds();
	TestTrue(TEXT("the crab is soaked in the stats"), Rig.Crab->GetRoundDug() == 2 && Rig.Crab->GetRoundFoodEaten() > 0.05f);
	Rig.Crab->SetActorRotation(FRotator(0.f, 137.f, 0.f));

	// The new round button on the results panel starts it.
	const FVector Ground = Rig.Ground(0.f, 900.f);
	const FVector2D Button = CrabHud::NewRoundButtonRect(MoltView.X, MoltView.Y).GetCenter();
	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, MoltView, &Ground);
	TestFalse(TEXT("the round is on again"), Rig.Crab->IsRoundOver());
	TestFalse(TEXT("and the click was not a walk"), Rig.Crab->HasMoveTarget());

	TestNearlyEqual(TEXT("the tide is back at low water"), Rig.Beach->GetTideClock(), 0.f, 1e-4f);
	TestFalse(TEXT("and running"), Rig.Beach->IsTideFrozen());
	TestNearlyEqual(TEXT("food is the starting store"), Rig.Crab->GetFood(), CrabFood::StartFood, 1e-4f);
	TestNearlyEqual(TEXT("grip is full"), Rig.Crab->GetGrip(), 1.f, 1e-4f);
	TestEqual(TEXT("no molts"), Rig.Crab->GetMolts(), 0);
	TestNearlyEqual(TEXT("the crab is its own size again"), Rig.Crab->GetShownGrowth(), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("and its growth target"), Rig.Crab->GetGrowthScale(), 1.f, 1e-4f);
	TestFalse(TEXT("out of the burrow"), Rig.Crab->IsInBurrow());
	TestFalse(TEXT("no molt under way"), Rig.Crab->IsMolting());
	TestFalse(TEXT("not soft"), Rig.Crab->IsSoft());
	TestNearlyEqual(TEXT("the round clock starts over"), Rig.Crab->GetRoundSeconds(), 0.f, 1e-4f);
	TestEqual(TEXT("no dug burrows counted"), Rig.Crab->GetRoundDug(), 0);
	TestNearlyEqual(TEXT("no food eaten counted"), Rig.Crab->GetRoundFoodEaten(), 0.f, 1e-6f);
	TestTrue(TEXT("the best time is kept"), Rig.Crab->GetBestSeconds() > 0.f);
	TestNearlyEqual(TEXT("as the round that set it"), Rig.Crab->GetBestSeconds(), FirstTime, 1e-3f);

	TestTrue(TEXT("the crab is back at the start"), FVector::Dist2D(Rig.Crab->GetActorLocation(), FVector::ZeroVector) < 5.f);
	TestNearlyEqual(TEXT("facing the sea"), static_cast<float>(FMath::Abs(FRotator::NormalizeAxis(Rig.Crab->GetActorRotation().Yaw))), 0.f, 1.f);
	TestEqual(TEXT("the dug burrows are gone"), Rig.Beach->GetDugBurrowCount(), 0);
	TestEqual(TEXT("and the beach's own four remain"), Rig.Beach->GetBurrows().Num(), 4);
	TestEqual(TEXT("in the places and with the indices they had"), Rig.Beach->FindBurrowNear(FVector(700.f, 760.f, 0.f), 50.f), 2);
	TestEqual(TEXT("their holes and rims are gone with them"), Rig.CountMeshParts(), PartsAtStart);
	for (int32 Index = 0; Index < Rig.Beach->GetFoodPatches().Num(); ++Index)
	{
		const FCrabFoodPatch& Patch = Rig.Beach->GetFoodPatches()[Index];
		TestNearlyEqual(*FString::Printf(TEXT("patch %d is full again"), Index), Patch.Richness, Patch.FullRichness, 1e-5f);
		TestFalse(*FString::Printf(TEXT("and not soaked (%d)"), Index), Patch.bSoaked);
	}

	// The world runs again: the tide moves, hunger drains, and the crab can act.
	Rig.World.TickSeconds(2.f);
	TestTrue(TEXT("the tide clock runs"), Rig.Beach->GetTideClock() > 1.5f);
	TestTrue(TEXT("hunger drains"), Rig.Crab->GetFood() < CrabFood::StartFood);
	Rig.Crab->SetMoveTarget(Rig.Ground(300.f, 0.f));
	TestTrue(TEXT("and the crab takes orders"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabRoundBestTimeTest, "CrabSim.Round.TheBestTimeIsKeptForTheSession", TestFlags)
bool FCrabRoundBestTimeTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	auto PlayRound = [&Rig](float ExtraWait) -> bool
	{
		Rig.Beach->SetTideClock(LowTide);
		Rig.Crab->SetFood(1.f);
		Rig.Crab->EnterBurrow(HighBurrow);
		Rig.World.TickSeconds(ExtraWait);
		for (int32 Molt = 0; Molt < 3; ++Molt)
		{
			if (!Rig.MoltOnce())
			{
				return false;
			}
		}
		return Rig.Crab->IsRoundOver();
	};
	const FVector Ground = Rig.Ground(0.f, 900.f);
	const FVector2D Button = CrabHud::NewRoundButtonRect(MoltView.X, MoltView.Y).GetCenter();

	TestTrue(TEXT("a first round"), PlayRound(4.f));
	const float First = Rig.Crab->GetRoundSeconds();
	TestTrue(TEXT("its time is a new best"), Rig.Crab->IsNewBest());
	TestNearlyEqual(TEXT("and is the best"), Rig.Crab->GetBestSeconds(), First, 1e-3f);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, MoltView, &Ground);
	Rig.Controller->NotifyLeftButtonReleased();
	TestTrue(TEXT("a slower second round"), PlayRound(20.f));
	TestTrue(TEXT("is slower"), Rig.Crab->GetRoundSeconds() > First + 10.f);
	TestFalse(TEXT("so not a new best"), Rig.Crab->IsNewBest());
	TestNearlyEqual(TEXT("and the best stays"), Rig.Crab->GetBestSeconds(), First, 1e-3f);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, MoltView, &Ground);
	Rig.Controller->NotifyLeftButtonReleased();
	TestTrue(TEXT("a quicker third round"), PlayRound(0.f));
	TestTrue(TEXT("is quicker"), Rig.Crab->GetRoundSeconds() < First - 2.f);
	TestTrue(TEXT("so a new best"), Rig.Crab->IsNewBest());
	TestNearlyEqual(TEXT("and it replaces it"), Rig.Crab->GetBestSeconds(), Rig.Crab->GetRoundSeconds(), 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabRoundButtonOnlyTest, "CrabSim.Round.OnlyTheNewRoundButtonRestartsAndOnlyOnAWonRound", TestFlags)
bool FCrabRoundButtonOnlyTest::RunTest(const FString& Parameters)
{
	FMoltRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Ground = Rig.Ground(0.f, 900.f);
	const FVector2D Button = CrabHud::NewRoundButtonRect(MoltView.X, MoltView.Y).GetCenter();

	// Mid-round the middle of the screen is the ground, not a button.
	Rig.Crab->SetFood(0.9f);
	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, MoltView, &Ground);
	TestTrue(TEXT("a mid-round click in the panel's place walks"), Rig.Crab->HasMoveTarget());
	TestNearlyEqual(TEXT("and nothing was reset"), Rig.Crab->GetFood(), 0.9f, 0.01f);
	Rig.Crab->ClearMoveTarget();

	Rig.Beach->SetTideClock(LowTide);
	Rig.DigInHigh();
	for (int32 Molt = 0; Molt < 3; ++Molt)
	{
		Rig.MoltOnce();
	}
	TestTrue(TEXT("won"), Rig.Crab->IsRoundOver());
	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Controller->HandleLeftPress(*Rig.Crab, FVector2D(100.f, 100.f), MoltView, &Ground);
	TestTrue(TEXT("a click off the button does not restart it"), Rig.Crab->IsRoundOver());
	Rig.Controller->HandleLeftPress(*Rig.Crab, CrabHud::ResultsPanelRect(MoltView.X, MoltView.Y).Min + FVector2D(30.f, 30.f), MoltView, &Ground);
	TestTrue(TEXT("nor does a click on the panel itself"), Rig.Crab->IsRoundOver());
	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, MoltView, nullptr);
	TestFalse(TEXT("the button does, even with no ground under the cursor"), Rig.Crab->IsRoundOver());
	return true;
}

// --- The test hooks -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltTestFoodTest, "CrabSim.Molt.TheTestFoodSwitchesAreOffByDefaultAndWorkWhenSet", TestFlags)
bool FCrabMoltTestFoodTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Start = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.StartFood"));
	IConsoleVariable* Floor = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.FoodFloor"));
	if (!TestNotNull(TEXT("CrabSim.StartFood exists"), Start) || !TestNotNull(TEXT("CrabSim.FoodFloor exists"), Floor))
	{
		return false;
	}
	TestTrue(TEXT("start food is off by default"), Start->GetFloat() < 0.f);
	TestTrue(TEXT("and so is the floor"), Floor->GetFloat() < 0.f);

	{
		FMoltRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.World.TickSeconds(1.f);
		TestTrue(TEXT("with the switches off the crab keeps its own food"), Rig.Crab->GetFood() < CrabFood::StartFood);
	}

	Start->Set(0.9f, ECVF_SetByCode);
	{
		FMoltRig Rig;
		if (TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			Rig.World.TickSeconds(0.5f);
			TestNearlyEqual(TEXT("start food puts the crab at that store"), Rig.Crab->GetFood(), 0.9f, 0.01f);
			Rig.World.TickSeconds(5.f);
			TestTrue(TEXT("once: hunger takes it down after"), Rig.Crab->GetFood() < 0.9f);
		}
	}
	Start->Set(-1.f, ECVF_SetByCode);

	Floor->Set(0.85f, ECVF_SetByCode);
	{
		FMoltRig Rig(-2450.f, 350.f);
		if (TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			Rig.Beach->SetTideClock(LowTide);
			Rig.Crab->EnterBurrow(HighBurrow);
			Rig.World.TickSeconds(0.5f);
			TestTrue(TEXT("the floor keeps the store up"), Rig.Crab->GetFood() >= 0.85f - 1e-4f);
			TestTrue(TEXT("so a molt can start"), Rig.Crab->StartMolt());
			Rig.World.TickSeconds(CrabMolt::Tuning::Duration + 0.5f);
			TestEqual(TEXT("and finish"), Rig.Crab->GetMolts(), 1);
			TestTrue(TEXT("and the store is back up for the next"), Rig.Crab->GetFood() >= 0.85f - 1e-4f);
			TestTrue(TEXT("which starts at once"), Rig.Crab->StartMolt());
		}
	}
	Floor->Set(-1.f, ECVF_SetByCode);
	return true;
}
