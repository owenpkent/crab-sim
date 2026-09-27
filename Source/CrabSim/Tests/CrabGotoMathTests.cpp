// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabGotoMath.h"

namespace UE::CrabSim::Tests::GotoMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	CrabGoto::FPatch Patch(float Distance, float Richness = 0.8f)
	{
		CrabGoto::FPatch Result;
		Result.Distance = Distance;
		Result.Richness = Richness;
		return Result;
	}

	CrabGoto::FBurrow Burrow(float Distance, float FloorZ)
	{
		CrabGoto::FBurrow Result;
		Result.Distance = Distance;
		Result.FloorZ = FloorZ;
		return Result;
	}
}

using namespace UE::CrabSim::Tests::GotoMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoPatchNearestTest, "CrabSim.Goto.TheFoodButtonPicksTheNearestPatch", TestFlags)
bool FCrabGotoPatchNearestTest::RunTest(const FString& Parameters)
{
	const TArray<CrabGoto::FPatch> Patches = {Patch(900.f), Patch(300.f), Patch(600.f)};
	TestEqual(TEXT("the nearest of three"), CrabGoto::PickPatch(Patches), 1);
	TestEqual(TEXT("with one patch, that one"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Patch(2500.f)}), 0);
	TestEqual(TEXT("the richest is not what it looks for: a poor near patch beats a rich far one"),
		CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Patch(1500.f, 1.f), Patch(400.f, 0.4f)}), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoPatchTieTest, "CrabSim.Goto.EqualPatchesGoToTheLowerIndex", TestFlags)
bool FCrabGotoPatchTieTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("two at the same distance: the first"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Patch(500.f), Patch(500.f)}), 0);
	TestEqual(TEXT("the first is bare: the second"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Patch(500.f, 0.f), Patch(500.f)}), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoPatchSkipTest, "CrabSim.Goto.BarePatchesSubmergedPatchesAndPatchesTheSeaTakesAreSkipped", TestFlags)
bool FCrabGotoPatchSkipTest::RunTest(const FString& Parameters)
{
	CrabGoto::FPatch Bare = Patch(100.f, 0.f);
	CrabGoto::FPatch Nearly = Patch(100.f, CrabFood::EmptyRichness);
	CrabGoto::FPatch Under = Patch(100.f);
	Under.WaterDepth = CrabFood::SoakDepth + 5.f;
	CrabGoto::FPatch Damp = Patch(100.f);
	Damp.WaterDepth = CrabFood::SoakDepth - 2.f;
	const CrabGoto::FPatch Far = Patch(2000.f);

	TestEqual(TEXT("a bare patch is skipped for a far one"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Bare, Far}), 1);
	TestEqual(TEXT("so is one that only counts as bare"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Nearly, Far}), 1);
	TestEqual(TEXT("a submerged one is skipped"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Under, Far}), 1);
	TestEqual(TEXT("a damp one under the soak depth is not"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Damp, Far}), 0);

	// 100 uu is under a second's walk. The sea must stay off it that long, plus the lead the crab wants to feed.
	CrabGoto::FPatch Late = Patch(100.f);
	Late.SecondsUntilSoaked = CrabGoto::ArrivalSeconds(100.f) + CrabGoto::Tuning::FeedLeadSeconds + 0.5f;
	CrabGoto::FPatch Early = Patch(100.f);
	Early.SecondsUntilSoaked = CrabGoto::ArrivalSeconds(100.f) + CrabGoto::Tuning::FeedLeadSeconds - 0.5f;
	TestEqual(TEXT("a patch that floods after the crab could feed is used"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Late, Far}), 0);
	TestEqual(TEXT("one that floods before is skipped"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Early, Far}), 1);

	// The walk takes longer the farther the patch is, so the same flood time can rule out a far patch and not a near one.
	CrabGoto::FPatch NearEnough = Patch(200.f);
	CrabGoto::FPatch TooFar = Patch(5000.f);
	NearEnough.SecondsUntilSoaked = TooFar.SecondsUntilSoaked = 20.f;
	TestTrue(TEXT("200 uu in 20 s is fine"), CrabGoto::IsPatchUsable(NearEnough));
	TestFalse(TEXT("5000 uu in 20 s is not"), CrabGoto::IsPatchUsable(TooFar));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoPatchNoneTest, "CrabSim.Goto.NoUsablePatchMeansNoPick", TestFlags)
bool FCrabGotoPatchNoneTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("no patches"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>()), static_cast<int32>(INDEX_NONE));
	CrabGoto::FPatch Under = Patch(100.f);
	Under.WaterDepth = 50.f;
	TestEqual(TEXT("all bare or submerged"), CrabGoto::PickPatch(TArray<CrabGoto::FPatch>{Patch(200.f, 0.f), Under}), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoFoodResultTest, "CrabSim.Goto.TheFoodButtonSaysWhyItIsGrey", TestFlags)
bool FCrabGotoFoodResultTest::RunTest(const FString& Parameters)
{
	const TArray<CrabGoto::FPatch> Patches = {Patch(900.f), Patch(300.f)};
	int32 Chosen = 123;
	TestEqual(TEXT("a hungry crab with patches: Ok"), CrabGoto::EvaluateFood(false, 0.4f, Patches, Chosen), CrabGoto::EFoodResult::Ok);
	TestEqual(TEXT("and it names the patch"), Chosen, 1);
	TestTrue(TEXT("Ok has no reason text"), FCString::Strlen(CrabGoto::ReasonText(CrabGoto::EFoodResult::Ok)) == 0);

	TestEqual(TEXT("a full crab has no use for food"), CrabGoto::EvaluateFood(false, CrabFood::FullFood, Patches, Chosen), CrabGoto::EFoodResult::NotHungry);
	TestEqual(TEXT("and names none"), Chosen, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no patch left"), CrabGoto::EvaluateFood(false, 0.4f, TArray<CrabGoto::FPatch>{Patch(100.f, 0.f)}, Chosen), CrabGoto::EFoodResult::NoPatch);
	TestEqual(TEXT("a won round beats everything"), CrabGoto::EvaluateFood(true, 0.4f, Patches, Chosen), CrabGoto::EFoodResult::RoundOver);
	TestEqual(TEXT("being full beats having no patch"), CrabGoto::EvaluateFood(false, 1.f, TArray<CrabGoto::FPatch>(), Chosen), CrabGoto::EFoodResult::NotHungry);
	TestTrue(TEXT("the reasons have words"), FCString::Strlen(CrabGoto::ReasonText(CrabGoto::EFoodResult::NoPatch)) > 0 && FCString::Strlen(CrabGoto::ReasonText(CrabGoto::EFoodResult::NotHungry)) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoBurrowSafestTest, "CrabSim.Goto.TheBurrowButtonPicksTheHighestFloorInRange", TestFlags)
bool FCrabGotoBurrowSafestTest::RunTest(const FString& Parameters)
{
	// Floors from high to low, the way the beach builds them, at growing distances.
	const TArray<CrabGoto::FBurrow> Burrows = {Burrow(3000.f, 120.f), Burrow(1800.f, 0.f), Burrow(900.f, -80.f), Burrow(400.f, -160.f)};
	TestEqual(TEXT("the highest floor, though it is the farthest"), CrabGoto::PickBurrow(Burrows), 0);

	const TArray<CrabGoto::FBurrow> TooFar = {Burrow(CrabGoto::Tuning::SafeRange + 500.f, 120.f), Burrow(1800.f, 0.f), Burrow(900.f, -80.f)};
	TestEqual(TEXT("out of range, the best floor within range"), CrabGoto::PickBurrow(TooFar), 1);

	const TArray<CrabGoto::FBurrow> AllFar = {Burrow(CrabGoto::Tuning::SafeRange + 900.f, 120.f), Burrow(CrabGoto::Tuning::SafeRange + 400.f, -80.f)};
	TestEqual(TEXT("none in range: the nearest, whatever its floor"), CrabGoto::PickBurrow(AllFar), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoBurrowTieTest, "CrabSim.Goto.LevelBurrowsGoToTheNearerAndThenTheLowerIndex", TestFlags)
bool FCrabGotoBurrowTieTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("floors within the tie band: the nearer"),
		CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{Burrow(2000.f, 100.f), Burrow(800.f, 100.f - CrabGoto::Tuning::FloorTie + 1.f)}), 1);
	TestEqual(TEXT("floors just outside it: the higher"),
		CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{Burrow(2000.f, 100.f), Burrow(800.f, 100.f - CrabGoto::Tuning::FloorTie - 1.f)}), 0);
	TestEqual(TEXT("same floor and distance: the first"), CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{Burrow(700.f, 40.f), Burrow(700.f, 40.f)}), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoBurrowSkipTest, "CrabSim.Goto.FloodedBurrowsAndOnesTheSeaTakesFirstAreSkipped", TestFlags)
