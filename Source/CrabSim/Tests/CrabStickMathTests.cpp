// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabStickMath.h"

namespace UE::CrabSim::Tests::StickMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Steps a stick detector at a fixed dt for a whole run, one Stick value per step. Returns every tap fired. */
	inline TArray<CrabStick::FTap> RunStick(CrabStick::FStickTapDetector& Detector, float DeltaSeconds, TArrayView<const FVector2D> Steps)
	{
		TArray<CrabStick::FTap> Taps;
		for (const FVector2D& Stick : Steps)
		{
			Detector.Update(DeltaSeconds, Stick, Taps);
		}
		return Taps;
	}

	/** A single clean stick tap: up to Peak and straight back to rest, over StepCount steps (the caller picks the dt). */
	inline TArray<FVector2D> QuickTap(const FVector2D& Peak, int32 StepCount = 4)
	{
		TArray<FVector2D> Steps;
		for (int32 Index = 0; Index < StepCount; ++Index)
		{
			const float Alpha = (Index + 1) / static_cast<float>(StepCount);
			// Up in the first half, back down in the second, so the peak is reached partway through.
			const float Shape = Alpha < 0.5f ? Alpha * 2.f : (1.f - Alpha) * 2.f;
			Steps.Add(Peak * Shape);
		}
		Steps.Add(FVector2D::ZeroVector);
		return Steps;
	}
}

