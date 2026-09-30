// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabColony.h"
#include "CrabTestHelpers.h"

using namespace UE::CrabSim::Tests;

namespace
{
	/** A beach, the colony under its entrance burrow (0), and a crab standing on the ground, ready to dig in. */
	struct FColonyRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabColony* Colony = nullptr;
		ACrabPawn* Crab = nullptr;

		FColonyRig()
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Colony = World.SpawnActor<ACrabColony>();
					Crab = SpawnCrab(World, *Beach);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Colony && Crab; }

		/** Digs into the entrance burrow (0) and takes the crab down into the colony. */
		bool EnterAndGoDown()
		{
			if (!Crab->EnterBurrow(HighBurrow))
			{
				return false;
			}
			World.TickSeconds(1.f);
			return Crab->GoDown();
		}
	};

	/** Walks to the active dig face and digs until a pellet forms, or gives up after Seconds. */
	bool DigOnePellet(FColonyRig& Rig, float Seconds = 6.f)
	{
		if (!Rig.Crab->GoToDigFace())
		{
			return false;
		}
		const float Step = 1.f / 60.f;
		for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Step)
		{
			Rig.World.TickN(1, Step);
			if (Rig.Crab->IsCarryingPellet())
			{
				return true;
			}
		}
		return false;
	}
}

// --- Down and up ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyGoDownUpRoundTripTest, "CrabSim.Colony.GoDownAndUpRoundTrip", TestFlags)
bool FCrabColonyGoDownUpRoundTripTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("digs into the entrance burrow"), Rig.Crab->EnterBurrow(HighBurrow));
	Rig.World.TickSeconds(1.f);

	const float SurfaceYaw = Rig.Crab->GetCameraYaw();
	const float SurfacePitch = Rig.Crab->GetCameraPitch();

	TestTrue(TEXT("goes down"), Rig.Crab->GoDown());
	TestTrue(TEXT("underground"), Rig.Crab->IsUnderground());
	TestTrue(TEXT("still counts as in a burrow"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("still burrow 0"), Rig.Crab->GetCurrentBurrow(), 0);
	TestTrue(*FString::Printf(TEXT("starts at the mouth (%.1f, %.1f)"), Rig.Crab->GetUndergroundUV().X, Rig.Crab->GetUndergroundUV().Y),
		Rig.Crab->GetUndergroundUV().Size() < 1.f);
	TestNearlyEqual(TEXT("in the cutaway's own plane"), static_cast<float>(Rig.Crab->GetActorLocation().Y), static_cast<float>(Rig.Colony->GetActorLocation().Y), 1.f);
	TestNearlyEqual(TEXT("side-on camera: pitch 0"), Rig.Crab->GetCameraPitch(), 0.f, 0.5f);
	TestFalse(TEXT("cannot go down twice"), Rig.Crab->GoDown());

	Rig.Crab->GoUp();
	Rig.World.TickSeconds(1.f);
	TestFalse(TEXT("back on the surface"), Rig.Crab->IsUnderground());
	TestTrue(TEXT("still dug in"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("still burrow 0"), Rig.Crab->GetCurrentBurrow(), 0);
	TestTrue(*FString::Printf(TEXT("peeking again (%.2f)"), Rig.Crab->GetPeekBlend()), Rig.Crab->GetPeekBlend() > 0.5f);
	TestNearlyEqual(TEXT("camera yaw restored"), Rig.Crab->GetCameraYaw(), SurfaceYaw, 0.5f);
	TestNearlyEqual(TEXT("camera pitch restored"), Rig.Crab->GetCameraPitch(), SurfacePitch, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyGoDownRefusesTest, "CrabSim.Colony.GoDownRefusesUnlessDugInAndNotMolting", TestFlags)
bool FCrabColonyGoDownRefusesTest::RunTest(const FString& Parameters)
{
	{
		FColonyRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		TestFalse(TEXT("not in any burrow"), Rig.Crab->GoDown());
	}
	{
		FColonyRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		TestTrue(TEXT("dug into a different burrow"), Rig.Crab->EnterBurrow(LowBurrow));
		Rig.World.TickSeconds(1.f);
		TestFalse(TEXT("burrow 1 (not the colony's) refuses"), Rig.Crab->GoDown());
	}
	{
		FColonyRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->SetFood(1.f);
		TestTrue(TEXT("dug into the entrance"), Rig.Crab->EnterBurrow(HighBurrow));
		Rig.World.TickSeconds(1.f);
		TestTrue(TEXT("molting"), Rig.Crab->StartMolt());
		TestFalse(TEXT("cannot go down mid-molt"), Rig.Crab->GoDown());
	}
	return true;
}

// --- Getting about ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyUndergroundWalkTest, "CrabSim.Colony.UndergroundWalkArrivesAlongThePath", TestFlags)
bool FCrabColonyUndergroundWalkTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("down"), Rig.EnterAndGoDown());

	// The first junction, straight down the pre-dug shaft: an ordinary one-edge walk.
	const FVector2D Target = Rig.Colony->GetPlan().Nodes[1].Pos;
	Rig.Crab->SetUndergroundTarget(Target);
	TestTrue(TEXT("has a target"), Rig.Crab->HasUndergroundTarget());
	Rig.World.TickSeconds(6.f);
	TestFalse(TEXT("arrived, so the target cleared"), Rig.Crab->HasUndergroundTarget());
	const float Distance = static_cast<float>(FVector2D::Distance(Rig.Crab->GetUndergroundUV(), Target));
	TestTrue(*FString::Printf(TEXT("at the target (%.1f uu away)"), Distance), Distance < 5.f);
	const float WorldDistance = static_cast<float>(FVector::Dist(Rig.Crab->GetActorLocation(), Rig.Colony->PlanToWorld(Target)));
	TestTrue(*FString::Printf(TEXT("the actor sits there too (%.1f uu off)"), WorldDistance), WorldDistance < 5.f);
	return true;
}

