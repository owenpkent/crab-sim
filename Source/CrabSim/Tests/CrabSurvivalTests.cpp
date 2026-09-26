// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabSurvivalMath.h"

namespace UE::CrabSim::Tests::Survival
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSurvivalThresholdsTest, "CrabSim.Survival.ThresholdsAreOrdered", UE::CrabSim::Tests::Survival::TestFlags)
bool FCrabSurvivalThresholdsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("wading comes before surge"), CrabSurvival::WadeDepth < CrabSurvival::SurgeDepth);
	TestTrue(TEXT("surge comes before deep"), CrabSurvival::SurgeDepth < CrabSurvival::DeepDepth);
	TestTrue(TEXT("wade depth is positive"), CrabSurvival::WadeDepth > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSurvivalSpeedTest, "CrabSim.Survival.WaterSlowsTheCrab", UE::CrabSim::Tests::Survival::TestFlags)
bool FCrabSurvivalSpeedTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("dry is full speed"), CrabSurvival::SpeedScaleForDepth(0.f), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("ankle deep is full speed"), CrabSurvival::SpeedScaleForDepth(CrabSurvival::WadeDepth), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("out of depth is 60 percent"), CrabSurvival::SpeedScaleForDepth(CrabSurvival::DeepDepth), 0.6f, 1e-4f);
	TestNearlyEqual(TEXT("far out of depth stays at 60 percent"), CrabSurvival::SpeedScaleForDepth(5000.f), 0.6f, 1e-4f);
	TestNearlyEqual(TEXT("negative depth is full speed"), CrabSurvival::SpeedScaleForDepth(-40.f), 1.f, 1e-4f);

	float Previous = 1.f;
	for (float Depth = 0.f; Depth <= 200.f; Depth += 2.f)
	{
		const float Scale = CrabSurvival::SpeedScaleForDepth(Depth);
		TestTrue(*FString::Printf(TEXT("speed never rises with depth at %.0f"), Depth), Scale <= Previous + 1e-5f);
		TestTrue(*FString::Printf(TEXT("speed stays in range at %.0f"), Depth), Scale >= 0.6f - 1e-5f && Scale <= 1.f + 1e-5f);
		Previous = Scale;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSurvivalDrainTest, "CrabSim.Survival.GripDrainsOnlyInSurge", UE::CrabSim::Tests::Survival::TestFlags)
bool FCrabSurvivalDrainTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("no drain dry"), CrabSurvival::GripDrainPerSecond(0.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("no drain wading"), CrabSurvival::GripDrainPerSecond(CrabSurvival::WadeDepth), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("no drain exactly at the surge depth"), CrabSurvival::GripDrainPerSecond(CrabSurvival::SurgeDepth), 0.f, 1e-6f);
	TestTrue(TEXT("drain starts above the surge depth"), CrabSurvival::GripDrainPerSecond(CrabSurvival::SurgeDepth + 10.f) > 0.f);

	float Previous = 0.f;
	for (float Depth = 0.f; Depth <= 400.f; Depth += 5.f)
	{
		const float Drain = CrabSurvival::GripDrainPerSecond(Depth);
		TestTrue(*FString::Printf(TEXT("drain never falls as depth grows at %.0f"), Depth), Drain >= Previous - 1e-6f);
		Previous = Drain;
	}
	TestNearlyEqual(TEXT("drain is capped"), CrabSurvival::GripDrainPerSecond(10000.f), 0.24f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSurvivalRegenTest, "CrabSim.Survival.GripRecoversDryAndFastestInABurrow", UE::CrabSim::Tests::Survival::TestFlags)
bool FCrabSurvivalRegenTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("burrow recovers grip"), CrabSurvival::GripRegenPerSecond(0.f, true) > 0.f);
	TestTrue(TEXT("dry ground recovers grip"), CrabSurvival::GripRegenPerSecond(0.f, false) > 0.f);
	TestTrue(TEXT("a burrow is faster than open ground"), CrabSurvival::GripRegenPerSecond(0.f, true) > CrabSurvival::GripRegenPerSecond(0.f, false));
	TestNearlyEqual(TEXT("no recovery while wading in the open"), CrabSurvival::GripRegenPerSecond(CrabSurvival::WadeDepth + 1.f, false), 0.f, 1e-6f);
	TestTrue(TEXT("a burrow recovers even under water"), CrabSurvival::GripRegenPerSecond(200.f, true) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSurvivalPushTest, "CrabSim.Survival.SurgePushIsBoundedAndStartsAtSurge", UE::CrabSim::Tests::Survival::TestFlags)
bool FCrabSurvivalPushTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("no push while wading"), CrabSurvival::SurgePushSpeed(CrabSurvival::WadeDepth), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("no push at the surge depth"), CrabSurvival::SurgePushSpeed(CrabSurvival::SurgeDepth), 0.f, 1e-6f);
	TestTrue(TEXT("push in the surge"), CrabSurvival::SurgePushSpeed(CrabSurvival::SurgeDepth + 20.f) > 0.f);
	TestNearlyEqual(TEXT("push is capped"), CrabSurvival::SurgePushSpeed(10000.f), 120.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSurvivalOutcomeTest, "CrabSim.Survival.StayingInDeepWaterEventuallyCostsAllGrip", UE::CrabSim::Tests::Survival::TestFlags)
bool FCrabSurvivalOutcomeTest::RunTest(const FString& Parameters)
{
	// Integrate grip at a fixed depth, as the pawn does every frame.
	const float Depth = 120.f;
	float Grip = 1.f;
	float Seconds = 0.f;
	while (Grip > 0.f && Seconds < 120.f)
	{
		Grip += (CrabSurvival::GripRegenPerSecond(Depth, false) - CrabSurvival::GripDrainPerSecond(Depth)) * 0.02f;
		Seconds += 0.02f;
	}
	TestTrue(TEXT("a crab that stays out of its depth is swept away"), Grip <= 0.f);
	TestTrue(TEXT("but not instantly, so there is time to run"), Seconds > 4.f);
	TestTrue(TEXT("and not so slowly that the tide does not matter"), Seconds < 60.f);

	// A crab that stays dry keeps its grip.
	float Dry = 0.5f;
	for (int32 Frame = 0; Frame < 500; ++Frame)
	{
		Dry = FMath::Clamp(Dry + (CrabSurvival::GripRegenPerSecond(0.f, false) - CrabSurvival::GripDrainPerSecond(0.f)) * 0.02f, 0.f, 1.f);
	}
	TestNearlyEqual(TEXT("dry ground refills grip"), Dry, 1.f, 1e-4f);
	return true;
}
