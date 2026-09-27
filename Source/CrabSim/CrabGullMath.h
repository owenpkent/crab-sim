// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Gulls, as a pure state machine. A gull is slow to arrive, loudly announced and answered by one click: it
 * circles for a while (no threat), lands well away from the crab and eats, then stalks it on foot at a walking
 * pace the crab outruns. It catches the crab only when the crab is out of a burrow, standing still and within
 * reach. A burrow always hides the crab, and a dance from far enough off scares the gull away. Everything is
 * deterministic given the seed, and every number that sets the pace is in Tuning.
 */
namespace CrabGull
{
	struct Tuning
	{
		// --- When a gull comes ---
		/** No gull at all this long into a round, s. */
		static constexpr float FirstMinute = 60.f;
		/** The crab must have been out of a burrow, not molting, at low or falling water this long, s. */
		static constexpr float OutsideSeconds = 25.f;
		/** Water over the crab under this counts as low or falling water, uu. */
		static constexpr float LowWater = 20.f;
		/** No gull has been active for this long, s. */
		static constexpr float Cooldown = 45.f;
		/** A gull that was scared off stays away this long, s. */
		static constexpr float ScareCooldown = 60.f;

		// --- How it arrives ---
		/** The loop it flies before it lands: no threat, s. */
		static constexpr float CircleSeconds = 12.f;
		/** The loop's middle is this far from the crab, uu, toward the sea (within CircleArc degrees of it). */
		static constexpr float CircleDistanceMin = 1500.f;
		static constexpr float CircleDistanceMax = 2200.f;
		static constexpr float CircleArc = 80.f;
		static constexpr float CircleRadius = 500.f;
		/** How high it flies while it circles, uu, and how fast it goes round, radians a second. */
		static constexpr float CircleAltitude = 420.f;
		static constexpr float CircleAngularSpeed = 0.9f;
		/** It glides down to the ground this long, then eats where it lands this long before it sets out, s. */
		static constexpr float LandGlideSeconds = 4.f;
		static constexpr float LandEatSeconds = 9.f;
		/** It lands this far from the crab, uu. */
		static constexpr float LandMinDistance = 1400.f;
		static constexpr float LandMaxDistance = 1800.f;
		/** Richness a patch loses each second to a gull on it. */
		static constexpr float EatRate = 0.04f;

		// --- The hunt ---
		/** Its walking pace, uu/s. The crab walks at 250 to 450, so a moving crab always outruns it. */
		static constexpr float StalkSpeed = 130.f;
		/** It closes on the crab to this distance and no nearer, uu. */
		static constexpr float StopDistance = 70.f;
		/** It can take the crab from this close, uu. */
		static constexpr float CatchRadius = 150.f;
		/** The lunge before the catch, s, and how far past CatchRadius the crab may slip before the lunge is off, uu. */
		static constexpr float LungeSeconds = 0.6f;
		static constexpr float LungeSlack = 40.f;
		static constexpr float LungeSpeed = 260.f;
		/** The crab counts as moving above this speed, uu/s. */
		static constexpr float MovingSpeed = 40.f;
		/** In water this deep the crab's speed is cut and a moving crab can be caught, uu. */
		static constexpr float DeepWater = 60.f;

		// --- How it goes ---
		/** It gives up after this long on the hunt, s. */
		static constexpr float GiveUpSeconds = 40.f;
		/** It gives up when the crab has been in a burrow this long while it is down, s. */
		static constexpr float BurrowGiveUpSeconds = 5.f;
		/** A crab that dances this long with the gull on the hunt within ScareRange scares it, s and uu. */
		static constexpr float ScareDanceSeconds = 4.f;
		static constexpr float ScareRange = 900.f;
		/** It flies off this long, s, at this speed, uu/s, climbing this fast, uu/s. */
		static constexpr float LeaveSeconds = 6.f;
		static constexpr float LeaveSpeed = 700.f;
		static constexpr float LeaveClimb = 160.f;
	};

	enum class EPhase : uint8
	{
		Absent,
		Circling,
		Landing,
		Stalking,
		Lunging,
		Leaving,
		Caught,
	};

	enum class EEvent : uint8
	{
		Circling,
		Landed,
		Stalking,
		Scared,
		Left,
		Catch,
	};

	enum class ELeave : uint8
	{
		None,
		GaveUp,
		BurrowWait,
		Scared,
		NoLanding,
	};

