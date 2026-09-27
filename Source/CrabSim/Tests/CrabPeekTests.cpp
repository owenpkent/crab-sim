// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabMoltMath.h"

using namespace UE::CrabSim::Tests;

namespace
{
	struct FPeekRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;

		FPeekRig()
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Crab = SpawnCrab(World, *Beach, -2450.f, 350.f);
				}
				if (Crab)
				{
					Settle(World);
					Beach->SetTideClock(LowTide);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Crab; }
		float HoleGround() const { return Beach->GetBurrows()[HighBurrow].Location.Z; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPeekShowsTest, "CrabSim.Peek.ACrabInABurrowShowsOverTheEdgeOfTheHole", TestFlags)
bool FCrabPeekShowsTest::RunTest(const FString& Parameters)
{
	FPeekRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("not in the open"), Rig.Crab->IsPeeking());
	TestTrue(TEXT("dug in"), Rig.Crab->EnterBurrow(HighBurrow));
	Rig.World.TickSeconds(0.2f);
	TestFalse(TEXT("the model is still sinking, so the peek waits"), Rig.Crab->GetPeekBlend() > 0.5f);

	Rig.World.TickSeconds(1.8f);
	TestTrue(TEXT("the model is out of sight"), Rig.Crab->GetBurrowSink() > 0.97f);
	TestTrue(*FString::Printf(TEXT("and the peek is up (%.2f)"), Rig.Crab->GetPeekBlend()), Rig.Crab->GetPeekBlend() > 0.95f);
	const float Rise = Rig.Crab->GetPeekTop() - Rig.HoleGround();
	TestTrue(*FString::Printf(TEXT("eye stalks and shell reach well over the sand (%.0f uu)"), Rise), Rise > 50.f && Rise < 150.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPeekGoesTest, "CrabSim.Peek.TheCrabDoesNotPeekOnceItIsOut", TestFlags)
bool FCrabPeekGoesTest::RunTest(const FString& Parameters)
{
	FPeekRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(2.f);
	TestTrue(TEXT("peeking"), Rig.Crab->IsPeeking());

	Rig.Crab->ExitBurrow();
	Rig.World.TickN(1, 1.f / 60.f);
	TestFalse(TEXT("out of the burrow the peek is gone at once, so the crab is never shown twice"), Rig.Crab->IsPeeking());
	Rig.World.TickSeconds(1.f);
	TestFalse(TEXT("and stays gone"), Rig.Crab->IsPeeking());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPeekMoltTest, "CrabSim.Peek.AMoltingCrabKeepsItsPoseAndPeeksAfterwardsBigger", TestFlags)
bool FCrabPeekMoltTest::RunTest(const FString& Parameters)
{
	FPeekRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(1.f);
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(2.f);
	const float TopBefore = Rig.Crab->GetPeekTop() - Rig.HoleGround();
	TestTrue(TEXT("peeking before the molt"), Rig.Crab->IsPeeking());

	TestTrue(TEXT("the molt starts"), Rig.Crab->StartMolt());
	Rig.World.TickSeconds(1.5f);
	TestFalse(TEXT("a molting crab is shown half out by the molt pose, not by the peek"), Rig.Crab->IsPeeking());
	TestTrue(*FString::Printf(TEXT("the molt pose is there (sink %.2f)"), Rig.Crab->GetBurrowSink()), Rig.Crab->GetBurrowSink() < 0.6f);

	Rig.World.TickSeconds(CrabMolt::Tuning::Duration);
	TestEqual(TEXT("the molt finished"), Rig.Crab->GetMolts(), 1);
	TestFalse(TEXT("just after it the crab stays on show in the molt pose, no peek yet"), Rig.Crab->GetPeekBlend() > 0.5f);
	Rig.World.TickSeconds(4.f);
	TestTrue(TEXT("then it sinks and the peek comes up"), Rig.Crab->GetPeekBlend() > 0.95f);
	const float TopAfter = Rig.Crab->GetPeekTop() - Rig.HoleGround();
	TestTrue(*FString::Printf(TEXT("bigger than before, as the crab is (%.1f against %.1f)"), TopAfter, TopBefore), TopAfter > TopBefore * 1.04f);
	return true;
}