bool FCrabGotoBurrowSkipTest::RunTest(const FString& Parameters)
{
	CrabGoto::FBurrow HighButFlooded = Burrow(1000.f, 120.f);
	HighButFlooded.bFlooded = true;
	const CrabGoto::FBurrow Low = Burrow(600.f, -160.f);
	TestEqual(TEXT("a flooded high burrow is skipped for a dry low one"), CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{HighButFlooded, Low}), 1);

	CrabGoto::FBurrow Doomed = Burrow(1000.f, 120.f);
	Doomed.SecondsUntilFlooded = CrabGoto::ArrivalSeconds(1000.f) + CrabGoto::Tuning::BurrowLeadSeconds - 0.5f;
	TestEqual(TEXT("one that floods before the crab is settled in is skipped"), CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{Doomed, Low}), 1);
	Doomed.SecondsUntilFlooded += 1.f;
	TestEqual(TEXT("and one that floods after is not"), CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{Doomed, Low}), 0);

	CrabGoto::FBurrow Flooded = Low;
	Flooded.bFlooded = true;
	TestEqual(TEXT("nothing dry: no pick"), CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>{HighButFlooded, Flooded}), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no burrows: no pick"), CrabGoto::PickBurrow(TArray<CrabGoto::FBurrow>()), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoBurrowResultTest, "CrabSim.Goto.TheBurrowButtonSaysWhyItIsGrey", TestFlags)
bool FCrabGotoBurrowResultTest::RunTest(const FString& Parameters)
{
	const TArray<CrabGoto::FBurrow> Burrows = {Burrow(3000.f, 120.f), Burrow(900.f, -80.f)};
	int32 Chosen = 123;
	TestEqual(TEXT("in the open with burrows: Ok"), CrabGoto::EvaluateBurrow(false, false, Burrows, Chosen), CrabGoto::EBurrowResult::Ok);
	TestEqual(TEXT("and it names the burrow"), Chosen, 0);
	TestEqual(TEXT("already in one"), CrabGoto::EvaluateBurrow(false, true, Burrows, Chosen), CrabGoto::EBurrowResult::InBurrow);
	TestEqual(TEXT("and names none"), Chosen, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("none reachable"), CrabGoto::EvaluateBurrow(false, false, TArray<CrabGoto::FBurrow>(), Chosen), CrabGoto::EBurrowResult::NoBurrow);
	TestEqual(TEXT("a won round beats everything"), CrabGoto::EvaluateBurrow(true, true, Burrows, Chosen), CrabGoto::EBurrowResult::RoundOver);
	TestTrue(TEXT("Ok has no reason text"), FCString::Strlen(CrabGoto::ReasonText(CrabGoto::EBurrowResult::Ok)) == 0);
	TestTrue(TEXT("the reasons have words"), FCString::Strlen(CrabGoto::ReasonText(CrabGoto::EBurrowResult::InBurrow)) > 0 && FCString::Strlen(CrabGoto::ReasonText(CrabGoto::EBurrowResult::NoBurrow)) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGotoArrivalTest, "CrabSim.Goto.TheWalkIsTimedBelowTheCrabsBestSpeed", TestFlags)
bool FCrabGotoArrivalTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the assumed speed is under the side speed (450)"), CrabGoto::Tuning::AssumedSpeed < 450.f);
	TestTrue(TEXT("and over the forward speed (250), or the estimate would be far too gloomy"), CrabGoto::Tuning::AssumedSpeed > 250.f);
	TestNearlyEqual(TEXT("700 uu is two seconds"), CrabGoto::ArrivalSeconds(700.f), 700.f / CrabGoto::Tuning::AssumedSpeed, 1e-4f);
	TestNearlyEqual(TEXT("and no distance takes no time"), CrabGoto::ArrivalSeconds(-5.f), 0.f, 1e-6f);
	return true;
}