	/** How a gull comes to be: by the rules, when the crab is out and about, or now. */
	enum class ESpawn : uint8
	{
		Natural,
		/** The test switch (CrabSim.GullForce): as soon as there is none and the crab is out of its burrow, timers ignored. */
		WhenFree,
		/** At once, whatever the crab is doing (never with the results panel up). */
		Now,
	};

	/** What the gull rules need to know about the crab. */
	struct FCrabView
	{
		FVector2D Location = FVector2D::ZeroVector;
		/** Ground speed, uu/s. */
		float Speed = 0.f;
		/** Water over the ground it stands on, uu. */
		float WaterDepth = 0.f;
		/** Seconds since the round began. */
		float RoundSeconds = 0.f;
		bool bInBurrow = false;
		bool bMolting = false;
		bool bDancing = false;
		bool bRoundOver = false;
	};

	/** A food patch a gull might land on. It is free when it holds food and is dry. */
	struct FPatchSpot
	{
		FVector2D Location = FVector2D::ZeroVector;
		bool bFree = true;
	};

	struct FEnvironment
	{
		TArrayView<const FPatchSpot> Patches;
		/** Whether a gull may land on this spot: open, dry flats. Empty means anywhere. */
		TFunction<bool(const FVector2D&)> IsOpenFlat;
	};

	struct FState
	{
		FState() : Random(1) {}
		explicit FState(int32 Seed) : Random(Seed) {}

		FRandomStream Random;
		EPhase Phase = EPhase::Absent;
		/** Seconds in this phase. */
		float PhaseSeconds = 0.f;

		/** Where it is on the ground, how high above it, and which way it faces (yaw, degrees). */
		FVector2D Location = FVector2D::ZeroVector;
		float Altitude = 0.f;
		float Heading = 0.f;

		FVector2D CircleCentre = FVector2D::ZeroVector;
		float CircleAngle = 0.f;
		FVector2D GlideFrom = FVector2D::ZeroVector;
		float GlideFromAltitude = 0.f;
		FVector2D LandingSpot = FVector2D::ZeroVector;
		/** The patch it lands on and eats, or INDEX_NONE. */
		int32 Patch = INDEX_NONE;
		FVector2D LeaveDirection = FVector2D(1.0, 0.0);
		ELeave LeaveReason = ELeave::None;

		/** Seconds on the hunt (stalking and lunging), in a burrow while it is down, and dancing within range. */
		float StalkSeconds = 0.f;
		float BurrowSeconds = 0.f;
		float DanceSeconds = 0.f;
		/** Seconds the crab has been out of a burrow, not molting, at low water, without a break. */
		float OutsideSeconds = 0.f;
		/** Round seconds before which no gull comes. */
		float QuietUntil = 0.f;
		/** Number of gulls so far this round. */
		int32 Count = 0;
	};

	/** What one step of the state machine did. */
	struct FStep
	{
		TArray<EEvent, TInlineAllocator<4>> Events;
		/** Richness the gull ate this step from this patch. */
		int32 EatPatch = INDEX_NONE;
		float EatAmount = 0.f;

		bool Has(EEvent Event) const { return Events.Contains(Event); }
	};

	/** Seconds from a gull appearing to the earliest catch there can be: it takes the whole of the circle, the landing, the meal and the lunge. */
	inline float EarliestCatchSeconds()
	{
		return Tuning::CircleSeconds + Tuning::LandGlideSeconds + Tuning::LandEatSeconds + Tuning::LungeSeconds;
	}

	inline bool IsMoving(const FCrabView& Crab)
	{
		return Crab.Speed > Tuning::MovingSpeed;
	}

	/** The crab is in reach of a catch: out of every burrow, and standing still (or out of its depth, where its speed is cut). */
	inline bool IsCatchable(const FCrabView& Crab)
	{
		return !Crab.bInBurrow && (!IsMoving(Crab) || Crab.WaterDepth > Tuning::DeepWater);
	}

	/** The phases that threaten the crab: the gull is down and near. */
	inline bool IsThreat(EPhase Phase)
	{
		return Phase == EPhase::Landing || Phase == EPhase::Stalking || Phase == EPhase::Lunging;
	}

	/** The phases the HUD warns about: a gull is coming, from the first circle to the last lunge. */
	inline bool IsWarned(EPhase Phase)
	{
		return Phase == EPhase::Circling || IsThreat(Phase);
	}

	inline bool IsFlying(const FState& State)
	{
		return State.Altitude > 20.f;
	}