using namespace UE::CrabSim::Tests::StickMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickTapFiresWithDirectionTest, "CrabSim.Stick.QuickReachIsATapWithTheDominantDirection", TestFlags)
bool FCrabStickTapFiresWithDirectionTest::RunTest(const FString& Parameters)
{
	CrabStick::FStickTapDetector Detector;
	const TArray<FVector2D> Steps = QuickTap(FVector2D(0.f, 0.9f));
	const TArray<CrabStick::FTap> Taps = RunStick(Detector, 0.02f, Steps);
	if (!TestEqual(TEXT("one tap"), Taps.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("up, the dominant axis at the peak"), Taps[0].Direction, CrabStick::ETapDir::Up);
	TestFalse(TEXT("not a click"), Taps[0].bFromClick);

	TArray<CrabStick::FTap> RightTaps;
	CrabStick::FStickTapDetector RightDetector;
	for (const FVector2D& Stick : QuickTap(FVector2D(0.8f, 0.1f)))
	{
		RightDetector.Update(0.02f, Stick, RightTaps);
	}
	if (!TestEqual(TEXT("one tap"), RightTaps.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("right, the bigger axis"), RightTaps[0].Direction, CrabStick::ETapDir::Right);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickHysteresisIgnoresTremorTest, "CrabSim.Stick.TremorWobblingRoundOuterNeverTaps", TestFlags)
bool FCrabStickHysteresisIgnoresTremorTest::RunTest(const FString& Parameters)
{
	CrabStick::FStickTapDetector Detector;
	// Wobbles either side of Outer (0.5) but never dips under Inner (0.2): a naive "complete at Outer" rule
	// would fire several taps here. Hysteresis means none does, because the reach never resolves.
	TArray<FVector2D> Steps;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Steps.Add(FVector2D(0.f, (Index % 2 == 0) ? 0.62f : 0.42f));
	}
	Steps.Add(FVector2D::ZeroVector); // now it dips under Inner, but the reach ran long: no tap even here.
	const TArray<CrabStick::FTap> Taps = RunStick(Detector, 0.05f, Steps); // 40 * 0.05 = 2 s, well past MaxTapSeconds
	TestEqual(TEXT("no tap, at any point in the wobble or after it"), Taps.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickLongHoldIsNotATapTest, "CrabSim.Stick.AHeldDeflectionIsNotATapEvenWhenReleased", TestFlags)
bool FCrabStickLongHoldIsNotATapTest::RunTest(const FString& Parameters)
{
	CrabStick::FStickTapDetector Detector;
	TArray<CrabStick::FTap> Taps;
	// Held past Outer for well over MaxTapSeconds (0.4 s default), as steering would, then let go.
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Detector.Update(0.05f, FVector2D(0.f, 0.9f), Taps); // 20 * 0.05 = 1 s
	}
	TestEqual(TEXT("nothing while held"), Taps.Num(), 0);
	Detector.Update(0.02f, FVector2D::ZeroVector, Taps);
	TestEqual(TEXT("and nothing on release: the reach ran too long"), Taps.Num(), 0);

	// A fresh, quick reach right after still taps normally: the detector recovers from the long hold.
	for (const FVector2D& Stick : QuickTap(FVector2D(0.f, -0.9f)))
	{
		Detector.Update(0.02f, Stick, Taps);
	}
	if (!TestEqual(TEXT("one tap from the clean reach that followed"), Taps.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("down"), Taps[0].Direction, CrabStick::ETapDir::Down);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickClickTapTest, "CrabSim.Stick.AQuickClickIsADirectionlessTap", TestFlags)
bool FCrabStickClickTapTest::RunTest(const FString& Parameters)
{
	CrabStick::FClickTapDetector Detector;
	TArray<CrabStick::FTap> Taps;
	Detector.Update(0.02f, true, Taps);
	Detector.Update(0.05f, true, Taps);
	Detector.Update(0.02f, false, Taps);
	if (!TestEqual(TEXT("one tap"), Taps.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("from a click"), Taps[0].bFromClick);
	TestEqual(TEXT("directionless"), Taps[0].Direction, CrabStick::ETapDir::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickLongClickIsNotATapTest, "CrabSim.Stick.AHeldClickIsNotATap", TestFlags)
bool FCrabStickLongClickIsNotATapTest::RunTest(const FString& Parameters)
{
	CrabStick::FClickTapDetector Detector;
	TArray<CrabStick::FTap> Taps;
	// The player's drag tool latches on a stationary press around half a second: a long press must never
	// register as a tap of its own, so it can never fight that gesture.
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Detector.Update(0.05f, true, Taps); // 30 * 0.05 = 1.5 s
	}
	Detector.Update(0.02f, false, Taps);
	TestEqual(TEXT("no tap"), Taps.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickMenuCursorWrapsTest, "CrabSim.Stick.MenuCursorWrapsAtBothEnds", TestFlags)
bool FCrabStickMenuCursorWrapsTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	TestEqual(TEXT("starts on MOVE"), Menu.GetCursor(), CrabStick::EMenuItem::Move);

	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Up, false}}), false);
	TestEqual(TEXT("up from the top wraps to the bottom"), Menu.GetCursor(), CrabStick::EMenuItem::Dash);
	Menu.Update(1.f, TArrayView<const CrabStick::FTap>(), false); // past GapSeconds: closes that chain before the next tap

	// Walk all the way back round with down taps, one gap-closed chain at a time so none of them chains to three.
	for (int32 Index = 0; Index < 7; ++Index)
	{
		Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
		Menu.Update(1.f, TArrayView<const CrabStick::FTap>(), false); // past GapSeconds: closes the chain
	}
	TestEqual(TEXT("seven downs from DASH lands back on DASH"), Menu.GetCursor(), CrabStick::EMenuItem::Dash);
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	TestEqual(TEXT("down from the bottom wraps to the top"), Menu.GetCursor(), CrabStick::EMenuItem::Move);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickGapExpiryClosesTheChainTest, "CrabSim.Stick.AGapClosesTheChainWithoutAThirdTap", TestFlags)
bool FCrabStickGapExpiryClosesTheChainTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	TestEqual(TEXT("one tap open"), Menu.GetChainCount(), 1);
	// Just under the gap: still open.
	Menu.Update(CrabStick::FTuning().GapSeconds - 0.05f, TArrayView<const CrabStick::FTap>(), false);
	TestEqual(TEXT("still open just under the gap"), Menu.GetChainCount(), 1);
	// Now past it: closes on its own, with no third tap and nothing selected.
	Menu.Update(0.1f, TArrayView<const CrabStick::FTap>(), false);
	TestEqual(TEXT("closed"), Menu.GetChainCount(), 0);
	TestEqual(TEXT("still MENU"), Menu.GetMode(), CrabStick::EMode::Menu);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickPendingSelectCommitsOnCloseTest, "CrabSim.Stick.APendingSelectCommitsWhenTheChainClosesShortOfThree", TestFlags)
