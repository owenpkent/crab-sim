// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * When and how the crab can dig a burrow, as pure functions. Digging is four
 * seconds of standing still on dry sand, and it costs food.
 */
namespace CrabDig
{
	/** Seconds of standing still. */
	constexpr float Duration = 4.f;
	/** Food it costs, paid when the burrow is finished. */
	constexpr float FoodCost = 0.30f;
	/** No new burrow this close (XY, uu) to the centre of any burrow or food patch. */
	constexpr float MinSpacing = 250.f;
	/** Dug burrows alive at once. */
	constexpr int32 MaxDug = 3;

	enum class EResult : uint8
	{
		Ok,
		NoGround,
		InBurrow,
		TooManyDug,
		NotEnoughFood,
		Wet,
		TooCloseToBurrow,
		TooCloseToPatch,
	};

	/** Everything the rules need to know about the crab and the spot it stands on. */
	struct FSpot
	{
		float Food = 0.f;
		/** Water over the sand at the spot, uu. */
		float WaterDepth = 0.f;
		bool bInSurge = false;
		bool bInBurrow = false;
		int32 DugCount = 0;
		/** Distance (XY) to the nearest burrow centre and food patch centre. Huge when there are none. */
		float NearestBurrowDistance = BIG_NUMBER;
		float NearestPatchDistance = BIG_NUMBER;
	};

	/** Ok if the crab may start digging here, otherwise the first reason it may not. */
	inline EResult Evaluate(const FSpot& Spot)
	{
		if (Spot.bInBurrow)
		{
			return EResult::InBurrow;
		}
		if (Spot.DugCount >= MaxDug)
		{
			return EResult::TooManyDug;
		}
		if (Spot.Food < FoodCost)
		{
			return EResult::NotEnoughFood;
		}
		if (Spot.WaterDepth > 0.f || Spot.bInSurge)
		{
			return EResult::Wet;
		}
		if (Spot.NearestBurrowDistance < MinSpacing)
		{
			return EResult::TooCloseToBurrow;
		}
		if (Spot.NearestPatchDistance < MinSpacing)
		{
			return EResult::TooCloseToPatch;
		}
		return EResult::Ok;
	}

	/** Short reason, lower case, for the HUD and the message line. Empty for Ok. */
	inline const TCHAR* ReasonText(EResult Result)
	{
		switch (Result)
		{
		case EResult::NoGround: return TEXT("no ground here");
		case EResult::InBurrow: return TEXT("already in a burrow");
		case EResult::TooManyDug: return TEXT("3 burrows dug already");
		case EResult::NotEnoughFood: return TEXT("not enough food");
		case EResult::Wet: return TEXT("the sand is wet");
		case EResult::TooCloseToBurrow: return TEXT("too close to a burrow");
		case EResult::TooCloseToPatch: return TEXT("too close to a food patch");
		default: return TEXT("");
		}
	}

	/** 0 to 1 through the dig after this many seconds of standing still. */
	inline float Progress(float ElapsedSeconds)
	{
		return FMath::Clamp(ElapsedSeconds / Duration, 0.f, 1.f);
	}
}