// --- Digging and pellets ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyDigMakesAPelletTest, "CrabSim.Colony.DiggingMakesAPelletTheCrabHolds", TestFlags)
bool FCrabColonyDigMakesAPelletTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("down"), Rig.EnterAndGoDown());

	TestFalse(TEXT("not carrying yet"), Rig.Crab->IsCarryingPellet());
	TestTrue(TEXT("walks to the face and digs a pellet's worth"), DigOnePellet(Rig));
	TestTrue(TEXT("holds it"), Rig.Crab->IsCarryingPellet());
	TestFalse(TEXT("and digging pauses while it does"), Rig.Crab->IsUndergroundDigging());

	// Holding a pellet, digging is refused until it is dropped off.
	TestFalse(TEXT("cannot start digging again while carrying"), Rig.Crab->GoToDigFace());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyRollPelletToMoundTest, "CrabSim.Colony.RollingAPelletUpGrowsTheMoundAndCounts", TestFlags)
bool FCrabColonyRollPelletToMoundTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("down"), Rig.EnterAndGoDown());
	TestTrue(TEXT("digs a pellet"), DigOnePellet(Rig));
	TestEqual(TEXT("none rolled yet"), Rig.Crab->GetPelletsRolled(), 0);
	// The colony's own NPCs dig and roll pellets of their own in the background, so the mound's own count is
	// not exactly the player's to claim: PelletsRolled is. Only the rise across the player's own drop is checked.
	const int32 MoundBefore = Rig.Colony->GetState().PelletsOnMound;

	Rig.Crab->GoUp();
	Rig.World.TickSeconds(6.f);
	TestFalse(TEXT("carrying no more"), Rig.Crab->IsCarryingPellet());
	TestTrue(*FString::Printf(TEXT("the mound grew (%d to %d)"), MoundBefore, Rig.Colony->GetState().PelletsOnMound),
		Rig.Colony->GetState().PelletsOnMound > MoundBefore);
	TestEqual(TEXT("exactly one of them is the player's"), Rig.Crab->GetPelletsRolled(), 1);
	TestFalse(TEXT("and the crab surfaced"), Rig.Crab->IsUnderground());
	return true;
}

// --- Eating from the store ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyEatFromStoreTest, "CrabSim.Colony.EatFromStoreRaisesFoodAndLowersTheStore", TestFlags)
bool FCrabColonyEatFromStoreTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("down"), Rig.EnterAndGoDown());
	Rig.Crab->SetFood(0.5f);
	const float StoreBefore = Rig.Colony->GetState().Store;
	TestTrue(TEXT("some food in the store to begin with"), StoreBefore > 0.f);

	TestTrue(TEXT("walks to a pantry and eats"), Rig.Crab->GoToPantryAndEat());
	Rig.World.TickSeconds(4.f);
	TestTrue(*FString::Printf(TEXT("food rose (%.3f)"), Rig.Crab->GetFood()), Rig.Crab->GetFood() > 0.5f);
	TestTrue(*FString::Printf(TEXT("the store fell (%.3f from %.3f)"), Rig.Colony->GetState().Store, StoreBefore),
		Rig.Colony->GetState().Store < StoreBefore);
	return true;
}

// --- Dash and the round ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyDashRefusedUndergroundTest, "CrabSim.Colony.DashIsRefusedUnderground", TestFlags)
bool FCrabColonyDashRefusedUndergroundTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("down"), Rig.EnterAndGoDown());
	TestFalse(TEXT("no dash underground"), Rig.Crab->TryDash(Rig.Crab->GetActorLocation() + FVector(0.f, 500.f, 0.f)));
	TestFalse(TEXT("and no cooldown spent"), Rig.Crab->GetDashCooldownRemaining() > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyStartNewRoundSurfacesTest, "CrabSim.Colony.StartNewRoundComesUpFirstIfUnderground", TestFlags)
bool FCrabColonyStartNewRoundSurfacesTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("down"), Rig.EnterAndGoDown());
	TestTrue(TEXT("underground"), Rig.Crab->IsUnderground());

	Rig.Crab->StartNewRound();
	TestFalse(TEXT("back on the surface at once"), Rig.Crab->IsUnderground());
	Rig.World.TickSeconds(0.2f);
	TestEqual(TEXT("the colony reset too"), Rig.Colony->GetState().PelletsOnMound, 0);
	return true;
}
