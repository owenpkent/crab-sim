// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTide.h"

namespace UE::CrabSim::Tests::Tide
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FCrabTideSettings Simple()
	{
		FCrabTideSettings S;
		S.LowLevel = 0.f;
		S.HighLevel = 100.f;
		S.Period = 100.f;
		S.SwellHeight = 0.f;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideEndpointsTest, "CrabSim.Tide.StartsLowAndPeaksAtHalfPeriod", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideEndpointsTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S = UE::CrabSim::Tests::Tide::Simple();
	TestNearlyEqual(TEXT("low at t=0"), S.TideLevelAt(0.f), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("mid at a quarter"), S.TideLevelAt(25.f), 50.f, 1e-3f);
	TestNearlyEqual(TEXT("high at half period"), S.TideLevelAt(50.f), 100.f, 1e-3f);
	TestNearlyEqual(TEXT("mid at three quarters"), S.TideLevelAt(75.f), 50.f, 1e-3f);
	TestNearlyEqual(TEXT("low again after one period"), S.TideLevelAt(100.f), 0.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideDefaultsTest, "CrabSim.Tide.DefaultsSpanTheConfiguredRange", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideDefaultsTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S;
	TestNearlyEqual(TEXT("default low"), S.TideLevelAt(0.f), S.LowLevel, 1e-3f);
	TestNearlyEqual(TEXT("default high"), S.TideLevelAt(0.5f * S.Period), S.HighLevel, 1e-3f);
	TestTrue(TEXT("low is below high"), S.LowLevel < S.HighLevel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideMonotonicTest, "CrabSim.Tide.RisesThenFalls", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideMonotonicTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S = UE::CrabSim::Tests::Tide::Simple();
	for (int32 Step = 0; Step < 50; ++Step)
	{
		const float T = static_cast<float>(Step);
		TestTrue(*FString::Printf(TEXT("rising at %d"), Step), S.TideLevelAt(T + 1.f) >= S.TideLevelAt(T) - 1e-4f);
		TestTrue(*FString::Printf(TEXT("falling at %d"), Step + 50), S.TideLevelAt(T + 51.f) <= S.TideLevelAt(T + 50.f) + 1e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTidePeriodicTest, "CrabSim.Tide.RepeatsEveryPeriod", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTidePeriodicTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S;
	for (float T = 0.f; T < S.Period; T += 13.7f)
	{
		TestNearlyEqual(*FString::Printf(TEXT("tide repeats at %.1f"), T), S.TideLevelAt(T + 3.f * S.Period), S.TideLevelAt(T), 0.05f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideDirectionTest, "CrabSim.Tide.DirectionAndTimeToTurn", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideDirectionTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S = UE::CrabSim::Tests::Tide::Simple();
	TestTrue(TEXT("rising a quarter in"), S.IsRisingAt(25.f));
	TestFalse(TEXT("falling three quarters in"), S.IsRisingAt(75.f));
	TestTrue(TEXT("rising just after low"), S.IsRisingAt(1.f));
	TestFalse(TEXT("falling just after high"), S.IsRisingAt(51.f));

	// The turns themselves: low tide starts a rise, high tide starts a fall, and it repeats.
	TestTrue(TEXT("rising at exactly low tide"), S.IsRisingAt(0.f));
	TestFalse(TEXT("falling at exactly high tide"), S.IsRisingAt(50.f));
	TestTrue(TEXT("rising again at the start of the next cycle"), S.IsRisingAt(100.f));
	TestTrue(TEXT("rising just before high tide"), S.IsRisingAt(49.99f));
	TestFalse(TEXT("falling just before low tide"), S.IsRisingAt(99.99f));
	TestTrue(TEXT("negative time counts as the start"), S.IsRisingAt(-5.f));

	TestNearlyEqual(TEXT("50 s to the first high"), S.SecondsToTurnAt(0.f), 50.f, 1e-3f);
	TestNearlyEqual(TEXT("10 s to high"), S.SecondsToTurnAt(40.f), 10.f, 1e-3f);
	TestNearlyEqual(TEXT("25 s to low"), S.SecondsToTurnAt(75.f), 25.f, 1e-3f);
	TestNearlyEqual(TEXT("second cycle repeats"), S.SecondsToTurnAt(140.f), 10.f, 1e-3f);
	TestNearlyEqual(TEXT("negative time clamps"), S.SecondsToTurnAt(-5.f), 50.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideFractionTest, "CrabSim.Tide.FractionRunsZeroToOne", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideFractionTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S = UE::CrabSim::Tests::Tide::Simple();
	TestNearlyEqual(TEXT("empty at low"), S.FractionAt(0.f), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("half at mid"), S.FractionAt(25.f), 0.5f, 1e-3f);
	TestNearlyEqual(TEXT("full at high"), S.FractionAt(50.f), 1.f, 1e-3f);

	FCrabTideSettings Flat = S;
	Flat.HighLevel = Flat.LowLevel;
	const float Degenerate = Flat.FractionAt(10.f);
	TestTrue(TEXT("a zero range does not divide by zero"), FMath::IsFinite(Degenerate) && Degenerate >= 0.f && Degenerate <= 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideSwellTest, "CrabSim.Tide.SwellStaysWithinItsHeight", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideSwellTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S;
	float MaxOffset = 0.f;
	bool bSawAbove = false;
	bool bSawBelow = false;
	for (float T = 0.f; T < 2.f * S.Period; T += 0.25f)
	{
		const float Offset = S.SurfaceLevelAt(T) - S.TideLevelAt(T);
		MaxOffset = FMath::Max(MaxOffset, FMath::Abs(Offset));
		bSawAbove |= Offset > 1.f;
		bSawBelow |= Offset < -1.f;
	}
	TestTrue(TEXT("swell never exceeds its height"), MaxOffset <= S.SwellHeight + 1e-3f);
	TestTrue(TEXT("swell actually reaches its height"), MaxOffset > 0.95f * S.SwellHeight);
	TestTrue(TEXT("swell goes both above and below the tide"), bSawAbove && bSawBelow);

	FCrabTideSettings NoSwell = S;
	NoSwell.SwellHeight = 0.f;
	TestNearlyEqual(TEXT("no swell means surface equals tide"), NoSwell.SurfaceLevelAt(31.f), NoSwell.TideLevelAt(31.f), 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTideLookAheadTest, "CrabSim.Tide.FindsWhenTheSeaWillNextStandAboveALevel", UE::CrabSim::Tests::Tide::TestFlags)
bool FCrabTideLookAheadTest::RunTest(const FString& Parameters)
{
	const FCrabTideSettings S = UE::CrabSim::Tests::Tide::Simple();
	TestNearlyEqual(TEXT("already above: now"), S.SecondsUntilSurfaceAbove(50.f, 40.f, 60.f), 0.f, 1e-4f);

	// The level is 50 at t=25 on the way up. From t=0 the sea reaches just over 50 a moment after that.
	const float Rising = S.SecondsUntilSurfaceAbove(0.f, 50.f, 60.f, 0.5f);
	TestTrue(*FString::Printf(TEXT("rising to 50 takes about 25 s (%.1f)"), Rising), Rising >= 25.f && Rising <= 25.5f);
	TestNearlyEqual(TEXT("from ten seconds later it is ten seconds less"), S.SecondsUntilSurfaceAbove(10.f, 50.f, 60.f, 0.5f), Rising - 10.f, 0.51f);

	TestEqual(TEXT("a level it never reaches within the horizon"), S.SecondsUntilSurfaceAbove(0.f, 50.f, 20.f), static_cast<float>(BIG_NUMBER));
	TestEqual(TEXT("or above the high water mark at all"), S.SecondsUntilSurfaceAbove(0.f, 150.f, 500.f), static_cast<float>(BIG_NUMBER));
	TestEqual(TEXT("or on the ebb, when the level is only falling away from it"), S.SecondsUntilSurfaceAbove(60.f, 95.f, 20.f), static_cast<float>(BIG_NUMBER));

	// A swell makes the surface cross a level early: the first crossing counts.
	FCrabTideSettings Swelling = S;
	Swelling.SwellHeight = 10.f;
	Swelling.SwellPeriod = 7.f;
	TestTrue(TEXT("with a swell the sea reaches a level no later than without it"),
		Swelling.SecondsUntilSurfaceAbove(0.f, 50.f, 60.f, 0.25f) <= S.SecondsUntilSurfaceAbove(0.f, 50.f, 60.f, 0.25f) + 0.26f);
	return true;
}
