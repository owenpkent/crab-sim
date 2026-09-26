// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabMovementMath.h"

namespace UE::CrabSim::Tests::MovementMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabAlignmentTest, "CrabSim.MovementMath.SidewaysAlignment", UE::CrabSim::Tests::MovementMath::TestFlags)
bool FCrabAlignmentTest::RunTest(const FString& Parameters)
{
	// Facing +X. Travel along +X or -X is forward or backward, along +/-Y is a scuttle.
	TestNearlyEqual(TEXT("forward"), CrabMovementMath::SidewaysAlignment(0.f, 0.f), 0.f, 1e-4f);
	TestNearlyEqual(TEXT("backward"), CrabMovementMath::SidewaysAlignment(0.f, 180.f), 0.f, 1e-4f);
	TestNearlyEqual(TEXT("scuttle right"), CrabMovementMath::SidewaysAlignment(0.f, 90.f), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("scuttle left"), CrabMovementMath::SidewaysAlignment(0.f, -90.f), 1.f, 1e-4f);
	// Same answers when the crab faces another way.
	TestNearlyEqual(TEXT("rotated scuttle"), CrabMovementMath::SidewaysAlignment(90.f, 180.f), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("diagonal is between"), CrabMovementMath::SidewaysAlignment(0.f, 45.f), 0.7071f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSpeedTest, "CrabSim.MovementMath.SideIsFasterThanForward", UE::CrabSim::Tests::MovementMath::TestFlags)
bool FCrabSpeedTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("forward speed"), CrabMovementMath::SpeedForAlignment(0.f, 250.f, 450.f), 250.f, 1e-3f);
	TestNearlyEqual(TEXT("side speed"), CrabMovementMath::SpeedForAlignment(1.f, 250.f, 450.f), 450.f, 1e-3f);
	TestNearlyEqual(TEXT("halfway"), CrabMovementMath::SpeedForAlignment(0.5f, 250.f, 450.f), 350.f, 1e-3f);
	TestNearlyEqual(TEXT("clamps above 1"), CrabMovementMath::SpeedForAlignment(3.f, 250.f, 450.f), 450.f, 1e-3f);
	TestNearlyEqual(TEXT("clamps below 0"), CrabMovementMath::SpeedForAlignment(-2.f, 250.f, 450.f), 250.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabSideFacingTest, "CrabSim.MovementMath.SideFacingYaw", UE::CrabSim::Tests::MovementMath::TestFlags)
bool FCrabSideFacingTest::RunTest(const FString& Parameters)
{
	// Heading +Y (yaw 90): a side leads when the crab faces yaw 0 or yaw 180. Nearest to current wins.
	TestNearlyEqual(TEXT("already facing 0"), CrabMovementMath::SideFacingYaw(0.f, 90.f), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("prefers 0 from 40"), CrabMovementMath::SideFacingYaw(40.f, 90.f), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("prefers 180 from 140"), FMath::Abs(CrabMovementMath::SideFacingYaw(140.f, 90.f)), 180.f, 1e-3f);

	// Heading +X (yaw 0): a side leads at yaw +90 or -90.
	TestNearlyEqual(TEXT("from 10 goes to +90"), CrabMovementMath::SideFacingYaw(10.f, 0.f), 90.f, 1e-3f);
	TestNearlyEqual(TEXT("from -10 goes to -90"), CrabMovementMath::SideFacingYaw(-10.f, 0.f), -90.f, 1e-3f);

	// Never more than 90 degrees of turn, wherever the crab starts and wherever it heads.
	for (int32 Facing = -180; Facing <= 180; Facing += 15)
	{
		for (int32 Heading = -180; Heading <= 180; Heading += 15)
		{
			const float Goal = CrabMovementMath::SideFacingYaw(static_cast<float>(Facing), static_cast<float>(Heading));
			const float Turn = FMath::Abs(FRotator::NormalizeAxis(Goal - Facing));
			TestTrue(*FString::Printf(TEXT("turn <= 90 from %d heading %d"), Facing, Heading), Turn <= 90.f + 1e-3f);
			TestNearlyEqual(*FString::Printf(TEXT("goal is side-on from %d heading %d"), Facing, Heading),
				CrabMovementMath::SidewaysAlignment(Goal, static_cast<float>(Heading)), 1.f, 1e-3f);
		}
	}
	return true;
}
