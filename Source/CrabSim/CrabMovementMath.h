// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * Pure movement rules for the crab, kept free of engine state so they can be
 * unit tested without a world. Angles are yaw degrees, UE convention: yaw 0
 * faces +X and positive yaw turns toward +Y. The crab's side axis is its facing
 * yaw plus or minus 90.
 */
namespace CrabMovementMath
{
	/**
	 * 0 when travel runs along the crab's facing axis (walking forward or
	 * backward, slow), 1 when it runs along the side axis (scuttling, fast).
	 */
	inline float SidewaysAlignment(float FacingYawDeg, float MoveYawDeg)
	{
		return FMath::Abs(FMath::Sin(FMath::DegreesToRadians(MoveYawDeg - FacingYawDeg)));
	}

	inline float SpeedForAlignment(float Alignment, float ForwardSpeed, float SideSpeed)
	{
		return FMath::Lerp(ForwardSpeed, SideSpeed, FMath::Clamp(Alignment, 0.f, 1.f));
	}

	/**
	 * The facing that puts a side axis on the direction of travel. Of the two
	 * candidates (left side or right side leading) it returns the one nearer the
	 * current yaw, so the crab never turns more than 90 degrees to scuttle.
	 */
	inline float SideFacingYaw(float CurrentYawDeg, float MoveYawDeg)
	{
		const float RightLeads = FRotator::NormalizeAxis(MoveYawDeg - 90.f);
		const float LeftLeads = FRotator::NormalizeAxis(MoveYawDeg + 90.f);
		const float ToRight = FMath::Abs(FRotator::NormalizeAxis(RightLeads - CurrentYawDeg));
		const float ToLeft = FMath::Abs(FRotator::NormalizeAxis(LeftLeads - CurrentYawDeg));
		return ToRight <= ToLeft ? RightLeads : LeftLeads;
	}
}
