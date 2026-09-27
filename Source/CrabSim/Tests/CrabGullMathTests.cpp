// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabGullMath.h"

namespace UE::CrabSim::Tests::GullMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	using CrabGull::EEvent;
	using CrabGull::EPhase;

	struct FEventAt
	{
		EEvent Event = EEvent::Circling;
		float Time = 0.f;
		FVector2D Gull = FVector2D::ZeroVector;
		float Distance = 0.f;
	};

	/** The state machine, a crab and a little world, ticked by hand. Every event is kept with the round time it came at. */
	struct FRig
	{
		CrabGull::FState State;
		CrabGull::FCrabView Crab;
		CrabGull::FEnvironment Env;
		CrabGull::FStep Out;
		TArray<CrabGull::FPatchSpot> Patches;
		TArray<FEventAt> Log;
		CrabGull::ESpawn Mode = CrabGull::ESpawn::Natural;
		float Dt = 0.05f;

		explicit FRig(int32 Seed = 3) : State(Seed) {}

		void Tick()
		{
			Env.Patches = Patches;
			Crab.RoundSeconds += Dt;
			CrabGull::Step(State, Crab, Env, Dt, Mode, Out);
			for (const EEvent Event : Out.Events)
			{
				Log.Add({Event, Crab.RoundSeconds, State.Location, CrabGull::DistanceToCrab(State, Crab)});
			}
		}

		void TickFor(float Seconds)
		{
			const float End = Crab.RoundSeconds + Seconds;
			while (Crab.RoundSeconds < End - 1e-4f)
			{
				Tick();
			}
		}

		const FEventAt* Find(EEvent Event, int32 From = 0) const
		{
			for (int32 Index = From; Index < Log.Num(); ++Index)
			{
				if (Log[Index].Event == Event)
				{
					return &Log[Index];
				}
			}
			return nullptr;
		}

		int32 Count(EEvent Event) const
		{
			int32 Total = 0;
			for (const FEventAt& Entry : Log)
			{
				Total += Entry.Event == Event ? 1 : 0;
			}
			return Total;
		}

		/** Ticks until the event comes (one that came before this call does not count) or Limit seconds pass. */
		bool RunUntil(EEvent Event, float Limit)
		{
			const int32 From = Log.Num();
			const float End = Crab.RoundSeconds + Limit;
			while (Crab.RoundSeconds < End && !Find(Event, From))
			{
				Tick();
			}
			return Find(Event, From) != nullptr;
		}

		/** A gull now, and the rules back to natural. */
		void SendGull()
		{
			Mode = CrabGull::ESpawn::Now;
			Tick();
			Mode = CrabGull::ESpawn::Natural;
		}

		/** Puts a gull on the hunt at a spot, as if it had come in and eaten. */
		void PutHunting(const FVector2D& Where)
		{
			State.Phase = EPhase::Stalking;
			State.PhaseSeconds = 0.f;
			State.Location = Where;
			State.Altitude = 0.f;
			State.StalkSeconds = 0.f;
			State.DanceSeconds = 0.f;
			State.BurrowSeconds = 0.f;
		}
	};

	/** The round time at which the first gull begins to circle for a crab whose state a script sets each tick, or -1. */
	float FirstCircling(float Limit, TFunctionRef<void(FRig&)> Script)
	{
		FRig Rig;
		while (Rig.Crab.RoundSeconds < Limit)
		{
			Script(Rig);
			Rig.Tick();
			if (const FEventAt* Found = Rig.Find(EEvent::Circling))
			{
				return Found->Time;
			}
		}
		return -1.f;
	}
}

