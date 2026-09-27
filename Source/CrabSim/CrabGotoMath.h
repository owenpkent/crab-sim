// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "CrabFoodMath.h"

/**
 * Which food patch or burrow the FOOD and BURROW buttons send the crab to, as pure functions. A press
 * is the same as clicking that patch or burrow. Both leave out what the sea would take before the crab
 * could use it, so one press is always worth making. FOOD picks the best patch, not the nearest: how much
 * it holds, cut down by the walk.
 */
namespace CrabGoto
{
	struct Tuning
	{
		/** How fast the crab is assumed to cover ground, uu/s. Under its side speed (450), so the estimate errs safe. */
		static constexpr float AssumedSpeed = 350.f;
		/** A patch must stay clear of the water this long after the crab gets there, s. */
		static constexpr float FeedLeadSeconds = 8.f;
		/** A burrow must stay unflooded this long after the crab gets there, s. */
		static constexpr float BurrowLeadSeconds = 3.f;
		/** The safest burrow is picked from those within this distance, uu. */
		static constexpr float SafeRange = 3500.f;
		/** Burrow floors within this many uu of the highest count as level, and the nearer of them wins. */
		static constexpr float FloorTie = 10.f;
		/** A patch's worth is halved by a walk this long, uu: richness / (1 + distance / DistanceFalloff). */
		static constexpr float DistanceFalloff = 1500.f;
		/** A patch holding less than this is only picked when no richer one is usable. */
		static constexpr float MinRichness = 0.25f;
	};

	/** What the rules need to know about a food patch. Distance is XY, from the crab to the middle. */
	struct FPatch
	{
		float Distance = 0.f;
		float Richness = 0.f;
		/** Water over the patch now, uu. */
		float WaterDepth = 0.f;
		/** Seconds until the water over it is deeper than CrabFood::SoakDepth. BIG_NUMBER when it never is in sight. */
		float SecondsUntilSoaked = BIG_NUMBER;
	};

	/** What the rules need to know about a burrow. Distance is XY, from the crab to the hole. */
	struct FBurrow
	{
		float Distance = 0.f;
		/** World Z of the floor of the hole: the higher, the later it floods. */
		float FloorZ = 0.f;
		bool bFlooded = false;
		/** Seconds until it floods. BIG_NUMBER when it never does in sight. */
		float SecondsUntilFlooded = BIG_NUMBER;
	};

	enum class EFoodResult : uint8
	{
		Ok,
		RoundOver,
		NotHungry,
		NoPatch,
	};

	enum class EBurrowResult : uint8
	{
		Ok,
		RoundOver,
		InBurrow,
		NoBurrow,
	};

	/** Seconds the crab needs to cover a distance, at the assumed speed. */
	inline float ArrivalSeconds(float Distance)
	{
		return FMath::Max(Distance, 0.f) / Tuning::AssumedSpeed;
	}

	/** A patch worth walking to: it holds food, is not under water, and stays dry long enough after the crab gets there. */
	inline bool IsPatchUsable(const FPatch& Patch)
	{
		return !CrabFood::IsEmpty(Patch.Richness)
			&& Patch.WaterDepth <= CrabFood::SoakDepth
			&& Patch.SecondsUntilSoaked >= ArrivalSeconds(Patch.Distance) + Tuning::FeedLeadSeconds;
	}

	/** A burrow worth walking to: not flooded, and not flooded by the time the crab gets there. */
	inline bool IsBurrowUsable(const FBurrow& Burrow)
	{
		return !Burrow.bFlooded && Burrow.SecondsUntilFlooded >= ArrivalSeconds(Burrow.Distance) + Tuning::BurrowLeadSeconds;
	}

	/** What a patch is worth walking to: the richness it holds, cut down by the distance. */
	inline float PatchScore(const FPatch& Patch)
	{
		return Patch.Richness / (1.f + FMath::Max(Patch.Distance, 0.f) / Tuning::DistanceFalloff);
	}

