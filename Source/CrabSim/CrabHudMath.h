// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "CrabOrbitMath.h"

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
	/** The gull banner, top left, above the tide gauge and out of the way of the gull's own approach, at scale 1. */
	constexpr float GullBannerWidth = 380.f;
	constexpr float GullBannerHeight = 56.f;
	constexpr float GullBannerTop = 8.f;
	constexpr float GullBannerLeft = 24.f;
	/** The gull arrow's box (the arrow and its distance text), half its width and height, and its gap to the view's edge, at scale 1. */
	constexpr float GullArrowHalfWidth = 64.f;
	constexpr float GullArrowHalfHeight = 60.f;
	constexpr float GullArrowInset = 10.f;
	/** The ground seen from the camera's pitch is squashed on screen: a world step along the view's depth shows this much as tall as a step across it is wide. */
	constexpr float GroundForeshortening = 0.62f;

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

	/** The gull banner: top left, clear of the tide gauge under it and of the message line beside it. */
	inline FBox2D GullBannerRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Size(GullBannerWidth * Scale, GullBannerHeight * Scale);
		const FVector2D Min(GullBannerLeft * Scale, GullBannerTop * Scale);
		return FBox2D(Min, Min + Size);
	}

	/**
	 * Which way the gull is on screen, as a unit vector from the middle of the view (x right, y down). A gull
	 * further along the camera's forward is up and one to its right is right: at camera yaw 0 the camera looks
	 * toward +X with +Y on its right. Up is foreshortened by the pitch (Foreshortening, CrabOrbit::GroundForeshortening
	 * for an orbited camera). Up when the gull is on the crab.
	 */
	inline FVector2D GullArrowDirection(const FVector2D& CrabXY, const FVector2D& GullXY, float CameraYaw = 0.f, float Foreshortening = GroundForeshortening)
	{
		const FVector2D Seen = CrabOrbit::WorldToCamera(GullXY - CrabXY, CameraYaw);
		FVector2D Screen(Seen.X, -Seen.Y * Foreshortening);
		if (!Screen.Normalize())
		{
			return FVector2D(0.0, -1.0);
		}
		return Screen;
	}

	/** Everything else on the HUD that a gull banner or arrow must keep clear of, with room for the text that comes and goes (reason lines, labels, the message). */
	inline TArray<FBox2D> GullKeepClearZones(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Wide(10.f * Scale, 10.f * Scale);
		TArray<FBox2D> Zones;
		Zones.Add(TideGaugeRect(ViewWidth, ViewHeight).ExpandBy(8.f * Scale));
		Zones.Add(HintPanelRect(ViewWidth, ViewHeight).ExpandBy(6.f * Scale));

		const FBox2D Grip = GripBarRect(ViewWidth, ViewHeight);
		Zones.Add(FBox2D(Grip.Min - FVector2D(10.f, 44.f) * Scale, Grip.Max + Wide));
		const FBox2D Food = FoodBarRect(ViewWidth, ViewHeight);
		const FBox2D FirstPip = MoltPipRect(ViewWidth, ViewHeight, 0);
		const FBox2D LastPip = MoltPipRect(ViewWidth, ViewHeight, 2);
		const float FoodRight = FMath::Max(Food.Max.X, LastPip.Max.X + 110.f * Scale);
		Zones.Add(FBox2D(FirstPip.Min - FVector2D(10.f, 8.f) * Scale, FVector2D(FoodRight, Food.Max.Y) + Wide));

		// Each button with its reason line above it, right-aligned to the button and growing left as far as its words run.
		const float Up = 50.f * Scale;
		const float Margin = DigButtonHitMargin * Scale + 4.f * Scale;
		const FBox2D Dig = DigButtonRect(ViewWidth, ViewHeight);
		const FBox2D Molt = MoltButtonRect(ViewWidth, ViewHeight);
		const FBox2D Burrow = BurrowButtonRect(ViewWidth, ViewHeight);
		const FBox2D FoodButton = FoodButtonRect(ViewWidth, ViewHeight);
		Zones.Add(FBox2D(FVector2D(Dig.Min.X - 250.f * Scale, Dig.Min.Y - Up), Dig.Max + FVector2D(Margin, Margin)));
		Zones.Add(FBox2D(FVector2D(Molt.Min.X - 250.f * Scale, Molt.Min.Y - Up), Molt.Max + FVector2D(Margin, Margin)));
		Zones.Add(FBox2D(FVector2D(Burrow.Min.X - Margin - 40.f * Scale, Burrow.Min.Y - Up), Burrow.Max + FVector2D(Margin, Margin)));
		Zones.Add(FBox2D(FVector2D(FoodButton.Min.X - Margin - 40.f * Scale, FoodButton.Min.Y - Up), FoodButton.Max + FVector2D(Margin, Margin)));

		// The message line: middle of the top, a line or two of big text.
		Zones.Add(FBox2D(FVector2D(ViewWidth * 0.5f - 330.f * Scale, ViewHeight * 0.12f - 12.f * Scale), FVector2D(ViewWidth * 0.5f + 330.f * Scale, ViewHeight * 0.12f + 46.f * Scale)));
		return Zones;
	}

	/**
	 * Where the gull arrow sits: on the frame just inside the view's edge in the gull's direction, moved to the nearest
	 * spot where its box (the arrow and the distance under it) keeps clear of the banner and of every HUD element,
	 * and of the Extra zones the caller adds (the gull itself, when it is on screen).
	 */
	inline FBox2D GullArrowBox(float ViewWidth, float ViewHeight, const FVector2D& Direction, const TArray<FBox2D>& Extra = TArray<FBox2D>())
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Half(GullArrowHalfWidth * Scale, GullArrowHalfHeight * Scale);
		const FVector2D Low = Half + FVector2D(GullArrowInset * Scale, GullArrowInset * Scale);
		const FVector2D High = FVector2D(ViewWidth, ViewHeight) - Low;
		const FVector2D Middle(ViewWidth * 0.5f, ViewHeight * 0.5f);

		double Reach = BIG_NUMBER;
		if (Direction.X > 1e-6)
		{
			Reach = FMath::Min(Reach, (High.X - Middle.X) / Direction.X);
		}
		else if (Direction.X < -1e-6)
		{
			Reach = FMath::Min(Reach, (Low.X - Middle.X) / Direction.X);
		}
		if (Direction.Y > 1e-6)
		{
			Reach = FMath::Min(Reach, (High.Y - Middle.Y) / Direction.Y);
		}
		else if (Direction.Y < -1e-6)
		{
			Reach = FMath::Min(Reach, (Low.Y - Middle.Y) / Direction.Y);
		}
		const FVector2D Ideal = Reach >= BIG_NUMBER ? FVector2D(Middle.X, Low.Y) : Middle + Direction * Reach;

		TArray<FBox2D> Zones = GullKeepClearZones(ViewWidth, ViewHeight);
		Zones.Add(GullBannerRect(ViewWidth, ViewHeight).ExpandBy(8.f * Scale));
		Zones.Append(Extra);
		auto IsFree = [&](const FVector2D& Centre)
		{
			const FBox2D Box(Centre - Half, Centre + Half);
			for (const FBox2D& Zone : Zones)
			{
				if (Zone.Intersect(Box))
				{
					return false;
				}
			}
			return true;
		};

		if (IsFree(Ideal))
		{
			return FBox2D(Ideal - Half, Ideal + Half);
		}
		FVector2D Best = Ideal;
		double BestDistance = BIG_NUMBER;
		const float Step = 10.f * Scale;
		for (float Y = Low.Y; Y <= High.Y; Y += Step)
		{
			for (float X = Low.X; X <= High.X; X += Step)
			{
				const FVector2D Candidate(X, Y);
				const double Distance = FVector2D::DistSquared(Candidate, Ideal);
				if (Distance < BestDistance && IsFree(Candidate))
				{
					BestDistance = Distance;
					Best = Candidate;
				}
			}
		}
		return FBox2D(Best - Half, Best + Half);
	}

	/** The help text: up to two short lines. */
	struct FHintText
	{
		FString First;
		FString Second;
	};

	/**
	 * All the controls for the first minute of a round, then one short line for what the crab is doing, and what to do
	 * about a gull that is down and coming (bGullDown). None while the results panel is up. In one-stick mode
	 * (bOneStick) a click is a tap, so the lines speak of the stick instead.
	 */
	inline FHintText HintText(float RoundSeconds, bool bInBurrow, bool bMolting, bool bRoundOver = false, bool bGullDown = false, bool bOneStick = false)
	{
		if (bRoundOver)
		{
			return {FString(), FString()};
		}
		if (bOneStick)
		{
			if (RoundSeconds < FullHintSeconds)
			{
				return {TEXT("Triple-tap: menu or steer."), TEXT("A click counts as one tap.")};
			}
			if (bMolting)
			{
				return {TEXT("Molting. Steer to cancel."), FString()};
			}
			if (bInBurrow)
			{
				return {bGullDown ? TEXT("Gull outside. Stay in until it goes.") : TEXT("Steer to come out."), FString()};
			}
			if (bGullDown)
			{
				return {TEXT("Gull! Pick BURROW, or dance."), FString()};
			}
			return {TEXT("Triple-tap: menu or steer."), FString()};
		}
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
			return {bGullDown ? TEXT("Gull outside. Stay in until it goes.") : TEXT("Click elsewhere to come out."), FString()};
		}
		if (bGullDown)
		{
			return {TEXT("Gull! Press BURROW, or dance."), FString()};
		}
		return {TEXT("Right click: dash. Right drag: orbit."), FString()};
	}

	/**
	 * True if the message only says again what a button's reason line already shows: the same words, or
	 * the reason after a lead-in ("Cannot dig: too close to a burrow" against "Too close to a burrow").
	 */
	inline bool EchoesReason(const FString& Message, const FString& Reason)
	{
		return !Reason.IsEmpty() && Message.EndsWith(Reason, ESearchCase::IgnoreCase);
	}

	// --- One-stick mode -------------------------------------------------------------------------------

	/** The ONE STICK toggle, top right: on however the game is being played, so a stick-only player can turn it on with a click. */
	constexpr float OneStickButtonWidth = 240.f;
	constexpr float OneStickButtonHeight = 50.f;

	inline FBox2D OneStickButtonRect(float ViewWidth, float ViewHeight)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FVector2D Size(OneStickButtonWidth * Scale, OneStickButtonHeight * Scale);
		const float Right = ViewWidth - 24.f * Scale;
		const float Top = 20.f * Scale;
		return FBox2D(FVector2D(Right - Size.X, Top), FVector2D(Right, Top + Size.Y));
	}

	inline bool HitsOneStickButton(float ViewWidth, float ViewHeight, const FVector2D& Pixel)
	{
		const float Margin = DigButtonHitMargin * ScaleForHeight(ViewHeight);
		return OneStickButtonRect(ViewWidth, ViewHeight).ExpandBy(Margin).IsInside(Pixel);
	}

	/** The MENU column: a vertical stack of items, left of the middle, clear of the tide gauge, the hint panel and every other button. */
	constexpr float StickMenuLeft = 200.f;
	constexpr float StickMenuTop = 96.f;
	constexpr float StickMenuItemWidth = 260.f;
	constexpr float StickMenuItemHeight = 50.f;
	constexpr float StickMenuItemGap = 6.f;

	inline FBox2D StickMenuItemRect(float ViewWidth, float ViewHeight, int32 Index)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const float Top = (StickMenuTop + Index * (StickMenuItemHeight + StickMenuItemGap)) * Scale;
		const FVector2D Min(StickMenuLeft * Scale, Top);
		return FBox2D(Min, Min + FVector2D(StickMenuItemWidth * Scale, StickMenuItemHeight * Scale));
	}

	/** The bottom of a column of this many stacked menu items, for whatever sits just under it. */
	inline float StickMenuBottom(float ViewWidth, float ViewHeight, int32 ItemCount)
	{
		return StickMenuItemRect(ViewWidth, ViewHeight, FMath::Max(ItemCount - 1, 0)).Max.Y;
	}

	/** The mode banner ("MENU: ..." or "STEER: ..."), under the menu column so it never needs the column hidden to show. */
	constexpr float StickBannerHeight = 64.f;
	constexpr float StickBannerGap = 12.f;

	inline FBox2D StickModeBannerRect(float ViewWidth, float ViewHeight, int32 ItemCount)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const float Top = StickMenuBottom(ViewWidth, ViewHeight, ItemCount) + StickBannerGap * Scale;
		const FVector2D Min(StickMenuLeft * Scale, Top);
		return FBox2D(Min, Min + FVector2D(StickMenuItemWidth * Scale, StickBannerHeight * Scale));
	}

	/** One chain pip, under the mode banner: up to three, filling as the tap chain builds toward a triple tap. */
	constexpr float StickPipSize = 32.f;
	constexpr float StickPipGap = 10.f;
	constexpr float StickPipTopGap = 10.f;

	inline FBox2D StickChainPipRect(float ViewWidth, float ViewHeight, int32 ItemCount, int32 PipIndex)
	{
		const float Scale = ScaleForHeight(ViewHeight);
		const FBox2D Banner = StickModeBannerRect(ViewWidth, ViewHeight, ItemCount);
		const float Top = Banner.Max.Y + StickPipTopGap * Scale;
		const float Left = StickMenuLeft * Scale + PipIndex * (StickPipSize + StickPipGap) * Scale;
		return FBox2D(FVector2D(Left, Top), FVector2D(Left + StickPipSize * Scale, Top + StickPipSize * Scale));
	}
}
