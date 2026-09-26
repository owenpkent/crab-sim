// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabDigMath.h"
#include "CrabFoodMath.h"

namespace UE::CrabSim::Tests::DigMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A spot where digging is fine: fed, dry, in the open, well clear of everything. */
	inline CrabDig::FSpot GoodSpot()
	{
		CrabDig::FSpot Spot;
		Spot.Food = 0.6f;
		Spot.WaterDepth = 0.f;
		Spot.NearestBurrowDistance = 900.f;
		Spot.NearestPatchDistance = 900.f;
		return Spot;
	}
}

using namespace UE::CrabSim::Tests::DigMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigConstantsTest, "CrabSim.Dig.ConstantsAreSane", TestFlags)
bool FCrabDigConstantsTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("four seconds of standing still"), CrabDig::Duration, 4.f, 1e-6f);
	TestNearlyEqual(TEXT("it costs 0.30 food"), CrabDig::FoodCost, 0.30f, 1e-6f);
	TestNearlyEqual(TEXT("250 uu of space"), CrabDig::MinSpacing, 250.f, 1e-6f);
	TestEqual(TEXT("three dug burrows at most"), CrabDig::MaxDug, 3);
	TestTrue(TEXT("the starting store is a little short of a burrow, so digging asks for some foraging first"), CrabFood::StartFood < CrabDig::FoodCost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigOkTest, "CrabSim.Dig.AFedCrabOnOpenDrySandMayDig", TestFlags)
