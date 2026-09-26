// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * Foraging as pure functions. Food is the crab's store, 0 to 1. A patch of mud
 * holds richness, 0 (bare) to 1 (thick with algae). Sifting moves richness into
 * food until the patch is bare or the crab is full.
 */
namespace CrabFood
{
	/** What the crab starts the round with. */
	constexpr float StartFood = 0.25f;
	/** Food gained per second from a full patch. Slow on purpose: gathering a molt's worth takes the better part of a minute, so the tide sets the pace of a round. */
	constexpr float FeedRatePerSecond = 0.025f;
	/** A nearly bare patch still feeds at this share of the full rate, so every patch runs out in finite time. */
	constexpr float FeedRateFloor = 0.3f;
	/** Food lost per second, always. Slow: a 180 s tide costs about 0.7. */
	constexpr float DrainPerSecond = 0.004f;
	/** Water this deep over a patch soaks it. When the water leaves, the patch is fresh again. */
	constexpr float SoakDepth = 10.f;
	/** At or below this a patch counts as bare. */
	constexpr float EmptyRichness = 0.005f;
	/** At or above this the crab counts as full and stops eating. It is a band, not a point: hunger soon takes it back under. */
	constexpr float FullFood = 0.98f;

	inline bool IsEmpty(float Richness)
	{
		return Richness <= EmptyRichness;
	}

	inline bool IsFull(float Food)
	{
		return Food >= FullFood;
	}

	/** Food per second from a patch: the full rate when it is full, easing down to the floor rate as it runs out, none when bare. */
	inline float FeedRate(float Richness)
	{
		if (IsEmpty(Richness))
		{
			return 0.f;
		}
		return FeedRatePerSecond * FMath::Lerp(FeedRateFloor, 1.f, FMath::Clamp(Richness, 0.f, 1.f));
	}

	/** How much moves from the patch into the crab over Seconds: the rate, capped by what the patch holds and by the room left in the crab. */
	inline float FeedTransfer(float Richness, float Food, float Seconds)
	{
		if (IsEmpty(Richness) || IsFull(Food))
		{
			return 0.f;
		}
		const float Wanted = FeedRate(Richness) * FMath::Max(Seconds, 0.f);
		return FMath::Max(FMath::Min3(Wanted, Richness, 1.f - Food), 0.f);
	}

	/** The store after Seconds of slow drain. Never below 0: hunger does not kill the crab yet. */
	inline float FoodAfterDrain(float Food, float Seconds)
	{
		return FMath::Max(Food - DrainPerSecond * FMath::Max(Seconds, 0.f), 0.f);
	}

	inline bool IsSoaked(float WaterDepth)
	{
		return WaterDepth > SoakDepth;
	}
}