bool FCrabStickPendingSelectCommitsOnCloseTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false); // cursor -> FOOD
	TestEqual(TEXT("cursor moved to FOOD"), Menu.GetCursor(), CrabStick::EMenuItem::Food);
	CrabStick::FStepResult Right = Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Right, false}}), false);
	TestFalse(TEXT("a select is not committed on the tap that makes it"), Right.bSelected);
	TestTrue(TEXT("it is held pending"), Menu.HasPendingSelect());

	const CrabStick::FStepResult Closed = Menu.Update(1.f, TArrayView<const CrabStick::FTap>(), false); // past the gap
	TestTrue(TEXT("committed once the chain closes"), Closed.bSelected);
	TestEqual(TEXT("FOOD, the item that was highlighted"), Closed.SelectedItem, CrabStick::EMenuItem::Food);
	TestFalse(TEXT("nothing left pending"), Menu.HasPendingSelect());
	TestEqual(TEXT("a non-MOVE select stays in MENU"), Menu.GetMode(), CrabStick::EMode::Menu);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickThirdTapTogglesAndUndoesTest, "CrabSim.Stick.AThirdTapTogglesTheModeAndRestoresTheCursor", TestFlags)
bool FCrabStickThirdTapTogglesAndUndoesTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false); // MOVE -> FOOD
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false); // FOOD -> BURROW
	TestEqual(TEXT("cursor moved twice"), Menu.GetCursor(), CrabStick::EMenuItem::Burrow);

	const CrabStick::FStepResult Third = Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	TestTrue(TEXT("the third tap is a triple tap"), Third.bTripleTapped);
	TestTrue(TEXT("and the mode changed"), Third.bModeChanged);
	TestEqual(TEXT("MENU to STEER"), Menu.GetMode(), CrabStick::EMode::Steer);
	TestEqual(TEXT("the cursor is undone to where it stood before the chain's first tap"),
		Menu.GetCursor(), CrabStick::EMenuItem::Move);
	TestEqual(TEXT("chain reset"), Menu.GetChainCount(), 0);

	// From STEER, a fresh triple tap returns to MENU. Taps there do not move a cursor: nothing to undo.
	CrabStick::FStepResult Last;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Last = Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::None, true}}), false);
	}
	TestTrue(TEXT("triple tap back"), Last.bTripleTapped);
	TestEqual(TEXT("STEER to MENU"), Menu.GetMode(), CrabStick::EMode::Menu);
	TestEqual(TEXT("cursor untouched by the steering taps"), Menu.GetCursor(), CrabStick::EMenuItem::Move);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickThirdTapDropsAPendingSelectTest, "CrabSim.Stick.AThirdTapDropsAPendingSelectInstead", TestFlags)
bool FCrabStickThirdTapDropsAPendingSelectTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Right, false}}), false); // selects MOVE, pending
	TestTrue(TEXT("pending"), Menu.HasPendingSelect());
	const CrabStick::FStepResult Second = Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Left, false}}), false);
	TestFalse(TEXT("still not committed"), Second.bSelected);
	const CrabStick::FStepResult Third = Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Left, false}}), false);
	TestTrue(TEXT("the third tap toggles"), Third.bTripleTapped);
	TestFalse(TEXT("and the select never fires"), Third.bSelected);
	TestFalse(TEXT("nothing left pending either"), Menu.HasPendingSelect());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickMoveSelectEntersSteerTest, "CrabSim.Stick.SelectingMoveEntersSteerAndOthersStayInMenu", TestFlags)
bool FCrabStickMoveSelectEntersSteerTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::None, true}}), false); // click selects MOVE, pending
	const CrabStick::FStepResult Closed = Menu.Update(1.f, TArrayView<const CrabStick::FTap>(), false); // gap closes: commits
	TestTrue(TEXT("selected"), Closed.bSelected);
	TestEqual(TEXT("MOVE"), Closed.SelectedItem, CrabStick::EMenuItem::Move);
	TestTrue(TEXT("mode changed"), Closed.bModeChanged);
	TestEqual(TEXT("into STEER"), Menu.GetMode(), CrabStick::EMode::Steer);

	// FOOD, by contrast, keeps the crab in MENU once committed.
	CrabStick::FMenuState Other;
	Other.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false); // MOVE -> FOOD
	Other.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Right, false}}), false); // select FOOD, pending
	const CrabStick::FStepResult ClosedOther = Other.Update(1.f, TArrayView<const CrabStick::FTap>(), false);
	TestTrue(TEXT("selected"), ClosedOther.bSelected);
	TestEqual(TEXT("FOOD"), ClosedOther.SelectedItem, CrabStick::EMenuItem::Food);
	TestFalse(TEXT("no mode change"), ClosedOther.bModeChanged);
	TestEqual(TEXT("still MENU"), Other.GetMode(), CrabStick::EMode::Menu);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickTapsInSteerJustCountTowardTheChainTest, "CrabSim.Stick.TapsInSteerDoNotTouchAnyMenuState", TestFlags)
bool FCrabStickTapsInSteerJustCountTowardTheChainTest::RunTest(const FString& Parameters)
{
	CrabStick::FMenuState Menu;
	// Force STEER by a triple tap, having moved the cursor first.
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	TestEqual(TEXT("in STEER now"), Menu.GetMode(), CrabStick::EMode::Steer);
	const CrabStick::EMenuItem CursorInSteer = Menu.GetCursor();

	// An up tap in STEER is just a tap toward the chain: no cursor to move.
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Up, false}}), false);
	TestEqual(TEXT("cursor untouched"), Menu.GetCursor(), CursorInSteer);
	TestFalse(TEXT("nothing pending either"), Menu.HasPendingSelect());
	TestEqual(TEXT("chain has one tap open"), Menu.GetChainCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickRoundOverShowsOnlyNewRoundTest, "CrabSim.Stick.RoundOverShowsOnlyNewRoundAndSnapsTheCursorToIt", TestFlags)