	inline float DistanceToCrab(const FState& State, const FCrabView& Crab)
	{
		return static_cast<float>(FVector2D::Distance(State.Location, Crab.Location));
	}

	inline const TCHAR* PhaseName(EPhase Phase)
	{
		switch (Phase)
		{
		case EPhase::Circling: return TEXT("Circling");
		case EPhase::Landing: return TEXT("Landing");
		case EPhase::Stalking: return TEXT("Stalking");
		case EPhase::Lunging: return TEXT("Lunging");
		case EPhase::Leaving: return TEXT("Leaving");
		case EPhase::Caught: return TEXT("Caught");
		default: return TEXT("Absent");
		}
	}

	inline const TCHAR* LeaveText(ELeave Reason)
	{
		switch (Reason)
		{
		case ELeave::GaveUp: return TEXT("gave_up");
		case ELeave::BurrowWait: return TEXT("burrow");
		case ELeave::Scared: return TEXT("scared");
		case ELeave::NoLanding: return TEXT("no_landing");
		default: return TEXT("none");
		}
	}

	/** The line under "Gull!" on the HUD: what the gull is doing. */
	inline const TCHAR* BannerText(EPhase Phase)
	{
		switch (Phase)
		{
		case EPhase::Circling: return TEXT("It is circling.");
		case EPhase::Landing: return TEXT("It has landed.");
		case EPhase::Stalking: return TEXT("It is coming. Press BURROW.");
		case EPhase::Lunging: return TEXT("Too late.");
		default: return TEXT("");
		}
	}

	/** A distance as metres, for the HUD: 100 uu is a metre. */
	inline FString DistanceText(float Distance)
	{
		return FString::Printf(TEXT("%d m"), FMath::RoundToInt(FMath::Max(Distance, 0.f) / 100.f));
	}

	/** True if a natural gull may begin circling now. */
	inline bool MaySpawn(const FState& State, const FCrabView& Crab, ESpawn Mode)
	{
		if (Crab.bRoundOver)
		{
			return false;
		}
		switch (Mode)
		{
		case ESpawn::Now:
			return true;
		case ESpawn::WhenFree:
			return !Crab.bInBurrow && !Crab.bMolting;
		default:
			return Crab.RoundSeconds >= Tuning::FirstMinute && State.OutsideSeconds >= Tuning::OutsideSeconds
				&& Crab.RoundSeconds >= State.QuietUntil;
		}
	}

	/**
	 * Where the gull lands: a free patch 1400 to 1800 uu from the crab (one of them, at random), else a random spot
	 * on open flats at that range. False if there is none.
	 */
	inline bool ChooseLanding(FRandomStream& Random, const FVector2D& Crab, const FEnvironment& Env, FVector2D& OutSpot, int32& OutPatch)
	{
		OutPatch = INDEX_NONE;
		TArray<int32, TInlineAllocator<8>> Candidates;
		for (int32 Index = 0; Index < Env.Patches.Num(); ++Index)
		{
			const float Distance = static_cast<float>(FVector2D::Distance(Crab, Env.Patches[Index].Location));
			if (Env.Patches[Index].bFree && Distance >= Tuning::LandMinDistance && Distance <= Tuning::LandMaxDistance)
			{
				Candidates.Add(Index);
			}
		}
		if (Candidates.Num() > 0)
		{
			OutPatch = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
			OutSpot = Env.Patches[OutPatch].Location;
			return true;
		}
		for (int32 Attempt = 0; Attempt < 24; ++Attempt)
		{
			const float Angle = Random.FRandRange(0.f, 2.f * PI);
			const float Distance = Random.FRandRange(Tuning::LandMinDistance, Tuning::LandMaxDistance);
			const FVector2D Spot = Crab + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
			if (!Env.IsOpenFlat || Env.IsOpenFlat(Spot))
			{
				OutSpot = Spot;
				return true;
			}
		}
		return false;
	}

	namespace Detail
	{
		inline void Enter(FState& State, EPhase Phase)
		{
			State.Phase = Phase;
			State.PhaseSeconds = 0.f;
		}

		inline float YawOf(const FVector2D& Direction)
		{
			return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
		}

