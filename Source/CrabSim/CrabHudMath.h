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
	constexpr float ResultsPanelHeight = 410.f;
	/** Side of one molt pip and the gap between pips, at scale 1. */
	constexpr float MoltPipSize = 26.f;
	constexpr float MoltPipGap = 10.f;
	/** The FOOD and BURROW buttons, at scale 1: big targets, hit margin as for the dig button. */
	constexpr float GotoButtonWidth = 170.f;
	constexpr float GotoButtonHeight = 100.f;
	/** Room between the go-to column and the MOLT and DIG column, for their reason lines, at scale 1. */
	constexpr float GotoColumnGap = 110.f;
	/** The help text stays whole this long at the start of a round, s. */
	constexpr float FullHintSeconds = 60.f;
	/** The help text is never smaller than this on screen, px at scale 1. */
	constexpr float HintMinPixels = 16.f;

	/** The readout scales with the window, so it is as big to read at 4K as at 720p. */
	inline float ScaleForHeight(float ViewHeight)
	{
		return FMath::Clamp(ViewHeight / 1080.f, 0.8f, 2.5f) * 1.25f;
	}

	/** The dig button: bottom right. */
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

	/** The tide gauge bar, left of the view and a quarter of the way down. */
	inline FBox2D TideGaugeBarRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Min(30.f * Scale, ViewHeight * 0.25f);
		return FBox2D(Min, Min + FVector2D(28.f * Scale, ViewHeight * 0.4f));
	}

	/** The tide gauge with its labels: HIGH above, LOW and the rising or falling line below. */
	inline FBox2D TideGaugeRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FBox2D Bar = TideGaugeBarRect(ViewWidth, ViewHeight);
		return FBox2D(Bar.Min - FVector2D(14.f * Scale, 32.f * Scale), FVector2D(Bar.Min.X + 150.f * Scale, Bar.Max.Y + 56.f * Scale));
	}

	/**
	 * The BURROW button, in a column left of the MOLT and DIG column, its foot clear above the molt pips. The
	 * gap to that column leaves room for the reason lines of DIG and MOLT.
	 */
	inline FBox2D BurrowButtonRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Size(GotoButtonWidth * Scale, GotoButtonHeight * Scale);
		const float Right = MoltButtonRect(ViewWidth, ViewHeight).Min.X - GotoColumnGap * Scale;
		const float Bottom = MoltPipRect(ViewWidth, ViewHeight, 0).Min.Y - 40.f * Scale;
		return FBox2D(FVector2D(Right - Size.X, Bottom - Size.Y), FVector2D(Right, Bottom));
	}

	/** The FOOD button, straight above the BURROW button, with room between them for the BURROW button's reason line. */
	inline FBox2D FoodButtonRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FBox2D Burrow = BurrowButtonRect(ViewWidth, ViewHeight);
		const FVector2D Size = Burrow.GetSize();
		const float Bottom = Burrow.Min.Y - 56.f * Scale;
		return FBox2D(FVector2D(Burrow.Min.X, Bottom - Size.Y), FVector2D(Burrow.Max.X, Bottom));
	}

	inline bool HitsBurrowButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		const float Margin = DigButtonHitMargin * ScaleForHeight(ViewHeight);
		return BurrowButtonRect(ViewWidth, ViewHeight).ExpandBy(Margin).IsInside(Pixel);
	}

	inline bool HitsFoodButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		const float Margin = DigButtonHitMargin * ScaleForHeight(ViewHeight);
		return FoodButtonRect(ViewWidth, ViewHeight).ExpandBy(Margin).IsInside(Pixel);
	}

	/** Any of the four buttons that are always up: FOOD, BURROW, MOLT and DIG. A click here is never a click on the ground or on a burrow behind it. */
	inline bool HitsAnyButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		return HitsFoodButton(ViewWidth, ViewHeight, Pixel) || HitsBurrowButton(ViewWidth, ViewHeight, Pixel)
			|| HitsMoltButton(ViewWidth, ViewHeight, Pixel) || HitsDigButton(ViewWidth, ViewHeight, Pixel);
	}

	/**
	 * The most room the help text has: bottom left, on a dark panel, ending clear of the grip bar. The text
	 * is drawn inside it, in one or two lines.
	 */
	inline FBox2D HintPanelRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const float Left = 24.f * Scale;
		const float Right = FMath::Max(GripBarRect(ViewWidth, ViewHeight).Min.X - 20.f * Scale, Left);
		const float Bottom = ViewHeight - 14.f * Scale;
		return FBox2D(FVector2D(Left, Bottom - 72.f * Scale), FVector2D(Right, Bottom));
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

	/** The help text: up to two short lines. */
	struct FHintText
	{
		FString First;
		FString Second;
	};

	/** All the controls for the first minute of a round, then one short line for what the crab is doing. */
	inline FHintText HintText(float RoundSeconds, bool bInBurrow, bool bMolting)
	{
		if (RoundSeconds < FullHintSeconds)
		{
			return {TEXT("Click: walk. Hold: follow."), TEXT("Right click: dash. Click crab: dance.")};
		}
		if (bMolting)
		{
			return {TEXT("Molting. Click elsewhere to cancel."), FString()};
		}
		if (bInBurrow)
		{
			return {TEXT("Click elsewhere to come out."), FString()};
		}
		return {TEXT("Click: walk. Right click: dash."), FString()};
	}

	/**
	 * True if the message only says again what a button's reason line already shows: the same words, or
	 * the reason after a lead-in ("Cannot dig: too close to a burrow" against "Too close to a burrow").
	 */
	inline bool EchoesReason(const FString& Message, const FString& Reason)
	{
		return !Reason.IsEmpty() && Message.EndsWith(Reason, ESearchCase::IgnoreCase);
	}
}