bool FCrabDigOkTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a good spot is fine"), CrabDig::Evaluate(GoodSpot()), CrabDig::EResult::Ok);
	TestEqual(TEXT("the default spot has too little food, nothing else wrong"), CrabDig::Evaluate(CrabDig::FSpot()), CrabDig::EResult::NotEnoughFood);
	TestTrue(TEXT("and Ok has no reason text"), FCString::Strlen(CrabDig::ReasonText(CrabDig::EResult::Ok)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigFoodTest, "CrabSim.Dig.NeedsThirtyPercentFood", TestFlags)
bool FCrabDigFoodTest::RunTest(const FString& Parameters)
{
	CrabDig::FSpot Spot = GoodSpot();
	Spot.Food = CrabDig::FoodCost;
	TestEqual(TEXT("exactly enough food is enough"), CrabDig::Evaluate(Spot), CrabDig::EResult::Ok);
	Spot.Food = CrabDig::FoodCost - 0.01f;
	TestEqual(TEXT("a little short is refused"), CrabDig::Evaluate(Spot), CrabDig::EResult::NotEnoughFood);
	Spot.Food = 0.f;
	TestEqual(TEXT("an empty store is refused"), CrabDig::Evaluate(Spot), CrabDig::EResult::NotEnoughFood);
	Spot.Food = 1.f;
	TestEqual(TEXT("a full one is fine"), CrabDig::Evaluate(Spot), CrabDig::EResult::Ok);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigWetTest, "CrabSim.Dig.NeedsDrySandAndNoSurge", TestFlags)
bool FCrabDigWetTest::RunTest(const FString& Parameters)
{
	CrabDig::FSpot Spot = GoodSpot();
	Spot.WaterDepth = 0.5f;
	TestEqual(TEXT("a film of water is too wet"), CrabDig::Evaluate(Spot), CrabDig::EResult::Wet);
	Spot.WaterDepth = 80.f;
	TestEqual(TEXT("deep water is too wet"), CrabDig::Evaluate(Spot), CrabDig::EResult::Wet);
	Spot.WaterDepth = 0.f;
	Spot.bInSurge = true;
	TestEqual(TEXT("the surge is too wet even on a dry reading"), CrabDig::Evaluate(Spot), CrabDig::EResult::Wet);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigSpacingTest, "CrabSim.Dig.KeepsTwoFiftyFromBurrowsAndPatches", TestFlags)
bool FCrabDigSpacingTest::RunTest(const FString& Parameters)
{
	CrabDig::FSpot Spot = GoodSpot();
	Spot.NearestBurrowDistance = CrabDig::MinSpacing - 1.f;
	TestEqual(TEXT("just inside 250 of a burrow"), CrabDig::Evaluate(Spot), CrabDig::EResult::TooCloseToBurrow);
	Spot.NearestBurrowDistance = 0.f;
	TestEqual(TEXT("on top of a burrow"), CrabDig::Evaluate(Spot), CrabDig::EResult::TooCloseToBurrow);
	Spot.NearestBurrowDistance = CrabDig::MinSpacing;
	TestEqual(TEXT("exactly 250 from a burrow is fine"), CrabDig::Evaluate(Spot), CrabDig::EResult::Ok);

	Spot = GoodSpot();
	Spot.NearestPatchDistance = CrabDig::MinSpacing - 1.f;
	TestEqual(TEXT("just inside 250 of a food patch"), CrabDig::Evaluate(Spot), CrabDig::EResult::TooCloseToPatch);
	Spot.NearestPatchDistance = CrabDig::MinSpacing + 1.f;
	TestEqual(TEXT("just outside is fine"), CrabDig::Evaluate(Spot), CrabDig::EResult::Ok);

	Spot = GoodSpot();
	Spot.NearestBurrowDistance = BIG_NUMBER;
	Spot.NearestPatchDistance = BIG_NUMBER;
	TestEqual(TEXT("with nothing around at all"), CrabDig::Evaluate(Spot), CrabDig::EResult::Ok);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigLimitTest, "CrabSim.Dig.AtMostThreeDugBurrows", TestFlags)
bool FCrabDigLimitTest::RunTest(const FString& Parameters)
{
	CrabDig::FSpot Spot = GoodSpot();
	for (int32 Dug = 0; Dug < CrabDig::MaxDug; ++Dug)
	{
		Spot.DugCount = Dug;
		TestEqual(*FString::Printf(TEXT("with %d dug there is room"), Dug), CrabDig::Evaluate(Spot), CrabDig::EResult::Ok);
	}
	Spot.DugCount = CrabDig::MaxDug;
	TestEqual(TEXT("with three dug there is not"), CrabDig::Evaluate(Spot), CrabDig::EResult::TooManyDug);
	Spot.DugCount = CrabDig::MaxDug + 5;
	TestEqual(TEXT("nor with more"), CrabDig::Evaluate(Spot), CrabDig::EResult::TooManyDug);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigInBurrowTest, "CrabSim.Dig.CannotDigFromInsideABurrow", TestFlags)
bool FCrabDigInBurrowTest::RunTest(const FString& Parameters)
{
	CrabDig::FSpot Spot = GoodSpot();
	Spot.bInBurrow = true;
	TestEqual(TEXT("in a burrow"), CrabDig::Evaluate(Spot), CrabDig::EResult::InBurrow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigPriorityTest, "CrabSim.Dig.ReportsTheMostBasicReasonFirst", TestFlags)
bool FCrabDigPriorityTest::RunTest(const FString& Parameters)
{
	CrabDig::FSpot Everything;
	Everything.Food = 0.f;
	Everything.WaterDepth = 50.f;
	Everything.bInSurge = true;
	Everything.bInBurrow = true;
	Everything.DugCount = CrabDig::MaxDug;
	Everything.NearestBurrowDistance = 0.f;
	Everything.NearestPatchDistance = 0.f;
	TestEqual(TEXT("everything wrong: being in a burrow comes first"), CrabDig::Evaluate(Everything), CrabDig::EResult::InBurrow);
	Everything.bInBurrow = false;
	TestEqual(TEXT("then the limit"), CrabDig::Evaluate(Everything), CrabDig::EResult::TooManyDug);
	Everything.DugCount = 0;
	TestEqual(TEXT("then food"), CrabDig::Evaluate(Everything), CrabDig::EResult::NotEnoughFood);
	Everything.Food = 1.f;
	TestEqual(TEXT("then water"), CrabDig::Evaluate(Everything), CrabDig::EResult::Wet);
	Everything.WaterDepth = 0.f;
	Everything.bInSurge = false;
	TestEqual(TEXT("then a burrow's space"), CrabDig::Evaluate(Everything), CrabDig::EResult::TooCloseToBurrow);
	Everything.NearestBurrowDistance = 900.f;
	TestEqual(TEXT("then a patch's space"), CrabDig::Evaluate(Everything), CrabDig::EResult::TooCloseToPatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigReasonsTest, "CrabSim.Dig.EveryRefusalHasAReadableReason", TestFlags)
bool FCrabDigReasonsTest::RunTest(const FString& Parameters)
{
	const CrabDig::EResult Refusals[] = {
		CrabDig::EResult::NoGround, CrabDig::EResult::InBurrow, CrabDig::EResult::TooManyDug, CrabDig::EResult::NotEnoughFood,
		CrabDig::EResult::Wet, CrabDig::EResult::TooCloseToBurrow, CrabDig::EResult::TooCloseToPatch};
	TSet<FString> Seen;
	for (const CrabDig::EResult Refusal : Refusals)
	{
		const FString Text = CrabDig::ReasonText(Refusal);
		TestFalse(*FString::Printf(TEXT("refusal %d has a reason"), static_cast<int32>(Refusal)), Text.IsEmpty());
		TestFalse(*FString::Printf(TEXT("refusal %d has its own reason"), static_cast<int32>(Refusal)), Seen.Contains(Text));
		Seen.Add(Text);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigProgressTest, "CrabSim.Dig.ProgressRunsZeroToOneOverFourSeconds", TestFlags)
bool FCrabDigProgressTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("start"), CrabDig::Progress(0.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("halfway"), CrabDig::Progress(2.f), 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("done"), CrabDig::Progress(CrabDig::Duration), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("clamped after"), CrabDig::Progress(99.f), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("clamped before"), CrabDig::Progress(-1.f), 0.f, 1e-6f);
	return true;
}
