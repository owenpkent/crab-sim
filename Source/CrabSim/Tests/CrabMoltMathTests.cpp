// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabFoodMath.h"
#include "CrabMoltMath.h"

namespace UE::CrabSim::Tests::MoltMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A crab that may molt: in a burrow, fed, nothing under way. */
	inline CrabMolt::FState GoodState()
	{
		CrabMolt::FState State;
		State.Food = 0.9f;
		State.bInBurrow = true;
		return State;
	}
}

using namespace UE::CrabSim::Tests::MoltMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltConstantsTest, "CrabSim.Molt.ConstantsAreSane", TestFlags)
bool FCrabMoltConstantsTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("ten seconds in the burrow"), CrabMolt::Tuning::Duration, 10.f, 1e-6f);
	TestNearlyEqual(TEXT("it needs 0.80 food"), CrabMolt::Tuning::MinFood, 0.80f, 1e-6f);
	TestNearlyEqual(TEXT("and costs 0.80 food"), CrabMolt::Tuning::FoodCost, 0.80f, 1e-6f);
	TestEqual(TEXT("three molts win"), CrabMolt::Tuning::MoltsToWin, 3);
	TestNearlyEqual(TEXT("eight percent a molt"), CrabMolt::Tuning::GrowthPerMolt, 0.08f, 1e-6f);
	TestNearlyEqual(TEXT("soft for thirty seconds"), CrabMolt::Tuning::SoftDuration, 30.f, 1e-6f);
	TestNearlyEqual(TEXT("with at most half its grip"), CrabMolt::Tuning::SoftGripCap, 0.5f, 1e-6f);
	TestTrue(TEXT("a molt is affordable from the start of the begin rule: the store can pay for it"), CrabMolt::Tuning::MinFood >= CrabMolt::Tuning::FoodCost);
	TestTrue(TEXT("and a full crab has some left over"), CrabMolt::Tuning::FoodCost < 1.f);
	TestTrue(TEXT("the starting store is far short of a molt, so the crab has to forage"), CrabFood::StartFood < CrabMolt::Tuning::MinFood);
	TestTrue(TEXT("it takes a full crab most of a tide to feed a molt's worth from a patch"),
		CrabMolt::Tuning::FoodCost / CrabFood::FeedRatePerSecond > 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltAvailabilityTest, "CrabSim.Molt.NeedsABurrowAndEightyPercentFood", TestFlags)
bool FCrabMoltAvailabilityTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("in a burrow, fed: ok"), CrabMolt::Evaluate(GoodState()), CrabMolt::EResult::Ok);
	TestEqual(TEXT("the default state is not in a burrow"), CrabMolt::Evaluate(CrabMolt::FState()), CrabMolt::EResult::NotInBurrow);

	CrabMolt::FState State = GoodState();
	State.bInBurrow = false;
	TestEqual(TEXT("out in the open, however fed"), CrabMolt::Evaluate(State), CrabMolt::EResult::NotInBurrow);

	State = GoodState();
	State.Food = CrabMolt::Tuning::MinFood;
	TestEqual(TEXT("exactly enough food is enough"), CrabMolt::Evaluate(State), CrabMolt::EResult::Ok);
	State.Food = CrabMolt::Tuning::MinFood - 0.01f;
	TestEqual(TEXT("a little short is refused"), CrabMolt::Evaluate(State), CrabMolt::EResult::NotEnoughFood);
	State.Food = 0.f;
	TestEqual(TEXT("an empty store is refused"), CrabMolt::Evaluate(State), CrabMolt::EResult::NotEnoughFood);
	State.Food = 1.f;
	TestEqual(TEXT("a full one is fine"), CrabMolt::Evaluate(State), CrabMolt::EResult::Ok);

	State = GoodState();
	State.bInProgress = true;
	TestEqual(TEXT("one molt at a time"), CrabMolt::Evaluate(State), CrabMolt::EResult::InProgress);
	State = GoodState();
	State.bRoundOver = true;
	TestEqual(TEXT("nothing once the round is won"), CrabMolt::Evaluate(State), CrabMolt::EResult::RoundOver);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltPriorityTest, "CrabSim.Molt.ReportsTheMostBasicReasonFirst", TestFlags)
