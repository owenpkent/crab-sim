// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabGotoMath.h"
#include "CrabGullMath.h"

namespace UE::CrabSim::Tests::GullBalance
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	using CrabGull::EEvent;
	using CrabGull::EPhase;

	/** A round is about ten minutes: the length the playtest bot takes. */
	constexpr float RoundLength = 600.f;
	constexpr float TickSeconds = 0.1f;
	/** The crab's walk over open ground, uu/s: between its forward and side speeds. */
	constexpr float WalkSpeed = 300.f;
	/** The sea is up (the crab hides in a burrow) this long in each 180 s tide, from this far in. */
	constexpr float TideCycle = 180.f;
	constexpr float SeaFrom = 30.f;
	constexpr float SeaUntil = 100.f;

	const FVector2D Patches[] = {FVector2D(-1300, 450), FVector2D(-650, 520), FVector2D(-350, -700), FVector2D(600, -650),
		FVector2D(1150, -80), FVector2D(1700, -420), FVector2D(1800, 850)};
	const float PatchRichness[] = {0.40f, 0.45f, 0.60f, 0.75f, 0.80f, 0.90f, 1.00f};
	const FVector2D Burrows[] = {FVector2D(-2450, 350), FVector2D(-1000, -520), FVector2D(700, 760), FVector2D(1900, -900)};
	const float BurrowFloors[] = {130.f, 20.f, -82.f, -154.f};

	enum class EPolicy : uint8
	{
		/** Presses BURROW when a gull lands, or, half the time, feeds on until it is within 700 uu. */
		Answers,
		/** Never looks up. */
		Ignores,
	};

	enum class EMode : uint8
	{
		Feeding,
		Walking,
		Hidden,
	};

	struct FOutcome
	{
		int32 Gulls = 0;
		bool bCaught = false;
		float CaughtAt = 0.f;
		float AnsweringSeconds = 0.f;
	};

	/** The distance to the burrow the BURROW button would pick from here, and which. */
	int32 ButtonBurrow(const FVector2D& From, float& OutDistance)
	{
		TArray<CrabGoto::FBurrow> Options;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Burrows); ++Index)
		{
			CrabGoto::FBurrow Burrow;
			Burrow.Distance = static_cast<float>(FVector2D::Distance(From, Burrows[Index]));
			Burrow.FloorZ = BurrowFloors[Index];
			Options.Add(Burrow);
		}
		const int32 Pick = CrabGoto::PickBurrow(Options);
		OutDistance = Options[Pick].Distance;
		return Pick;
	}

	int32 ButtonPatch(const FVector2D& From)
	{
		TArray<CrabGoto::FPatch> Options;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Patches); ++Index)
		{
			CrabGoto::FPatch Patch;
			Patch.Distance = static_cast<float>(FVector2D::Distance(From, Patches[Index]));
			Patch.Richness = PatchRichness[Index];
			Options.Add(Patch);
		}
		return CrabGoto::PickPatch(Options);
	}

	/**
	 * One round of a crab that does what the playtest bot does: feed at a patch, walk on to another, sit out each high
	 * water in a burrow, and answer (or not) a gull. The gull's own rules are the real ones.
	 */
	FOutcome PlayRound(int32 Seed, EPolicy Policy)
	{
		FRandomStream Plan(Seed * 13 + 5);
		CrabGull::FState Gull(1000 + Seed);
		CrabGull::FCrabView Crab;
		CrabGull::FEnvironment Env;
		CrabGull::FStep Out;
		TArray<CrabGull::FPatchSpot> Spots;
		for (const FVector2D& Patch : Patches)
		{
			Spots.Add({Patch, true});
		}
		Env.Patches = Spots;
		Env.IsOpenFlat = [](const FVector2D& Spot) { return FMath::Abs(Spot.Y) <= 2300.0 && Spot.X >= -1900.0 && Spot.X <= 4500.0; };

		FOutcome Outcome;
		EMode Mode = EMode::Feeding;
		FVector2D Target = Crab.Location;
		bool bToBurrow = false;
		bool bAnswering = false;
		bool bWaitFar = false;
		float FeedUntil = Plan.FRandRange(25.f, 45.f);
		float AnswerClear = -1.f;
		int32 LastSeaCycle = -1;

		auto GoToBurrow = [&]()
		{
			float Unused = 0.f;
			Target = Burrows[ButtonBurrow(Crab.Location, Unused)];
			Mode = EMode::Walking;
			bToBurrow = true;
		};

		for (float Time = 0.f; Time < RoundLength; Time += TickSeconds)
		{
			Crab.RoundSeconds = Time;
			const float Phase = FMath::Fmod(Time, TideCycle);
			const bool bSea = Phase >= SeaFrom && Phase < SeaUntil;
			const int32 Cycle = static_cast<int32>(Time / TideCycle);

			// The tide: when the flood starts the crab goes to a burrow, and stays until the water has fallen.
			if (bSea && Cycle != LastSeaCycle)
			{
				LastSeaCycle = Cycle;
				if (Mode != EMode::Hidden)
				{
					GoToBurrow();
				}
			}

			// A gull that is down: the crab that answers presses BURROW at once, or, if it chose to feed on, when the gull is near.
			const bool bThreat = CrabGull::IsThreat(Gull.Phase);
			if (Policy == EPolicy::Answers && bThreat && Mode != EMode::Hidden && !bAnswering)
			{
				const float Near = CrabGull::DistanceToCrab(Gull, Crab);
				if (!(Mode == EMode::Feeding && bWaitFar && Near > 700.f))
				{
					GoToBurrow();
					bAnswering = true;
					AnswerClear = -1.f;
				}
			}
			if (bAnswering)
			{
				Outcome.AnsweringSeconds += TickSeconds;
				if (Mode == EMode::Hidden && !bThreat && AnswerClear < 0.f)
				{
					AnswerClear = Time + 1.5f;
				}
			}

			// Out of the burrow when the water has fallen and there is no gull to wait for: to the best patch.
			if (Mode == EMode::Hidden && !bSea && (!bAnswering || (AnswerClear >= 0.f && Time >= AnswerClear)))
			{
				Crab.bInBurrow = false;
				bAnswering = false;
				AnswerClear = -1.f;
				Target = Patches[ButtonPatch(Crab.Location)];
				Mode = EMode::Walking;
				bToBurrow = false;
				FeedUntil = Time + 8.f + Plan.FRandRange(25.f, 45.f);
			}

			switch (Mode)
			{
			case EMode::Walking:
			{
				FVector2D Toward = Target - Crab.Location;
				if (Toward.Size() <= WalkSpeed * TickSeconds)
				{
					Crab.Location = Target;
					Crab.Speed = 0.f;
					Mode = bToBurrow ? EMode::Hidden : EMode::Feeding;
					Crab.bInBurrow = bToBurrow;
				}
				else
				{
					Toward.Normalize();
					Crab.Location += Toward * (WalkSpeed * TickSeconds);
					Crab.Speed = WalkSpeed;
				}
				break;
			}
			case EMode::Feeding:
			{
				Crab.Speed = 0.f;
				if (Time >= FeedUntil)
				{
					Target = Patches[Plan.RandRange(0, UE_ARRAY_COUNT(Patches) - 1)];
					Mode = EMode::Walking;
					bToBurrow = false;
					FeedUntil = Time + 8.f + Plan.FRandRange(25.f, 45.f);
				}
				break;
			}
			case EMode::Hidden:
				Crab.Speed = 0.f;
				break;
			}
			Crab.WaterDepth = (bSea && !Crab.bInBurrow) ? 60.f : 0.f;

			const EPhase Before = Gull.Phase;
			CrabGull::Step(Gull, Crab, Env, TickSeconds, CrabGull::ESpawn::Natural, Out);
			if (Out.Has(EEvent::Circling))
			{
				++Outcome.Gulls;
				bWaitFar = Plan.FRand() < 0.5f;
			}
			if (Before != EPhase::Absent && Gull.Phase == EPhase::Absent)
			{
				bAnswering = false;
			}
			if (Out.Has(EEvent::Catch))
			{
				Outcome.bCaught = true;
				Outcome.CaughtAt = Time;
				break;
			}
		}
		return Outcome;
	}

	struct FStats
	{
		int32 Rounds = 0;
		int32 Caught = 0;
		float Gulls = 0.f;
		float Answering = 0.f;
		float CaughtAt = 0.f;
	};

	FStats PlayMany(EPolicy Policy, int32 Rounds)
	{
		FStats Stats;
		for (int32 Seed = 1; Seed <= Rounds; ++Seed)
		{
			const FOutcome Outcome = PlayRound(Seed, Policy);
			++Stats.Rounds;
			Stats.Caught += Outcome.bCaught ? 1 : 0;
			Stats.Gulls += Outcome.Gulls;
			Stats.Answering += Outcome.AnsweringSeconds;
			Stats.CaughtAt += Outcome.bCaught ? Outcome.CaughtAt : 0.f;
		}
		return Stats;
	}
}