bool FCrabStickRoundOverShowsOnlyNewRoundTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("seven items while a round is on"), CrabStick::ActiveItemCount(false), 7);
	TestEqual(TEXT("just one, NEW_ROUND, once it is over"), CrabStick::ActiveItemCount(true), 1);
	TestEqual(TEXT("that one item is NEW_ROUND"), CrabStick::ActiveItemAt(true, 0), CrabStick::EMenuItem::NewRound);
	TestEqual(TEXT("up from it wraps to itself"), CrabStick::NextItem(CrabStick::EMenuItem::NewRound, true), CrabStick::EMenuItem::NewRound);
	TestEqual(TEXT("so does down"), CrabStick::PrevItem(CrabStick::EMenuItem::NewRound, true), CrabStick::EMenuItem::NewRound);

	CrabStick::FMenuState Menu;
	// Move the cursor off MOVE first, mid-chain, so the round ending has something to clean up.
	CrabStick::FStepResult Moved = Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::Down, false}}), false);
	TestEqual(TEXT("cursor moved to FOOD"), Menu.GetCursor(), CrabStick::EMenuItem::Food);
	TestFalse(TEXT("not a round-state change"), Moved.bModeChanged);

	const CrabStick::FStepResult Ended = Menu.Update(0.02f, TArrayView<const CrabStick::FTap>(), true);
	TestTrue(TEXT("the round ending is reported like a cursor and mode change"), Ended.bCursorMoved && Ended.bModeChanged);
	TestEqual(TEXT("the cursor snaps to NEW_ROUND"), Menu.GetCursor(), CrabStick::EMenuItem::NewRound);
	TestEqual(TEXT("MENU, even if STEER was showing"), Menu.GetMode(), CrabStick::EMode::Menu);
	TestEqual(TEXT("the open chain (one tap) was dropped"), Menu.GetChainCount(), 0);

	// Select it: a click, then the gap closes.
	Menu.Update(0.02f, MakeArrayView({CrabStick::FTap{CrabStick::ETapDir::None, true}}), true);
	const CrabStick::FStepResult Closed = Menu.Update(1.f, TArrayView<const CrabStick::FTap>(), true);
	TestTrue(TEXT("committed"), Closed.bSelected);
	TestEqual(TEXT("NEW_ROUND"), Closed.SelectedItem, CrabStick::EMenuItem::NewRound);

	// The round starts again: the cursor is put back on MOVE, the first item of the seven-item list.
	const CrabStick::FStepResult Started = Menu.Update(0.02f, TArrayView<const CrabStick::FTap>(), false);
	TestTrue(TEXT("a round-state change again"), Started.bCursorMoved && Started.bModeChanged);
	TestEqual(TEXT("cursor back on MOVE"), Menu.GetCursor(), CrabStick::EMenuItem::Move);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabStickSteerSpeedMultiplierTest, "CrabSim.Stick.SteerSpeedRampsFromMinSpeedToFullBetweenInnerAndFullDeflection", TestFlags)
bool FCrabStickSteerSpeedMultiplierTest::RunTest(const FString& Parameters)
{
	CrabStick::FTuning Tuning;
	Tuning.Inner = 0.2f;
	Tuning.MinSpeed = 0.35f;

	TestNearlyEqual(TEXT("at Inner: MinSpeed"), CrabStick::SteerSpeedMultiplier(Tuning.Inner, Tuning), 0.35f, 1e-5f);
	TestNearlyEqual(TEXT("at full deflection: 1"), CrabStick::SteerSpeedMultiplier(1.f, Tuning), 1.f, 1e-5f);
	TestNearlyEqual(TEXT("below Inner: still MinSpeed, clamped"), CrabStick::SteerSpeedMultiplier(0.f, Tuning), 0.35f, 1e-5f);
	TestNearlyEqual(TEXT("past full deflection: still 1, clamped"), CrabStick::SteerSpeedMultiplier(1.4f, Tuning), 1.f, 1e-5f);

	// Halfway between Inner and full: halfway between MinSpeed and 1.
	const float Half = Tuning.Inner + (1.f - Tuning.Inner) * 0.5f;
	TestNearlyEqual(TEXT("halfway"), CrabStick::SteerSpeedMultiplier(Half, Tuning), 0.35f + (1.f - 0.35f) * 0.5f, 1e-5f);

	// Monotonic: never a step down as the stick pushes further out.
	float Previous = -1.f;
	for (float Magnitude = 0.f; Magnitude <= 1.2f; Magnitude += 0.05f)
	{
		const float Value = CrabStick::SteerSpeedMultiplier(Magnitude, Tuning);
		TestTrue(*FString::Printf(TEXT("monotonic at magnitude %.2f"), Magnitude), Value >= Previous - 1e-6f);
		TestTrue(TEXT("never below MinSpeed"), Value >= Tuning.MinSpeed - 1e-5f);
		TestTrue(TEXT("never above 1"), Value <= 1.f + 1e-5f);
		Previous = Value;
	}
	return true;
}
