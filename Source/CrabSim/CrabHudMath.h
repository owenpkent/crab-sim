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
	/** The molt button is the same size, and the new round button on the results panel is bigger. Hit margins are the same. */
	constexpr float MoltButtonSize = DigButtonSize;
	constexpr float NewRoundButtonWidth = 300.f;
	constexpr float NewRoundButtonHeight = 96.f;
	constexpr float ResultsPanelWidth = 560.f;
	constexpr float ResultsPanelHeight = 440.f;
	/** Side of one molt pip and the gap between pips, at scale 1. */
	constexpr float MoltPipSize = 26.f;
	constexpr float MoltPipGap = 10.f;

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

	/** The molt button: straight above the dig button, with room between them for the dig button's reason line. */
	inline FBox2D MoltButtonRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const float Size = MoltButtonSize * Scale;
		const FBox2D Dig = DigButtonRect(ViewWidth, ViewHeight);
		const float Bottom = Dig.Min.Y - 56.f * Scale;
		return FBox2D(FVector2D(Dig.Max.X - Size, Bottom - Size), FVector2D(Dig.Max.X, Bottom));
	}

	inline bool HitsMoltButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		const float Margin = DigButtonHitMargin * ScaleForHeight(ViewHeight);
		return MoltButtonRect(ViewWidth, ViewHeight).ExpandBy(Margin).IsInside(Pixel);
	}

	/** The grip bar, bottom middle. */
	inline FBox2D GripBarRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const float Width = 300.f * Scale;
		const float X = (ViewWidth - Width) * 0.5f;
		const float Y = ViewHeight - 70.f * Scale;
		return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + 16.f * Scale));
	}

	/** The food bar, beside the grip bar on its right. */
	inline FBox2D FoodBarRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FBox2D Grip = GripBarRect(ViewWidth, ViewHeight);
		const float X = Grip.Max.X + 36.f * Scale;
		return FBox2D(FVector2D(X, Grip.Min.Y), FVector2D(X + 200.f * Scale, Grip.Max.Y));
	}

	/** One of the three molt pips, in a row above the food bar's label. */
	inline FBox2D MoltPipRect(float ViewWidth, float ViewHeight, int32 Index)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FBox2D Food = FoodBarRect(ViewWidth, ViewHeight);
		const float X = Food.Min.X + Index * (MoltPipSize + MoltPipGap) * Scale;
		const float Y = Food.Min.Y - 64.f * Scale;
		return FBox2D(FVector2D(X, Y), FVector2D(X + MoltPipSize * Scale, Y + MoltPipSize * Scale));
	}

	/** The results panel, in the middle of the view. */
	inline FBox2D ResultsPanelRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Size(ResultsPanelWidth * Scale, ResultsPanelHeight * Scale);
		const FVector2D Min((ViewWidth - Size.X) * 0.5f, (ViewHeight - Size.Y) * 0.5f);
		return FBox2D(Min, Min + Size);
	}

	/** The new round button, at the foot of the results panel. */
	inline FBox2D NewRoundButtonRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FBox2D Panel = ResultsPanelRect(ViewWidth, ViewHeight);
		const FVector2D Size(NewRoundButtonWidth * Scale, NewRoundButtonHeight * Scale);
		const FVector2D Min(Panel.GetCenter().X - Size.X * 0.5f, Panel.Max.Y - 28.f * Scale - Size.Y);
		return FBox2D(Min, Min + Size);
	}

	inline bool HitsNewRoundButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		const float Margin = DigButtonHitMargin * ScaleForHeight(ViewHeight);
		return NewRoundButtonRect(ViewWidth, ViewHeight).ExpandBy(Margin).IsInside(Pixel);
	}
}
