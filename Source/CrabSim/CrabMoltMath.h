// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * Molting and the round, as pure functions. A fiddler crab molts hidden in a burrow, soft and
 * vulnerable: ten seconds undisturbed, paid for with most of its food. Three molts and it is fully
 * grown, which ends the round. Every number that sets the pace of a round is in Tuning.
 */
namespace CrabMolt
{
	struct Tuning
	{
		/** Seconds in the burrow, undisturbed. */
		static constexpr float Duration = 10.f;
		/** Food the crab needs to begin. */
		static constexpr float MinFood = 0.80f;
		/** Food it costs, paid when the molt is finished. */
		static constexpr float FoodCost = 0.80f;
		/** Molts that win the round. */
		static constexpr int32 MoltsToWin = 3;
		/** Growth per molt, as a share of the starting size. It changes how the crab looks, not how it moves. */
		static constexpr float GrowthPerMolt = 0.08f;
		static constexpr float MaxGrowthScale = 1.24f;
		/** Seconds the crab stays soft after the sea floods it out of a molt, and the most grip it can have meanwhile. */
		static constexpr float SoftDuration = 30.f;
		static constexpr float SoftGripCap = 0.5f;
	};

	enum class EResult : uint8
	{
		Ok,
		RoundOver,
		InProgress,
		NotInBurrow,
		NotEnoughFood,
	};

	/** Everything the rules need to know about the crab. */
	struct FState
	{
		float Food = 0.f;
		bool bInBurrow = false;
		bool bInProgress = false;
		bool bRoundOver = false;
	};

	/** Ok if the crab may begin a molt, otherwise the first reason it may not. */
	inline EResult Evaluate(const FState& State)
	{
		if (State.bRoundOver)
		{
			return EResult::RoundOver;
		}
		if (State.bInProgress)
		{
			return EResult::InProgress;
		}
		if (!State.bInBurrow)
		{
			return EResult::NotInBurrow;
		}
		if (State.Food < Tuning::MinFood)
		{
			return EResult::NotEnoughFood;
		}
		return EResult::Ok;
	}

	/** The line for the message and the HUD. Empty for Ok and for the reasons that need no words. */
	inline const TCHAR* ReasonText(EResult Result)
	{
		switch (Result)
		{
		case EResult::NotInBurrow: return TEXT("Molt needs a burrow");
		case EResult::NotEnoughFood: return TEXT("Molt needs more food");
		default: return TEXT("");
		}
	}

	/** 0 to 1 through the molt after this many seconds in the burrow. */
	inline float Progress(float ElapsedSeconds)
	{
		return FMath::Clamp(ElapsedSeconds / Tuning::Duration, 0.f, 1.f);
	}

	/** The store after a molt is paid for. Never below 0. */
	inline float FoodAfterMolt(float Food)
	{
		return FMath::Max(Food - Tuning::FoodCost, 0.f);
	}

	/** How big the crab looks after this many molts, as a multiple of its starting size. */
	inline float GrowthScale(int32 Molts)
	{
		return FMath::Min(1.f + Tuning::GrowthPerMolt * FMath::Max(Molts, 0), Tuning::MaxGrowthScale);
	}

	inline bool IsWon(int32 Molts)
	{
		return Molts >= Tuning::MoltsToWin;
	}

	/** The most grip the crab can have: all of it, or half while it is soft. */
	inline float GripCap(bool bSoft)
	{
		return bSoft ? Tuning::SoftGripCap : 1.f;
	}

	inline float ClampGrip(float Grip, bool bSoft)
	{
		return FMath::Clamp(Grip, 0.f, GripCap(bSoft));
	}

	/** Soft seconds left after Seconds have passed. Never below 0. */
	inline float SoftAfter(float Remaining, float Seconds)
	{
		return FMath::Max(Remaining - FMath::Max(Seconds, 0.f), 0.f);
	}

	/** The best time so far after a round of Seconds. A best of 0 or less means there is none yet. */
	inline float BestAfter(float Best, float Seconds)
	{
		return (Best <= 0.f || Seconds < Best) ? Seconds : Best;
	}

	/** A time as minutes and seconds, "8:05". */
	inline FString TimeText(float Seconds)
	{
		const int32 Whole = FMath::Max(FMath::FloorToInt(Seconds), 0);
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	}
}
