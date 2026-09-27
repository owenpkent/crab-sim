// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "CrabTide.generated.h"

/**
 * The tide as a function of time. T = 0 is low tide, rising. A slow swell rides
 * on top of the tide, so the water line breathes in and out. Everything here is
 * pure, so the level at any moment can be tested and predicted.
 */
USTRUCT(BlueprintType)
struct CRABSIM_API FCrabTideSettings
{
	GENERATED_BODY()

	/** Water surface at low tide, world Z in uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tide")
	float LowLevel = -190.f;

	/** Water surface at high tide, world Z in uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tide")
	float HighLevel = 90.f;

	/** Seconds for one full low, high, low cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tide")
	float Period = 180.f;

	/** Height of the swell riding on the tide, uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tide")
	float SwellHeight = 9.f;

	/** Seconds between swell crests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tide")
	float SwellPeriod = 7.f;

	/** The tide alone, no swell. */
	float TideLevelAt(float Time) const
	{
		const float Mid = 0.5f * (LowLevel + HighLevel);
		const float Amplitude = 0.5f * (HighLevel - LowLevel);
		return Mid - Amplitude * FMath::Cos(2.f * PI * Time / Period);
	}

	/** The water surface the crab meets: tide plus swell. */
	float SurfaceLevelAt(float Time) const
	{
		return TideLevelAt(Time) + SwellHeight * FMath::Sin(2.f * PI * Time / SwellPeriod);
	}

	/**
	 * Seconds from Time until the surface first stands above Level, looking ahead at most Horizon seconds in
	 * Step seconds. 0 if it does already, BIG_NUMBER if it does not within the horizon.
	 */
	float SecondsUntilSurfaceAbove(float Time, float Level, float Horizon, float Step = 0.5f) const
	{
		for (float Ahead = 0.f; Ahead <= Horizon; Ahead += FMath::Max(Step, KINDA_SMALL_NUMBER))
		{
			if (SurfaceLevelAt(Time + Ahead) > Level)
			{
				return Ahead;
			}
		}
		return BIG_NUMBER;
	}

	/** 0 at low tide, 1 at high tide. */
	float FractionAt(float Time) const
	{
		return FMath::Clamp((TideLevelAt(Time) - LowLevel) / FMath::Max(HighLevel - LowLevel, KINDA_SMALL_NUMBER), 0.f, 1.f);
	}

	/** True from low tide up to high tide, and false from high tide back down to low. Low tide itself is the start of the rise. */
	bool IsRisingAt(float Time) const
	{
		return FMath::Fmod(FMath::Max(Time, 0.f), Period) < 0.5f * Period;
	}

	/** Seconds until the tide next turns, high to low or low to high. */
	float SecondsToTurnAt(float Time) const
	{
		const float Phase = FMath::Fmod(FMath::Max(Time, 0.f), Period);
		const float Half = 0.5f * Period;
		return Phase < Half ? Half - Phase : Period - Phase;
	}
};