	/**
	 * The best usable patch by PatchScore (the lower index on a tie), or INDEX_NONE. A patch holding less than
	 * MinRichness is passed over unless no richer one is usable.
	 */
	inline int32 PickPatch(TArrayView<const FPatch> Patches)
	{
		int32 Best = INDEX_NONE;
		int32 BestPoor = INDEX_NONE;
		for (int32 Index = 0; Index < Patches.Num(); ++Index)
		{
			if (!IsPatchUsable(Patches[Index]))
			{
				continue;
			}
			int32& Slot = Patches[Index].Richness >= Tuning::MinRichness ? Best : BestPoor;
			if (Slot == INDEX_NONE || PatchScore(Patches[Index]) > PatchScore(Patches[Slot]))
			{
				Slot = Index;
			}
		}
		return Best != INDEX_NONE ? Best : BestPoor;
	}

	/**
	 * The safest burrow: of the usable ones within SafeRange, the highest floor (the nearer, then the lower
	 * index, among floors that are level), otherwise the nearest usable one at any distance, or INDEX_NONE.
	 */
	inline int32 PickBurrow(TArrayView<const FBurrow> Burrows)
	{
		bool bAnySafe = false;
		float HighestFloor = 0.f;
		for (const FBurrow& Burrow : Burrows)
		{
			if (IsBurrowUsable(Burrow) && Burrow.Distance <= Tuning::SafeRange)
			{
				HighestFloor = bAnySafe ? FMath::Max(HighestFloor, Burrow.FloorZ) : Burrow.FloorZ;
				bAnySafe = true;
			}
		}

		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Burrows.Num(); ++Index)
		{
			const FBurrow& Burrow = Burrows[Index];
			if (!IsBurrowUsable(Burrow))
			{
				continue;
			}
			if (bAnySafe && !(Burrow.Distance <= Tuning::SafeRange && Burrow.FloorZ >= HighestFloor - Tuning::FloorTie))
			{
				continue;
			}
			if (Best == INDEX_NONE || Burrow.Distance < Burrows[Best].Distance)
			{
				Best = Index;
			}
		}
		return Best;
	}

	/** Ok, and the patch in OutPatch, if the FOOD button may be pressed, otherwise the first reason it may not. */
	inline EFoodResult EvaluateFood(bool bRoundOver, float Food, TArrayView<const FPatch> Patches, int32& OutPatch)
	{
		OutPatch = INDEX_NONE;
		if (bRoundOver)
		{
			return EFoodResult::RoundOver;
		}
		if (CrabFood::IsFull(Food))
		{
			return EFoodResult::NotHungry;
		}
		OutPatch = PickPatch(Patches);
		return OutPatch == INDEX_NONE ? EFoodResult::NoPatch : EFoodResult::Ok;
	}

	/** Ok, and the burrow in OutBurrow, if the BURROW button may be pressed, otherwise the first reason it may not. */
	inline EBurrowResult EvaluateBurrow(bool bRoundOver, bool bInBurrow, TArrayView<const FBurrow> Burrows, int32& OutBurrow)
	{
		OutBurrow = INDEX_NONE;
		if (bRoundOver)
		{
			return EBurrowResult::RoundOver;
		}
		if (bInBurrow)
		{
			return EBurrowResult::InBurrow;
		}
		OutBurrow = PickBurrow(Burrows);
		return OutBurrow == INDEX_NONE ? EBurrowResult::NoBurrow : EBurrowResult::Ok;
	}

	/** The line for the HUD and the message line. Empty for Ok and for the reasons that need no words. */
	inline const TCHAR* ReasonText(EFoodResult Result)
	{
		switch (Result)
		{
		case EFoodResult::NotHungry: return TEXT("Not hungry");
		case EFoodResult::NoPatch: return TEXT("No dry food left");
		default: return TEXT("");
		}
	}

	inline const TCHAR* ReasonText(EBurrowResult Result)
	{
		switch (Result)
		{
		case EBurrowResult::InBurrow: return TEXT("Already dug in");
		case EBurrowResult::NoBurrow: return TEXT("No dry burrow near");
		default: return TEXT("");
		}
	}
}