bool FCrabMoltPriorityTest::RunTest(const FString& Parameters)
{
	CrabMolt::FState State;
	State.bRoundOver = true;
	State.bInProgress = true;
	TestEqual(TEXT("a won round beats everything"), CrabMolt::Evaluate(State), CrabMolt::EResult::RoundOver);
	State.bRoundOver = false;
	TestEqual(TEXT("under way beats a missing burrow and food"), CrabMolt::Evaluate(State), CrabMolt::EResult::InProgress);
	State.bInProgress = false;
	TestEqual(TEXT("no burrow beats no food"), CrabMolt::Evaluate(State), CrabMolt::EResult::NotInBurrow);
	State.bInBurrow = true;
	TestEqual(TEXT("then food"), CrabMolt::Evaluate(State), CrabMolt::EResult::NotEnoughFood);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltReasonsTest, "CrabSim.Molt.RefusalsReadAsMessages", TestFlags)
bool FCrabMoltReasonsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("no burrow"), FString(CrabMolt::ReasonText(CrabMolt::EResult::NotInBurrow)), FString(TEXT("Molt needs a burrow")));
	TestEqual(TEXT("no food"), FString(CrabMolt::ReasonText(CrabMolt::EResult::NotEnoughFood)), FString(TEXT("Molt needs more food")));
	TestTrue(TEXT("ok has no reason"), FCString::Strlen(CrabMolt::ReasonText(CrabMolt::EResult::Ok)) == 0);
	TestTrue(TEXT("under way needs no words"), FCString::Strlen(CrabMolt::ReasonText(CrabMolt::EResult::InProgress)) == 0);
	TestTrue(TEXT("nor does a won round"), FCString::Strlen(CrabMolt::ReasonText(CrabMolt::EResult::RoundOver)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltCostTest, "CrabSim.Molt.CostsEightyPercentAndNeverGoesBelowZero", TestFlags)
bool FCrabMoltCostTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("a full store leaves a fifth"), CrabMolt::FoodAfterMolt(1.f), 0.2f, 1e-5f);
	TestNearlyEqual(TEXT("exactly the minimum leaves nothing"), CrabMolt::FoodAfterMolt(CrabMolt::Tuning::MinFood), 0.f, 1e-5f);
	TestNearlyEqual(TEXT("hunger during the molt cannot make it negative"), CrabMolt::FoodAfterMolt(0.75f), 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltProgressTest, "CrabSim.Molt.ProgressRunsZeroToOneOverTenSeconds", TestFlags)
bool FCrabMoltProgressTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("start"), CrabMolt::Progress(0.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("halfway at five seconds"), CrabMolt::Progress(5.f), 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("done at ten"), CrabMolt::Progress(10.f), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("and stays there"), CrabMolt::Progress(30.f), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("negative time is the start"), CrabMolt::Progress(-2.f), 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltGrowthTest, "CrabSim.Molt.GrowsEightPercentAMoltUpToACap", TestFlags)
bool FCrabMoltGrowthTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("unmolted is the starting size"), CrabMolt::GrowthScale(0), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("one molt"), CrabMolt::GrowthScale(1), 1.08f, 1e-5f);
	TestNearlyEqual(TEXT("two molts"), CrabMolt::GrowthScale(2), 1.16f, 1e-5f);
	TestNearlyEqual(TEXT("three molts"), CrabMolt::GrowthScale(3), 1.24f, 1e-5f);
	TestNearlyEqual(TEXT("more never grows past the cap"), CrabMolt::GrowthScale(10), CrabMolt::Tuning::MaxGrowthScale, 1e-6f);
	TestNearlyEqual(TEXT("or a silly number of them"), CrabMolt::GrowthScale(100000), CrabMolt::Tuning::MaxGrowthScale, 1e-6f);
	TestNearlyEqual(TEXT("a negative count is the starting size"), CrabMolt::GrowthScale(-4), 1.f, 1e-6f);
	TestTrue(TEXT("the cap is where the last molt of a round lands"), CrabMolt::Tuning::MaxGrowthScale >= CrabMolt::GrowthScale(CrabMolt::Tuning::MoltsToWin) - 1e-5f);
	float Previous = 0.f;
	for (int32 Molts = 0; Molts < 12; ++Molts)
	{
		const float Scale = CrabMolt::GrowthScale(Molts);
		TestTrue(*FString::Printf(TEXT("never smaller after molt %d"), Molts), Scale >= Previous);
		Previous = Scale;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltSoftGripTest, "CrabSim.Molt.ASoftCrabCannotHoldMoreThanHalfItsGrip", TestFlags)
bool FCrabMoltSoftGripTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("a hard crab can have all of it"), CrabMolt::GripCap(false), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("a soft one half"), CrabMolt::GripCap(true), 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("full grip, soft, is capped"), CrabMolt::ClampGrip(1.f, true), 0.5f, 1e-6f);
	TestNearlyEqual(TEXT("low grip, soft, is left alone"), CrabMolt::ClampGrip(0.3f, true), 0.3f, 1e-6f);
	TestNearlyEqual(TEXT("full grip, hard, is untouched"), CrabMolt::ClampGrip(1.f, false), 1.f, 1e-6f);
	TestNearlyEqual(TEXT("grip never goes below zero"), CrabMolt::ClampGrip(-0.2f, true), 0.f, 1e-6f);

	TestNearlyEqual(TEXT("thirty seconds of soft left after none"), CrabMolt::SoftAfter(30.f, 0.f), 30.f, 1e-6f);
	TestNearlyEqual(TEXT("twelve seconds later"), CrabMolt::SoftAfter(30.f, 12.f), 18.f, 1e-5f);
	TestNearlyEqual(TEXT("it ends and stays ended"), CrabMolt::SoftAfter(30.f, 45.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("time running backward does not extend it"), CrabMolt::SoftAfter(10.f, -5.f), 10.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltWinTest, "CrabSim.Molt.ThreeMoltsWinTheRound", TestFlags)
bool FCrabMoltWinTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("none"), CrabMolt::IsWon(0));
	TestFalse(TEXT("one"), CrabMolt::IsWon(1));
	TestFalse(TEXT("two"), CrabMolt::IsWon(2));
	TestTrue(TEXT("three"), CrabMolt::IsWon(3));
	TestTrue(TEXT("and more"), CrabMolt::IsWon(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltBestTimeTest, "CrabSim.Molt.TheBestTimeIsTheShortestWin", TestFlags)
bool FCrabMoltBestTimeTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("the first win is the best so far"), CrabMolt::BestAfter(0.f, 512.f), 512.f, 1e-4f);
	TestNearlyEqual(TEXT("a faster one replaces it"), CrabMolt::BestAfter(512.f, 470.f), 470.f, 1e-4f);
	TestNearlyEqual(TEXT("a slower one does not"), CrabMolt::BestAfter(470.f, 600.f), 470.f, 1e-4f);
	TestNearlyEqual(TEXT("a tie keeps it"), CrabMolt::BestAfter(470.f, 470.f), 470.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabMoltTimeTextTest, "CrabSim.Molt.TimesReadAsMinutesAndSeconds", TestFlags)
bool FCrabMoltTimeTextTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("zero"), CrabMolt::TimeText(0.f), FString(TEXT("0:00")));
	TestEqual(TEXT("under a minute"), CrabMolt::TimeText(7.9f), FString(TEXT("0:07")));
	TestEqual(TEXT("a minute and a bit"), CrabMolt::TimeText(65.f), FString(TEXT("1:05")));
	TestEqual(TEXT("nine and a half minutes"), CrabMolt::TimeText(572.f), FString(TEXT("9:32")));
	TestEqual(TEXT("past ten"), CrabMolt::TimeText(605.4f), FString(TEXT("10:05")));
	TestEqual(TEXT("negative is zero"), CrabMolt::TimeText(-3.f), FString(TEXT("0:00")));
	return true;
}