using namespace UE::CrabSim::Tests::GullMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullFirstMinuteTest, "CrabSim.Gull.NoGullInTheFirstMinuteOfARound", TestFlags)
bool FCrabGullFirstMinuteTest::RunTest(const FString& Parameters)
{
	const float First = FirstCircling(120.f, [](FRig&) {});
	TestTrue(TEXT("a crab out in the open from the start gets its first gull"), First > 0.f);
	TestTrue(TEXT("but not before the minute is up"), First >= CrabGull::Tuning::FirstMinute - 0.01f);
	TestTrue(TEXT("and then at once, the wait outside being over"), First < CrabGull::Tuning::FirstMinute + 0.2f);

	FRig Rig;
	Rig.Crab.RoundSeconds = 0.f;
	Rig.TickFor(CrabGull::Tuning::FirstMinute - 1.f);
	TestEqual(TEXT("no circling event in that first minute"), Rig.Count(EEvent::Circling), 0);
	TestTrue(TEXT("and the gull is absent"), Rig.State.Phase == EPhase::Absent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullOutsideTest, "CrabSim.Gull.ACrabMustBeOutTwentyFiveSecondsFirst", TestFlags)
bool FCrabGullOutsideTest::RunTest(const FString& Parameters)
{
	const float Wait = CrabGull::Tuning::OutsideSeconds;

	const float FromBurrow = FirstCircling(200.f, [](FRig& Rig) { Rig.Crab.bInBurrow = Rig.Crab.RoundSeconds < 70.f; });
	TestTrue(TEXT("a crab that comes out of its burrow at 70 s waits its 25 s: the gull is not before 95 s"), FromBurrow >= 70.f + Wait - 0.1f);
	TestTrue(TEXT("and comes then"), FromBurrow < 70.f + Wait + 0.3f);

	const float Interrupted = FirstCircling(200.f, [](FRig& Rig) { Rig.Crab.bInBurrow = Rig.Crab.RoundSeconds >= 50.f && Rig.Crab.RoundSeconds < 50.1f; });
	TestTrue(TEXT("a visit to a burrow at 50 s starts the 25 s over: not before 75 s"), Interrupted >= 75.f - 0.2f);
	TestTrue(TEXT("and then it comes"), Interrupted < 76.f);

	const float Molted = FirstCircling(200.f, [](FRig& Rig) { Rig.Crab.bMolting = Rig.Crab.RoundSeconds >= 50.f && Rig.Crab.RoundSeconds < 50.1f; });
	TestTrue(TEXT("a molt breaks it too: not before 75 s"), Molted >= 50.f + Wait - 0.2f);

	const float Wet = FirstCircling(300.f, [](FRig& Rig) { Rig.Crab.WaterDepth = 25.f; });
	TestTrue(TEXT("water over 20 uu at the crab and there is no gull"), Wet < 0.f);
	const float Damp = FirstCircling(300.f, [](FRig& Rig) { Rig.Crab.WaterDepth = CrabGull::Tuning::LowWater - 0.1f; });
	TestTrue(TEXT("water just under it and there is"), Damp >= CrabGull::Tuning::FirstMinute - 0.01f);
	const float AtTheLine = FirstCircling(300.f, [](FRig& Rig) { Rig.Crab.WaterDepth = CrabGull::Tuning::LowWater; });
	TestTrue(TEXT("water at exactly 20 uu is not low water"), AtTheLine < 0.f);

	const float Later = FirstCircling(300.f, [](FRig& Rig) { Rig.Crab.WaterDepth = Rig.Crab.RoundSeconds < 100.f ? 40.f : 0.f; });
	TestTrue(TEXT("the sea falling to low water starts the 25 s: a gull after 125 s"), Later >= 100.f + Wait - 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullOneAtATimeTest, "CrabSim.Gull.OneAtATimeWithAFortyFiveSecondCooldown", TestFlags)
bool FCrabGullOneAtATimeTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	float AbsentAt = -1.f;
	float NextAt = -1.f;
	int32 Gulls = 0;
	// The crab hides whenever a gull is down, so each one goes after five seconds in a burrow.
	for (int32 Step = 0; Step < 6000 && NextAt < 0.f; ++Step)
	{
		Rig.Crab.bInBurrow = CrabGull::IsThreat(Rig.State.Phase);
		const EPhase Before = Rig.State.Phase;
		Rig.Tick();
		if (Rig.Out.Has(EEvent::Circling))
		{
			TestTrue(TEXT("a gull only begins when there is none about"), Before == EPhase::Absent);
			++Gulls;
			if (Gulls == 2)
			{
				NextAt = Rig.Crab.RoundSeconds;
			}
		}
		if (Before != EPhase::Absent && Rig.State.Phase == EPhase::Absent)
		{
			AbsentAt = Rig.Crab.RoundSeconds;
		}
	}
	TestTrue(TEXT("the first gull came and went"), AbsentAt > 0.f);
	TestTrue(TEXT("a second came"), NextAt > 0.f);
	TestTrue(TEXT("but not until 45 s after the first was gone"), NextAt - AbsentAt >= CrabGull::Tuning::Cooldown - 0.1f);
	TestTrue(TEXT("and it came when the wait was over, no later than the 25 s outside takes"), NextAt - AbsentAt < CrabGull::Tuning::Cooldown + 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullResultsPanelTest, "CrabSim.Gull.NoGullWhileTheResultsPanelIsUp", TestFlags)
bool FCrabGullResultsPanelTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.Crab.bRoundOver = true;
	Rig.TickFor(300.f);
	TestEqual(TEXT("a crab out in the open with the panel up is never sent a gull"), Rig.Count(EEvent::Circling), 0);
	Rig.Mode = CrabGull::ESpawn::Now;
	Rig.TickFor(1.f);
	TestEqual(TEXT("not even on request"), Rig.Count(EEvent::Circling), 0);

	FRig Won;
	Won.SendGull();
	Won.TickFor(3.f);
	TestTrue(TEXT("a gull is about"), Won.State.Phase == EPhase::Circling);
	Won.Crab.bRoundOver = true;
	Won.Tick();
	TestTrue(TEXT("the round is won and it is gone"), Won.State.Phase == EPhase::Absent);

	FRig Eaten;
	Eaten.SendGull();
	Eaten.RunUntil(EEvent::Stalking, 40.f);
	Eaten.State.Phase = EPhase::Caught;
	Eaten.Crab.bRoundOver = true;
	Eaten.TickFor(2.f);
	TestTrue(TEXT("the gull that caught the crab stays where it is"), Eaten.State.Phase == EPhase::Caught);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullSeedTest, "CrabSim.Gull.EachRoundsGullsComeFromTheSeed", TestFlags)
bool FCrabGullSeedTest::RunTest(const FString& Parameters)
{
	auto Run = [](int32 Seed)
	{
		FRig Rig(Seed);
		Rig.SendGull();
		Rig.TickFor(60.f);
		return Rig.Log;
	};
	const TArray<FEventAt> A = Run(42);
	const TArray<FEventAt> B = Run(42);
	if (!TestEqual(TEXT("the same seed gives the same events"), A.Num(), B.Num()) || !TestTrue(TEXT("and there are some"), A.Num() > 2))
	{
		return false;
	}
	bool bSame = true;
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		bSame = bSame && A[Index].Event == B[Index].Event && FMath::IsNearlyEqual(A[Index].Time, B[Index].Time, 1e-4f)
			&& A[Index].Gull.Equals(B[Index].Gull, 1e-3);
	}
	TestTrue(TEXT("down to when and where"), bSame);

	bool bDiffers = false;
	const TArray<FEventAt> Base = Run(1);
	for (int32 Seed = 2; Seed <= 10 && !bDiffers; ++Seed)
	{
		const TArray<FEventAt> Other = Run(Seed);
		bDiffers = Other.Num() != Base.Num() || (Other.Num() > 1 && !Other[1].Gull.Equals(Base[1].Gull, 1.0));
	}
	TestTrue(TEXT("other seeds land elsewhere"), bDiffers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullCirclingTest, "CrabSim.Gull.CirclingIsFarAndNoThreatForTwelveSeconds", TestFlags)
bool FCrabGullCirclingTest::RunTest(const FString& Parameters)
{
	for (int32 Seed = 1; Seed <= 30; ++Seed)
	{
		FRig Rig(Seed);
		Rig.SendGull();
		if (!TestTrue(TEXT("a gull comes on request"), Rig.State.Phase == EPhase::Circling))
		{
			return false;
		}
		const float Centre = static_cast<float>(FVector2D::Distance(Rig.State.CircleCentre, Rig.Crab.Location));
		const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(Rig.State.CircleCentre.Y, Rig.State.CircleCentre.X));
		TestTrue(*FString::Printf(TEXT("seed %d: the loop is 1500 to 2200 uu away (%.0f)"), Seed, Centre),
			Centre >= CrabGull::Tuning::CircleDistanceMin - 1.f && Centre <= CrabGull::Tuning::CircleDistanceMax + 1.f);
		TestTrue(*FString::Printf(TEXT("seed %d: on the sea side of the crab (%.0f degrees)"), Seed, Bearing), FMath::Abs(Bearing) <= CrabGull::Tuning::CircleArc + 0.5f);

		float Nearest = BIG_NUMBER;
		bool bAloft = true;
		while (Rig.State.Phase == EPhase::Circling)
		{
			Rig.Tick();
			if (Rig.State.Phase == EPhase::Circling)
			{
				Nearest = FMath::Min(Nearest, CrabGull::DistanceToCrab(Rig.State, Rig.Crab));
				bAloft = bAloft && CrabGull::IsFlying(Rig.State);
			}
		}
		TestTrue(*FString::Printf(TEXT("seed %d: it never comes within 1000 uu while it circles (%.0f)"), Seed, Nearest), Nearest >= 999.f);
		TestTrue(TEXT("and stays in the air"), bAloft);
		TestEqual(TEXT("no threat and no catch while it circles"), Rig.Count(EEvent::Catch), 0);
		if (Seed == 1)
		{
			TestTrue(TEXT("the circling event came first"), Rig.Find(EEvent::Circling) != nullptr);
			TestTrue(TEXT("and it circled its twelve seconds before the phase changed"), Rig.State.PhaseSeconds < 0.2f && Rig.Crab.RoundSeconds >= CrabGull::Tuning::CircleSeconds - 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullLandingTest, "CrabSim.Gull.LandsFourteenToEighteenHundredUuAwayOnOpenFlats", TestFlags)
bool FCrabGullLandingTest::RunTest(const FString& Parameters)
{
	for (int32 Seed = 1; Seed <= 30; ++Seed)
	{
		FRig Rig(Seed);
		Rig.Crab.Location = FVector2D(300.0, -200.0);
		Rig.SendGull();
		if (!TestTrue(*FString::Printf(TEXT("seed %d: it lands"), Seed), Rig.RunUntil(EEvent::Landed, 30.f)))
		{
			continue;
		}
		const FEventAt* Landed = Rig.Find(EEvent::Landed);
		TestTrue(*FString::Printf(TEXT("seed %d: 1400 to 1800 uu from the crab (%.0f)"), Seed, Landed->Distance),
			Landed->Distance >= CrabGull::Tuning::LandMinDistance - 1.f && Landed->Distance <= CrabGull::Tuning::LandMaxDistance + 1.f);
		TestTrue(TEXT("on the ground"), !CrabGull::IsFlying(Rig.State));
		TestEqual(TEXT("not on a patch when there is none"), Rig.State.Patch, static_cast<int32>(INDEX_NONE));
	}

	// Open flats only: a rule that turns down half the map still finds a spot, and one that turns down all of it means no landing.
	FRig Half;
	Half.Env.IsOpenFlat = [](const FVector2D& Spot) { return Spot.Y <= 0.0; };
	Half.SendGull();
	Half.RunUntil(EEvent::Landed, 30.f);
	TestTrue(TEXT("it landed on the open side"), Half.Find(EEvent::Landed) && Half.State.Location.Y <= 0.0);

	FRig None;
	None.Env.IsOpenFlat = [](const FVector2D&) { return false; };
	None.SendGull();
	None.RunUntil(EEvent::Left, 30.f);
	TestTrue(TEXT("nowhere to land: it never lands"), None.Count(EEvent::Landed) == 0 && None.Find(EEvent::Left) != nullptr);
	TestTrue(TEXT("and goes when its circling is done"), None.Find(EEvent::Left)->Time >= CrabGull::Tuning::CircleSeconds - 0.2f);
	TestTrue(TEXT("saying why"), None.State.LeaveReason == CrabGull::ELeave::NoLanding);

	// A free patch in range is where it lands, one that is bare, too near or too far is not.
	FRig Patched;
	Patched.Patches = {{FVector2D(1600.0, 0.0), true}, {FVector2D(0.0, 1600.0), false}, {FVector2D(900.0, 0.0), true}, {FVector2D(0.0, -2500.0), true}};
	Patched.SendGull();
	Patched.RunUntil(EEvent::Landed, 30.f);
	TestEqual(TEXT("it lands on the one free patch in range"), Patched.State.Patch, 0);
	TestTrue(TEXT("in the middle of it"), Patched.State.Location.Equals(FVector2D(1600.0, 0.0), 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullEatsTest, "CrabSim.Gull.AGullEatsThePatchItLandsOn", TestFlags)
bool FCrabGullEatsTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.Patches = {{FVector2D(1600.0, 0.0), true}};
	Rig.SendGull();
	float Eaten = 0.f;
	float FirstMeal = -1.f;
	float LastMeal = -1.f;
	bool bStalked = false;
	for (int32 Step = 0; Step < 2000; ++Step)
	{
		Rig.Tick();
		if (Rig.Out.EatAmount > 0.f)
		{
			TestEqual(TEXT("it eats that patch"), Rig.Out.EatPatch, 0);
			Eaten += Rig.Out.EatAmount;
			FirstMeal = FirstMeal < 0.f ? Rig.Crab.RoundSeconds : FirstMeal;
			LastMeal = Rig.Crab.RoundSeconds;
			TestFalse(TEXT("only before it sets out"), bStalked);
		}
		bStalked = bStalked || Rig.State.Phase == EPhase::Stalking;
		if (Rig.State.Phase == EPhase::Stalking && Step > 400)
		{
			break;
		}
	}
	TestNearlyEqual(TEXT("the patch loses the meal: the rate over the nine seconds"), Eaten, CrabGull::Tuning::EatRate * CrabGull::Tuning::LandEatSeconds, 0.002f);
	TestTrue(TEXT("it starts once it is down, after the glide"), FirstMeal >= CrabGull::Tuning::CircleSeconds + CrabGull::Tuning::LandGlideSeconds - 0.01f);
	TestTrue(TEXT("and stops when it sets out"), LastMeal <= CrabGull::Tuning::CircleSeconds + CrabGull::Tuning::LandGlideSeconds + CrabGull::Tuning::LandEatSeconds + 0.2f);

	FRig Bare;
	Bare.SendGull();
	float Nothing = 0.f;
	for (int32 Step = 0; Step < 1000; ++Step)
	{
		Bare.Tick();
		Nothing += Bare.Out.EatAmount;
	}
	TestNearlyEqual(TEXT("a gull on open sand eats nothing"), Nothing, 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullStalkTest, "CrabSim.Gull.TheGullWalksAtWalkingPaceAndTheCrabOutrunsIt", TestFlags)
bool FCrabGullStalkTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the gull's pace is 130 uu/s"), FMath::IsNearlyEqual(CrabGull::Tuning::StalkSpeed, 130.f));
	TestTrue(TEXT("under the crab's slowest walk (250)"), CrabGull::Tuning::StalkSpeed < 250.f);

	FRig Rig;
	Rig.SendGull();
	Rig.RunUntil(EEvent::Stalking, 40.f);
	TestTrue(TEXT("it sets out"), Rig.State.Phase == EPhase::Stalking);
	const FVector2D From = Rig.State.Location;
	Rig.TickFor(1.f);
	TestNearlyEqual(TEXT("a second on foot takes it 130 uu"), static_cast<float>(FVector2D::Distance(From, Rig.State.Location)), CrabGull::Tuning::StalkSpeed, 1.5f);

	// A crab that keeps walking away at 250 is never reached, however long it goes.
	FRig Chase;
	Chase.PutHunting(FVector2D(600.0, 0.0));
	Chase.Crab.Speed = 250.f;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		Chase.Crab.Location.X -= 250.0 * Chase.Dt;
		Chase.Tick();
	}
	TestTrue(TEXT("the gull falls further behind"), CrabGull::DistanceToCrab(Chase.State, Chase.Crab) > 600.f);
	TestEqual(TEXT("and there is no catch"), Chase.Count(EEvent::Catch), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullNeverCatchesMovingTest, "CrabSim.Gull.ACrabThatIsMovingIsNeverCaught", TestFlags)
bool FCrabGullNeverCatchesMovingTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.PutHunting(FVector2D(100.0, 0.0));
	Rig.Crab.Speed = 250.f;
	// Right at its feet, walking, for as long as it hunts: never a lunge, never a catch.
	bool bLunged = false;
	for (int32 Step = 0; Step < 700; ++Step)
	{
		Rig.Crab.Location = Rig.State.Location + FVector2D(60.0, 0.0);
		Rig.Tick();
		bLunged = bLunged || Rig.State.Phase == EPhase::Lunging;
	}
	TestFalse(TEXT("it does not even lunge at a crab that is walking"), bLunged);
	TestEqual(TEXT("and never catches it"), Rig.Count(EEvent::Catch), 0);

	FRig Stops;
	Stops.PutHunting(FVector2D(100.0, 0.0));
	Stops.Crab.Speed = 250.f;
	Stops.Crab.Location = FVector2D::ZeroVector;
	Stops.TickFor(3.f);
	TestEqual(TEXT("walking beside it for three seconds: nothing"), Stops.Count(EEvent::Catch), 0);
	Stops.Crab.Speed = 0.f;
	TestTrue(TEXT("the moment the crab stands still it is caught"), Stops.RunUntil(EEvent::Catch, 1.f));
	TestTrue(TEXT("after the lunge, not before"), Stops.Log.Last().Time >= 3.f + CrabGull::Tuning::LungeSeconds - 0.06f);
	TestTrue(TEXT("and the gull is done: caught"), Stops.State.Phase == EPhase::Caught);

	FRig Line;
	Line.PutHunting(FVector2D(100.0, 0.0));
	Line.Crab.Speed = CrabGull::Tuning::MovingSpeed;
	TestFalse(TEXT("a crawl at exactly 40 uu/s is standing still"), CrabGull::IsMoving(Line.Crab));
	Line.Crab.Speed = CrabGull::Tuning::MovingSpeed + 1.f;
	TestTrue(TEXT("41 is moving"), CrabGull::IsMoving(Line.Crab));
	TestFalse(TEXT("and a moving crab in the shallows is not catchable"), CrabGull::IsCatchable(Line.Crab));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullDeepWaterTest, "CrabSim.Gull.InWaterOverSixtyUuASlowedCrabCanBeCaught", TestFlags)
bool FCrabGullDeepWaterTest::RunTest(const FString& Parameters)
{
	CrabGull::FCrabView Crab;
	Crab.Speed = 100.f;
	Crab.WaterDepth = CrabGull::Tuning::DeepWater;
	TestFalse(TEXT("in water at 60 uu a walking crab is still not caught"), CrabGull::IsCatchable(Crab));
	Crab.WaterDepth = CrabGull::Tuning::DeepWater + 1.f;
	TestTrue(TEXT("over 60 uu it is, though it is moving"), CrabGull::IsCatchable(Crab));
	Crab.bInBurrow = true;
	TestFalse(TEXT("but never in a burrow"), CrabGull::IsCatchable(Crab));

	FRig Rig;
	Rig.PutHunting(FVector2D(100.0, 0.0));
	Rig.Crab.Speed = 100.f;
	Rig.Crab.WaterDepth = 80.f;
	TestTrue(TEXT("a gull at its side takes a crab out of its depth"), Rig.RunUntil(EEvent::Catch, 2.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullNeverCatchesBurrowTest, "CrabSim.Gull.ACrabInABurrowIsNeverCaught", TestFlags)
bool FCrabGullNeverCatchesBurrowTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.PutHunting(FVector2D(100.0, 0.0));
	Rig.Crab.bInBurrow = true;
	Rig.TickFor(CrabGull::Tuning::BurrowGiveUpSeconds - 0.3f);
	TestEqual(TEXT("with the gull at its door, still, not in reach of a catch"), Rig.Count(EEvent::Catch), 0);
	TestTrue(TEXT("nor a lunge"), Rig.State.Phase == EPhase::Stalking);
	TestTrue(TEXT("and the gull has waited, not given up, before five seconds"), Rig.State.Phase != EPhase::Leaving);
	Rig.TickFor(0.6f);
	TestTrue(TEXT("after five seconds it goes"), Rig.State.Phase == EPhase::Leaving);
	TestTrue(TEXT("saying so"), Rig.Find(EEvent::Left) != nullptr && Rig.State.LeaveReason == CrabGull::ELeave::BurrowWait);
	TestEqual(TEXT("without a catch"), Rig.Count(EEvent::Catch), 0);

	// The five seconds are unbroken: a step out of the burrow starts them over.
	FRig Peeks;
	Peeks.PutHunting(FVector2D(1000.0, 0.0));
	Peeks.Crab.bInBurrow = true;
	Peeks.TickFor(4.f);
	Peeks.Crab.bInBurrow = false;
	Peeks.Crab.Speed = 200.f;
	Peeks.Tick();
	Peeks.Crab.bInBurrow = true;
	Peeks.Crab.Speed = 0.f;
	Peeks.TickFor(4.f);
	TestTrue(TEXT("four seconds in, one out, four in: the gull is still there"), Peeks.State.Phase == EPhase::Stalking);
	Peeks.TickFor(1.2f);
	TestTrue(TEXT("and gone after five in a row"), Peeks.State.Phase == EPhase::Leaving);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullCatchRuleTest, "CrabSim.Gull.ACatchNeedsAStillCrabOutsideAndWithinOneFiftyUu", TestFlags)
bool FCrabGullCatchRuleTest::RunTest(const FString& Parameters)
{
	auto LungesAt = [](float Distance, float Speed, bool bBurrow)
	{
		FRig Rig;
		Rig.Dt = 0.001f;
		Rig.PutHunting(FVector2D(Distance, 0.0));
		Rig.Crab.Speed = Speed;
		Rig.Crab.bInBurrow = bBurrow;
		Rig.Tick();
		return Rig.State.Phase == EPhase::Lunging;
	};
	TestTrue(TEXT("still, outside, at 148 uu: it lunges"), LungesAt(148.f, 0.f, false));
	TestFalse(TEXT("at 152 uu it does not (yet)"), LungesAt(152.f, 0.f, false));
	TestFalse(TEXT("moving at 100 uu: no"), LungesAt(100.f, 200.f, false));
	TestFalse(TEXT("in a burrow at 100 uu: no"), LungesAt(100.f, 0.f, true));

	// The lunge is off when the crab slips away before it lands.
	FRig Away;
	Away.PutHunting(FVector2D(100.0, 0.0));
	Away.TickFor(0.3f);
	TestTrue(TEXT("a lunge under way"), Away.State.Phase == EPhase::Lunging);
	Away.Crab.Speed = 300.f;
	Away.Tick();
	TestTrue(TEXT("the crab moves: the lunge is off and it is back to stalking"), Away.State.Phase == EPhase::Stalking);
	Away.TickFor(1.f);
	TestEqual(TEXT("no catch"), Away.Count(EEvent::Catch), 0);

	FRig Dives;
	Dives.PutHunting(FVector2D(100.0, 0.0));
	Dives.TickFor(0.3f);
	Dives.Crab.bInBurrow = true;
	Dives.Tick();
	TestTrue(TEXT("or digs in: the lunge is off"), Dives.State.Phase == EPhase::Stalking);
	Dives.TickFor(2.f);
	TestEqual(TEXT("no catch"), Dives.Count(EEvent::Catch), 0);

	// Stood still where the gull can reach it: caught, lunge and all.
	FRig Caught;
	Caught.PutHunting(FVector2D(100.0, 0.0));
	TestTrue(TEXT("a crab standing in reach is caught"), Caught.RunUntil(EEvent::Catch, 2.f));
	TestTrue(TEXT("with the gull within 150 uu"), Caught.Log.Last().Distance <= CrabGull::Tuning::CatchRadius);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullGiveUpTest, "CrabSim.Gull.TheGullGivesUpAfterFortySecondsOfHunting", TestFlags)
bool FCrabGullGiveUpTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.PutHunting(FVector2D(400.0, 0.0));
	Rig.Crab.Speed = 300.f;
	float Began = 0.f;
	for (int32 Step = 0; Step < 1200 && Rig.State.Phase != EPhase::Leaving; ++Step)
	{
		Rig.Crab.Location = Rig.State.Location + FVector2D(400.0, 0.0);
		Rig.Tick();
	}
	TestTrue(TEXT("the crab keeps moving and the gull gives up"), Rig.State.Phase == EPhase::Leaving && Rig.State.LeaveReason == CrabGull::ELeave::GaveUp);
	TestNearlyEqual(TEXT("after forty seconds"), Rig.Crab.RoundSeconds - Began, CrabGull::Tuning::GiveUpSeconds, 0.2f);
	TestEqual(TEXT("with no catch"), Rig.Count(EEvent::Catch), 0);
	TestEqual(TEXT("and one left event"), Rig.Count(EEvent::Left), 1);

	Rig.TickFor(CrabGull::Tuning::LeaveSeconds + 0.2f);
	TestTrue(TEXT("it flies off and is gone"), Rig.State.Phase == EPhase::Absent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullScareTest, "CrabSim.Gull.ADancingCrabScaresAGullWithinNineHundredUuAfterFourSeconds", TestFlags)
bool FCrabGullScareTest::RunTest(const FString& Parameters)
{
	// Dancing from the start: the gull lands, stalks, and comes within range of the dance. Four seconds later it flies off.
	FRig Rig;
	Rig.Crab.bDancing = true;
	Rig.SendGull();
	float WithinRange = -1.f;
	for (int32 Step = 0; Step < 2000 && !Rig.Find(EEvent::Scared); ++Step)
	{
		Rig.Tick();
		if (WithinRange < 0.f && Rig.State.Phase == EPhase::Stalking && CrabGull::DistanceToCrab(Rig.State, Rig.Crab) <= CrabGull::Tuning::ScareRange)
		{
			WithinRange = Rig.Crab.RoundSeconds;
		}
	}
	const FEventAt* Scared = Rig.Find(EEvent::Scared);
	if (!TestNotNull(TEXT("a crab that dances scares the gull"), Scared))
	{
		return false;
	}
	TestNearlyEqual(TEXT("four seconds after it came within 900 uu"), Scared->Time - WithinRange, CrabGull::Tuning::ScareDanceSeconds, 0.25f);
	TestTrue(TEXT("with the gull still beyond its catch reach"), Scared->Distance > CrabGull::Tuning::CatchRadius);
	TestEqual(TEXT("no catch"), Rig.Count(EEvent::Catch), 0);
	TestTrue(TEXT("it flies off, and says so"), Rig.State.Phase == EPhase::Leaving && Rig.Find(EEvent::Left) != nullptr && Rig.State.LeaveReason == CrabGull::ELeave::Scared);

	// It stays away for 60 s from the scare: no new gull before that, the crab out in the open the whole while.
	const float ScaredAt = Scared->Time;
	Rig.Crab.bDancing = false;
	const int32 Before = Rig.Count(EEvent::Circling);
	while (Rig.Crab.RoundSeconds < ScaredAt + 59.5f)
	{
		Rig.Tick();
	}
	TestEqual(TEXT("no gull for 60 s after the scare"), Rig.Count(EEvent::Circling), Before);
	Rig.TickFor(3.f);
	TestEqual(TEXT("and then one comes"), Rig.Count(EEvent::Circling), Before + 1);

	// The dance counts only while the gull is on the hunt and within range, and only when unbroken.
	FRig Far;
	Far.PutHunting(FVector2D(2000.0, 0.0));
	Far.Crab.bDancing = true;
	Far.Dt = 0.01f;
	Far.Tick();
	TestNearlyEqual(TEXT("a gull beyond 900 uu takes no notice"), Far.State.DanceSeconds, 0.f, 1e-6f);

	FRig Broken;
	Broken.PutHunting(FVector2D(880.0, 0.0));
	Broken.Crab.bDancing = true;
	Broken.TickFor(2.f);
	TestTrue(TEXT("two seconds of dance"), Broken.State.DanceSeconds > 1.9f);
	Broken.Crab.bDancing = false;
	Broken.Tick();
	TestNearlyEqual(TEXT("a break in the dance starts the four seconds over"), Broken.State.DanceSeconds, 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullDanceGambleTest, "CrabSim.Gull.DancingNearAVeryCloseGullIsCaught", TestFlags)
bool FCrabGullDanceGambleTest::RunTest(const FString& Parameters)
{
	FRig Close;
	Close.PutHunting(FVector2D(120.0, 0.0));
	Close.Crab.bDancing = true;
	Close.TickFor(2.f);
	TestEqual(TEXT("a gull within 150 uu catches a dancing crab: it is standing still"), Close.Count(EEvent::Catch), 1);
	TestEqual(TEXT("it is not scared"), Close.Count(EEvent::Scared), 0);

	FRig Late;
	Late.PutHunting(FVector2D(320.0, 0.0));
	Late.Crab.bDancing = true;
	Late.TickFor(4.5f);
	TestEqual(TEXT("a dance begun with the gull at 320 uu is too late: it closes in a second or two"), Late.Count(EEvent::Catch), 1);
	TestEqual(TEXT("and does not scare it"), Late.Count(EEvent::Scared), 0);

	FRig Early;
	Early.PutHunting(FVector2D(850.0, 0.0));
	Early.Crab.bDancing = true;
	Early.TickFor(4.5f);
	TestEqual(TEXT("begun in time, at 850 uu, it works"), Early.Count(EEvent::Scared), 1);
	TestEqual(TEXT("and there is no catch"), Early.Count(EEvent::Catch), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullWarningTest, "CrabSim.Gull.ThereAreAtLeastTwentyFiveSecondsBetweenTheFirstCircleAndAnyCatch", TestFlags)
bool FCrabGullWarningTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the earliest catch the numbers allow is 25 s or more after the circling starts"), CrabGull::EarliestCatchSeconds() >= 25.f);

	for (int32 Seed = 1; Seed <= 20; ++Seed)
	{
		// The worst case for the crab: it walks up to the landed gull and stands beside it, so the gull has no walk at all.
		FRig Rig(Seed);
		Rig.SendGull();
		const float Began = Rig.Crab.RoundSeconds;
		bool bMoved = false;
		for (int32 Step = 0; Step < 3000 && !Rig.Find(EEvent::Catch); ++Step)
		{
			if (Rig.State.Phase == EPhase::Landing && Rig.Crab.RoundSeconds - Began > CrabGull::Tuning::CircleSeconds + 0.5f)
			{
				Rig.Crab.Location = Rig.State.LandingSpot + FVector2D(80.0, 0.0);
				bMoved = true;
			}
			Rig.Tick();
		}
		const FEventAt* Catch = Rig.Find(EEvent::Catch);
		if (TestNotNull(*FString::Printf(TEXT("seed %d: even then there is a catch"), Seed), Catch) && bMoved)
		{
			TestTrue(*FString::Printf(TEXT("seed %d: %.1f s from the first circle"), Seed, Catch->Time - Began), Catch->Time - Began >= 25.f);
		}
	}

	// A crab that just stands where it is gets more: the gull's whole walk on top.
	FRig Still;
	Still.SendGull();
	const float Began = Still.Crab.RoundSeconds;
	Still.RunUntil(EEvent::Catch, 90.f);
	const FEventAt* Catch = Still.Find(EEvent::Catch);
	if (TestNotNull(TEXT("a crab that stands still is caught"), Catch))
	{
		const float Walk = (CrabGull::Tuning::LandMinDistance - CrabGull::Tuning::CatchRadius) / CrabGull::Tuning::StalkSpeed;
		TestTrue(*FString::Printf(TEXT("%.1f s: the earliest catch plus the walk (%.1f s)"), Catch->Time - Began, Walk), Catch->Time - Began >= CrabGull::EarliestCatchSeconds() + Walk - 0.5f);
		TestTrue(TEXT("and it does not drag on"), Catch->Time - Began < 60.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullLifecycleTest, "CrabSim.Gull.TheLifecycleRunsInOrderAndTheEventsAreLogged", TestFlags)
bool FCrabGullLifecycleTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	TestTrue(TEXT("nothing there to begin with"), Rig.State.Phase == EPhase::Absent);
	Rig.SendGull();
	TestTrue(TEXT("circling"), Rig.State.Phase == EPhase::Circling);
	Rig.RunUntil(EEvent::Landed, 30.f);
	TestTrue(TEXT("landing, and down"), Rig.State.Phase == EPhase::Landing && !CrabGull::IsFlying(Rig.State));
	Rig.RunUntil(EEvent::Stalking, 30.f);
	TestTrue(TEXT("stalking"), Rig.State.Phase == EPhase::Stalking);
	Rig.RunUntil(EEvent::Catch, 60.f);
	TestTrue(TEXT("caught"), Rig.State.Phase == EPhase::Caught);

	TArray<EEvent> Order;
	for (const FEventAt& Entry : Rig.Log)
	{
		Order.Add(Entry.Event);
	}
	TestTrue(TEXT("the events came in order: circling, landed, stalking, catch"),
		Order == TArray<EEvent>({EEvent::Circling, EEvent::Landed, EEvent::Stalking, EEvent::Catch}));

	const int32 Events = Rig.Log.Num();
	Rig.TickFor(120.f);
	TestEqual(TEXT("a gull that has caught the crab does nothing more"), Rig.Log.Num(), Events);
	TestTrue(TEXT("and stays"), Rig.State.Phase == EPhase::Caught);

	TestTrue(TEXT("the HUD warns from the circling to the lunge"), CrabGull::IsWarned(EPhase::Circling) && CrabGull::IsWarned(EPhase::Landing)
		&& CrabGull::IsWarned(EPhase::Stalking) && CrabGull::IsWarned(EPhase::Lunging));
	TestFalse(TEXT("not once it is gone, or before"), CrabGull::IsWarned(EPhase::Absent) || CrabGull::IsWarned(EPhase::Leaving));
	TestTrue(TEXT("every phase has a name"), FCString::Strlen(CrabGull::PhaseName(EPhase::Stalking)) > 0 && FCString::Strlen(CrabGull::LeaveText(CrabGull::ELeave::Scared)) > 0);
	TestEqual(TEXT("a distance is in metres"), CrabGull::DistanceText(2340.f), FString(TEXT("23 m")));
	TestTrue(TEXT("the banner says something while it is coming"), FCString::Strlen(CrabGull::BannerText(EPhase::Circling)) > 0
		&& FCString::Strlen(CrabGull::BannerText(EPhase::Stalking)) > 0 && FCString::Strlen(CrabGull::BannerText(EPhase::Absent)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullForceTest, "CrabSim.Gull.TheTestSwitchSendsAGullAtOnceButNotToACrabInABurrow", TestFlags)
bool FCrabGullForceTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.Mode = CrabGull::ESpawn::WhenFree;
	Rig.Tick();
	TestEqual(TEXT("with the switch on, a crab in the open has a gull in a moment"), Rig.Count(EEvent::Circling), 1);
	TestTrue(TEXT("in the first second of the round"), Rig.Log[0].Time < 1.f);

	FRig Hidden;
	Hidden.Mode = CrabGull::ESpawn::WhenFree;
	Hidden.Crab.bInBurrow = true;
	Hidden.TickFor(30.f);
	TestEqual(TEXT("but not one that is in a burrow"), Hidden.Count(EEvent::Circling), 0);
	Hidden.Crab.bInBurrow = false;
	Hidden.Tick();
	TestEqual(TEXT("it comes the moment the crab is out"), Hidden.Count(EEvent::Circling), 1);
	return true;
}
