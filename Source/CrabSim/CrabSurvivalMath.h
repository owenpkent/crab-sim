// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * What water does to the crab, as pure functions of depth. Grip runs from 0 to
 * 1. Lose all of it and the sea carries the crab away.
 */
namespace CrabSurvival
{
	/** Feet wet. Below this the water has no effect. */
	constexpr float WadeDepth = 15.f;
	/** Above this the water knocks the crab about and it starts to lose its grip. */
	constexpr float SurgeDepth = 45.f;
	/** Above this the crab is out of its depth. */
	constexpr float DeepDepth = 60.f;

	/** Walking speed multiplier: 1 when dry, easing down to 0.6 out of depth. */
	inline float SpeedScaleForDepth(float Depth)
	{
		if (Depth <= WadeDepth)
		{
			return 1.f;
		}
		const float T = FMath::Clamp((Depth - WadeDepth) / (DeepDepth - WadeDepth), 0.f, 1.f);
		return FMath::Lerp(1.f, 0.6f, T);
	}

	/** Grip lost per second. Zero until the surge depth, then rising to a cap. */
	inline float GripDrainPerSecond(float Depth)
	{
		if (Depth <= SurgeDepth)
		{
			return 0.f;
		}
		return 0.12f * FMath::Min((Depth - SurgeDepth) / 100.f, 2.f);
	}

	/** Grip regained per second: fast in a burrow, slower on dry ground, none in the water. */
	inline float GripRegenPerSecond(float Depth, bool bInBurrow)
	{
		if (bInBurrow)
		{
			return 0.5f;
		}
		return Depth < WadeDepth ? 0.2f : 0.f;
	}

	/** Sideways shove from the water, uu/s. Zero until the surge depth, capped at 120. */
	inline float SurgePushSpeed(float Depth)
	{
		if (Depth <= SurgeDepth)
		{
			return 0.f;
		}
		return 120.f * FMath::Min((Depth - SurgeDepth) / 100.f, 1.f);
	}
}
