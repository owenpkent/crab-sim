// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * Where things sit on the HUD, as pure functions of the view size, so the
 * canvas that draws them and the controller that clicks them agree.
 */
namespace CrabHud
{
	/** Side of the dig button at scale 1, px. Scale 1 is a 720p window. */
	constexpr float DigButtonSize = 140.f;
	/** Extra px around the button that still count as a click on it, at scale 1. */
	constexpr float DigButtonHitMargin = 10.f;

	/** The readout scales with the window, so it is as big to read at 4K as at 720p. */
	inline float ScaleForHeight(float ViewHeight)
	{
		return FMath::Clamp(ViewHeight / 1080.f, 0.8f, 2.5f) * 1.25f;
	}

	/** The dig button: bottom right, above the hint line. */
	inline FBox2D DigButtonRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const float Size = DigButtonSize * Scale;
		const float Right = ViewWidth - 30.f * Scale;
		const float Bottom = ViewHeight - 60.f * Scale;
		return FBox2D(FVector2D(Right - Size, Bottom - Size), FVector2D(Right, Bottom));
	}

	inline bool HitsDigButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		const float Margin = DigButtonHitMargin * ScaleForHeight(ViewHeight);
		return DigButtonRect(ViewWidth, ViewHeight).ExpandBy(Margin).IsInside(Pixel);
	}
}