		inline void BeginLeaving(FState& State, const FCrabView& Crab, ELeave Reason, FStep& Out)
		{
			FVector2D Away = State.Location - Crab.Location;
			if (!Away.Normalize())
			{
				Away = FVector2D(1.0, 0.0);
			}
			const float Turn = FMath::DegreesToRadians(State.Random.FRandRange(-30.f, 30.f));
			State.LeaveDirection = FVector2D(Away.X * FMath::Cos(Turn) - Away.Y * FMath::Sin(Turn), Away.X * FMath::Sin(Turn) + Away.Y * FMath::Cos(Turn));
			State.LeaveReason = Reason;
			State.Heading = YawOf(State.LeaveDirection);
			Enter(State, EPhase::Leaving);
			if (Reason == ELeave::Scared)
			{
				Out.Events.Add(EEvent::Scared);
				State.QuietUntil = Crab.RoundSeconds + Tuning::ScareCooldown;
			}
			Out.Events.Add(EEvent::Left);
		}

		inline void BeginCircling(FState& State, const FCrabView& Crab, FStep& Out)
		{
			const float Toward = State.Random.FRandRange(-Tuning::CircleArc, Tuning::CircleArc);
			const float Distance = State.Random.FRandRange(Tuning::CircleDistanceMin, Tuning::CircleDistanceMax);
			const float Radians = FMath::DegreesToRadians(Toward);
			State.CircleCentre = Crab.Location + FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * Distance;
			State.CircleAngle = State.Random.FRandRange(0.f, 2.f * PI);
			State.Location = State.CircleCentre + FVector2D(FMath::Cos(State.CircleAngle), FMath::Sin(State.CircleAngle)) * Tuning::CircleRadius;
			State.Altitude = Tuning::CircleAltitude;
			State.Heading = FMath::RadiansToDegrees(State.CircleAngle) + 90.f;
			State.Patch = INDEX_NONE;
			State.StalkSeconds = State.BurrowSeconds = State.DanceSeconds = 0.f;
			State.LeaveReason = ELeave::None;
			++State.Count;
			Enter(State, EPhase::Circling);
			Out.Events.Add(EEvent::Circling);
		}

		inline void BeginLanding(FState& State, const FCrabView& Crab, const FEnvironment& Env, FStep& Out)
		{
			State.GlideFrom = State.Location;
			State.GlideFromAltitude = State.Altitude;
			if (!ChooseLanding(State.Random, Crab.Location, Env, State.LandingSpot, State.Patch))
			{
				BeginLeaving(State, Crab, ELeave::NoLanding, Out);
				return;
			}
			Enter(State, EPhase::Landing);
		}
	}