using namespace UE::CrabSim::Tests::GullBalance;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGullBalanceTest, "CrabSim.Gull.Balance.AGullAnswererIsRarelyCaughtAndAnIgnorerUsually", TestFlags)
bool FCrabGullBalanceTest::RunTest(const FString& Parameters)
{
	constexpr int32 Rounds = 400;
	const FStats Answers = PlayMany(EPolicy::Answers, Rounds);
	const FStats Ignores = PlayMany(EPolicy::Ignores, Rounds);

	const float AnswerCaught = static_cast<float>(Answers.Caught) / Rounds;
	const float IgnoreCaught = static_cast<float>(Ignores.Caught) / Rounds;
	AddInfo(FString::Printf(TEXT("GULLBALANCE answers: caught %d of %d rounds (%.3f a round), %.2f gulls a round, %.0f s a round spent answering"),
		Answers.Caught, Rounds, AnswerCaught, Answers.Gulls / Rounds, Answers.Answering / Rounds));
	AddInfo(FString::Printf(TEXT("GULLBALANCE ignores: caught %d of %d rounds (%.3f a round), %.2f gulls a round, caught at %.0f s on average"),
		Ignores.Caught, Rounds, IgnoreCaught, Ignores.Gulls / Rounds, Ignores.Caught > 0 ? Ignores.CaughtAt / Ignores.Caught : 0.f));

	TestTrue(*FString::Printf(TEXT("a crab that answers its gulls is caught in %.1f%% of rounds, well under one in ten"), AnswerCaught * 100.f), AnswerCaught <= 0.10f);
	TestTrue(*FString::Printf(TEXT("a crab that ignores them is caught in %.1f%% of rounds, most of them"), IgnoreCaught * 100.f), IgnoreCaught >= 0.60f);
	TestTrue(TEXT("there are gulls to answer: about two to five a round"), Answers.Gulls / Rounds >= 1.5f && Answers.Gulls / Rounds <= 6.f);
	TestTrue(*FString::Printf(TEXT("and answering them costs under two minutes of a round (%.0f s)"), Answers.Answering / Rounds), Answers.Answering / Rounds < 120.f);
	return true;
}
