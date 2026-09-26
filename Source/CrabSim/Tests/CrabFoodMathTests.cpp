// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabFoodMath.h"

namespace UE::CrabSim::Tests::FoodMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodConstantsTest, "CrabSim.Food.ConstantsAreSane", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodConstantsTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("the crab starts a quarter full"), CrabFood::StartFood, 0.25f, 1e-6f);
	TestNearlyEqual(TEXT("a full patch feeds at 0.025 a second"), CrabFood::FeedRatePerSecond, 0.025f, 1e-6f);
	TestNearlyEqual(TEXT("hunger drains 0.004 a second"), CrabFood::DrainPerSecond, 0.004f, 1e-6f);
	TestTrue(TEXT("the soak line is shallow: ten centimetres"), CrabFood::SoakDepth > 0.f && CrabFood::SoakDepth <= 15.f);
	TestTrue(TEXT("a tide costs a real share of the store"), CrabFood::DrainPerSecond * 180.f > 0.5f && CrabFood::DrainPerSecond * 180.f < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodRateTest, "CrabSim.Food.FeedRateScalesWithRichness", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodRateTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("a full patch feeds at the full rate"), CrabFood::FeedRate(1.f), CrabFood::FeedRatePerSecond, 1e-6f);
	TestNearlyEqual(TEXT("a bare patch does not feed"), CrabFood::FeedRate(0.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("nor does one at the empty line"), CrabFood::FeedRate(CrabFood::EmptyRichness), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("richness above 1 is capped"), CrabFood::FeedRate(3.f), CrabFood::FeedRatePerSecond, 1e-6f);
	TestTrue(TEXT("a barely fed patch still feeds"), CrabFood::FeedRate(0.02f) > 0.f);
	TestNearlyEqual(TEXT("and never below the floor share"), CrabFood::FeedRate(0.02f), CrabFood::FeedRatePerSecond * CrabFood::FeedRateFloor, 0.003f);

	float Previous = 0.f;
	for (float Richness = 0.f; Richness <= 1.f; Richness += 0.05f)
	{
		const float Rate = CrabFood::FeedRate(Richness);
		TestTrue(*FString::Printf(TEXT("a richer patch never feeds slower at %.2f"), Richness), Rate >= Previous - 1e-6f);
		Previous = Rate;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodTransferTest, "CrabSim.Food.TransferIsCappedByPatchAndCrab", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodTransferTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("one second on a full patch"), CrabFood::FeedTransfer(1.f, 0.25f, 1.f), 0.025f, 1e-5f);
	TestNearlyEqual(TEXT("half a second gives half"), CrabFood::FeedTransfer(1.f, 0.25f, 0.5f), 0.0125f, 1e-5f);
	TestNearlyEqual(TEXT("no time gives nothing"), CrabFood::FeedTransfer(1.f, 0.25f, 0.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("negative time gives nothing"), CrabFood::FeedTransfer(1.f, 0.25f, -1.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("a bare patch gives nothing"), CrabFood::FeedTransfer(0.f, 0.25f, 5.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("a full crab takes nothing"), CrabFood::FeedTransfer(1.f, 1.f, 5.f), 0.f, 1e-6f);

	// A long step cannot take more than the patch holds, or more than the crab has room for.
	TestNearlyEqual(TEXT("capped by what the patch holds"), CrabFood::FeedTransfer(0.01f, 0.f, 60.f), 0.01f, 1e-6f);
	TestNearlyEqual(TEXT("capped by the room in the crab"), CrabFood::FeedTransfer(1.f, 0.95f, 60.f), 0.05f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodEmptiesTest, "CrabSim.Food.APatchRunsOutInFiniteTime", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodEmptiesTest::RunTest(const FString& Parameters)
{
	// Sift a full patch at 60 frames a second into a crab with room to spare, as the pawn does.
	float Richness = 1.f;
	float Gained = 0.f;
	float Seconds = 0.f;
	while (!CrabFood::IsEmpty(Richness) && Seconds < 120.f)
	{
		const float Moved = CrabFood::FeedTransfer(Richness, 0.f, 1.f / 60.f);
		Richness -= Moved;
		Gained += Moved;
		Seconds += 1.f / 60.f;
	}
	TestTrue(*FString::Printf(TEXT("the patch runs out (after %.1f s)"), Seconds), CrabFood::IsEmpty(Richness));
	TestTrue(TEXT("not instantly: a full patch is a long sift"), Seconds > 40.f);
	TestTrue(TEXT("and not so slowly that a person waits it out"), Seconds < 100.f);
	TestTrue(*FString::Printf(TEXT("almost all of it reached the crab (%.3f)"), Gained), Gained > 0.99f && Gained <= 1.f + 1e-4f);

	// A poorer patch gives less and runs out sooner.
	float PoorRichness = 0.4f;
	float PoorSeconds = 0.f;
	while (!CrabFood::IsEmpty(PoorRichness) && PoorSeconds < 120.f)
	{
		PoorRichness -= CrabFood::FeedTransfer(PoorRichness, 0.f, 1.f / 60.f);
		PoorSeconds += 1.f / 60.f;
	}
	TestTrue(*FString::Printf(TEXT("a poor patch runs out sooner (%.1f s against %.1f s)"), PoorSeconds, Seconds), PoorSeconds < Seconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodFullTest, "CrabSim.Food.FullIsABandJustUnderOne", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodFullTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("1 is full"), CrabFood::IsFull(1.f));
	TestTrue(TEXT("just under 1 is full"), CrabFood::IsFull(CrabFood::FullFood));
	TestFalse(TEXT("nine tenths is not"), CrabFood::IsFull(0.9f));
	TestFalse(TEXT("the start is not"), CrabFood::IsFull(CrabFood::StartFood));
	TestTrue(TEXT("the band is narrow"), CrabFood::FullFood > 0.95f && CrabFood::FullFood < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodFillsTest, "CrabSim.Food.TheCrabStopsAtFull", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodFillsTest::RunTest(const FString& Parameters)
{
	float Food = CrabFood::StartFood;
	float Richness = 1.f;
	float Seconds = 0.f;
	while (!CrabFood::IsFull(Food) && !CrabFood::IsEmpty(Richness) && Seconds < 120.f)
	{
		const float Moved = CrabFood::FeedTransfer(Richness, Food, 1.f / 60.f);
		Richness -= Moved;
		Food += Moved;
		Seconds += 1.f / 60.f;
	}
	TestTrue(*FString::Printf(TEXT("a full patch fills a quarter-full crab (after %.1f s)"), Seconds), CrabFood::IsFull(Food));
	TestTrue(TEXT("with some patch left"), Richness > 0.2f);
	TestTrue(TEXT("food never passes 1"), Food <= 1.f + 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodDrainTest, "CrabSim.Food.HungerDrainsSlowlyAndNeverBelowZero", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodDrainTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("a second costs 0.004"), CrabFood::FoodAfterDrain(0.5f, 1.f), 0.496f, 1e-5f);
	TestNearlyEqual(TEXT("no time costs nothing"), CrabFood::FoodAfterDrain(0.5f, 0.f), 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("a whole tide costs about 0.72"), CrabFood::FoodAfterDrain(1.f, 180.f), 0.28f, 1e-4f);
	TestNearlyEqual(TEXT("food stops at zero"), CrabFood::FoodAfterDrain(0.01f, 1000.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("negative time does not feed the crab"), CrabFood::FoodAfterDrain(0.5f, -10.f), 0.5f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodSoakTest, "CrabSim.Food.OnlyWaterOverTenCentimetresSoaksAPatch", UE::CrabSim::Tests::FoodMath::TestFlags)
bool FCrabFoodSoakTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("dry"), CrabFood::IsSoaked(0.f));
	TestFalse(TEXT("a damp film"), CrabFood::IsSoaked(5.f));
	TestFalse(TEXT("exactly at the line"), CrabFood::IsSoaked(CrabFood::SoakDepth));
	TestTrue(TEXT("just over"), CrabFood::IsSoaked(CrabFood::SoakDepth + 1.f));
	TestTrue(TEXT("deep"), CrabFood::IsSoaked(200.f));
	return true;
}