	/**
	 * One step of the gull and the rules that bring it. Dt in seconds. The crab is the caller's view of it, the
	 * environment where a gull can land. Out gets the events and what the gull ate. With the results panel up
	 * nothing happens: a gull that has caught the crab stays, any other goes.
	 */
	inline void Step(FState& State, const FCrabView& Crab, const FEnvironment& Env, float Dt, ESpawn Spawn, FStep& Out)
	{
		Out.Events.Reset();
		Out.EatPatch = INDEX_NONE;
		Out.EatAmount = 0.f;
		Dt = FMath::Max(Dt, 0.f);

		if (Crab.bRoundOver)
		{
			if (State.Phase != EPhase::Caught)
			{
				State.Phase = EPhase::Absent;
				State.Altitude = 0.f;
			}
			State.OutsideSeconds = 0.f;
			return;
		}

		const bool bOutside = !Crab.bInBurrow && !Crab.bMolting && Crab.WaterDepth < Tuning::LowWater;
		State.OutsideSeconds = bOutside ? State.OutsideSeconds + Dt : 0.f;

		const float Before = State.PhaseSeconds;
		State.PhaseSeconds += Dt;

		if (IsThreat(State.Phase))
		{
			State.BurrowSeconds = Crab.bInBurrow ? State.BurrowSeconds + Dt : 0.f;
			if (State.BurrowSeconds >= Tuning::BurrowGiveUpSeconds)
			{
				Detail::BeginLeaving(State, Crab, ELeave::BurrowWait, Out);
				return;
			}
		}

		switch (State.Phase)
		{
		case EPhase::Absent:
		{
			if (MaySpawn(State, Crab, Spawn))
			{
				Detail::BeginCircling(State, Crab, Out);
			}
			break;
		}
		case EPhase::Circling:
		{
			State.CircleAngle += Tuning::CircleAngularSpeed * Dt;
			State.Location = State.CircleCentre + FVector2D(FMath::Cos(State.CircleAngle), FMath::Sin(State.CircleAngle)) * Tuning::CircleRadius;
			State.Heading = FMath::RadiansToDegrees(State.CircleAngle) + 90.f;
			if (State.PhaseSeconds >= Tuning::CircleSeconds)
			{
				Detail::BeginLanding(State, Crab, Env, Out);
			}
			break;
		}
		case EPhase::Landing:
		{
			const float Glide = Tuning::LandGlideSeconds;
			const float T = FMath::Clamp(State.PhaseSeconds / Glide, 0.f, 1.f);
			const float Ease = FMath::SmoothStep(0.f, 1.f, T);
			State.Location = FMath::Lerp(State.GlideFrom, State.LandingSpot, static_cast<double>(Ease));
			State.Altitude = FMath::Lerp(State.GlideFromAltitude, 0.f, Ease);
			const FVector2D Heading = State.LandingSpot - State.GlideFrom;
			if (!Heading.IsNearlyZero())
			{
				State.Heading = Detail::YawOf(Heading);
			}
			if (Before < Glide && State.PhaseSeconds >= Glide)
			{
				Out.Events.Add(EEvent::Landed);
			}
			const float EatEnd = Glide + Tuning::LandEatSeconds;
			if (State.Patch != INDEX_NONE)
			{
				const float Meal = FMath::Clamp(FMath::Min(State.PhaseSeconds, EatEnd) - FMath::Max(Before, Glide), 0.f, Tuning::LandEatSeconds);
				if (Meal > 0.f)
				{
					Out.EatPatch = State.Patch;
					Out.EatAmount = Meal * Tuning::EatRate;
				}
			}
			if (State.PhaseSeconds >= EatEnd)
			{
				State.StalkSeconds = 0.f;
				State.DanceSeconds = 0.f;
				Detail::Enter(State, EPhase::Stalking);
				Out.Events.Add(EEvent::Stalking);
			}
			break;
		}
		case EPhase::Stalking:
		{
			State.StalkSeconds += Dt;
			const float Distance = DistanceToCrab(State, Crab);
			if (Distance <= Tuning::CatchRadius && IsCatchable(Crab))
			{
				Detail::Enter(State, EPhase::Lunging);
				break;
			}
			if (Crab.bDancing && Distance <= Tuning::ScareRange)
			{
				State.DanceSeconds += Dt;
				if (State.DanceSeconds >= Tuning::ScareDanceSeconds)
				{
					Detail::BeginLeaving(State, Crab, ELeave::Scared, Out);
					break;
				}
			}
			else
			{
				State.DanceSeconds = 0.f;
			}
			if (State.StalkSeconds >= Tuning::GiveUpSeconds)
			{
				Detail::BeginLeaving(State, Crab, ELeave::GaveUp, Out);
				break;
			}
			FVector2D Toward = Crab.Location - State.Location;
			if (Toward.Normalize())
			{
				State.Heading = Detail::YawOf(Toward);
				const float Advance = FMath::Min(Tuning::StalkSpeed * Dt, FMath::Max(Distance - Tuning::StopDistance, 0.f));
				State.Location += Toward * Advance;
			}
			break;
		}
		case EPhase::Lunging:
		{
			State.StalkSeconds += Dt;
			const float Distance = DistanceToCrab(State, Crab);
			if (!IsCatchable(Crab) || Distance > Tuning::CatchRadius + Tuning::LungeSlack)
			{
				State.DanceSeconds = 0.f;
				Detail::Enter(State, EPhase::Stalking);
				break;
			}
			FVector2D Toward = Crab.Location - State.Location;
			if (Toward.Normalize())
			{
				State.Heading = Detail::YawOf(Toward);
				State.Location += Toward * FMath::Min(Tuning::LungeSpeed * Dt, FMath::Max(Distance - Tuning::StopDistance, 0.f));
			}
			if (State.PhaseSeconds >= Tuning::LungeSeconds)
			{
				Detail::Enter(State, EPhase::Caught);
				Out.Events.Add(EEvent::Catch);
			}
			break;
		}
		case EPhase::Leaving:
		{
			State.Location += State.LeaveDirection * Tuning::LeaveSpeed * Dt;
			State.Altitude += Tuning::LeaveClimb * Dt;
			if (State.PhaseSeconds >= Tuning::LeaveSeconds)
			{
				State.Altitude = 0.f;
				State.QuietUntil = FMath::Max(State.QuietUntil, Crab.RoundSeconds + Tuning::Cooldown);
				Detail::Enter(State, EPhase::Absent);
			}
			break;
		}
		case EPhase::Caught:
			break;
		}
	}
}
