// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * One-stick mode: a MENU of the crab's actions and a STEER mode that drives it directly, both worked
 * from one analog stick (or a lone left click, standing in for a tap when there is no stick) and nothing
 * else. Built for a player with no other input, who may have tremor or limited stick travel, so every
 * threshold is a plain field the caller can set from a CVar. Pure rules, no engine singletons: time and
 * input are passed in every step, so a headless test drives it exactly as the controller does. See
 * GAME.md, "One-stick mode".
 */
namespace CrabStick
{
	/** Every threshold here is also a CVar (CrabSim.StickTap*), so the controller copies them in each tick. */
	struct FTuning
	{
		/** Stick magnitude that arms a reach: cross this and it is on its way to a tap. */
		float Outer = 0.5f;
		/** Stick magnitude a reach must fall back under to complete as a tap. Below Outer, with a gap
		 * between the two, so a tremor wobbling round one threshold never fires a tap or an early one. */
		float Inner = 0.2f;
		/** Longest a reach may take, Outer to under Inner, and still be a tap, seconds. Held longer, it is
		 * not a tap: in STEER it is only steering, in MENU it does nothing (no auto-repeat). */
		float MaxTapSeconds = 0.4f;
		/** Taps this close together, end to start, chain together. The chain closes when this passes with
		 * no new tap. */
		float GapSeconds = 0.6f;
		/** The walk speed multiplier STEER falls to at Inner deflection, before it ramps up to 1 at full
		 * deflection (SteerSpeedMultiplier below). CrabSim.StickMinSpeed. */
		float MinSpeed = 0.35f;
	};

	/**
	 * The MENU column, in cursor order, top to bottom, while a round is on. NEW_ROUND is not one of these
	 * seven: it is the only item once the round is over (ActiveItemCount, ActiveItemAt), standing in for
	 * the results panel's own button so a stick-only player can start the next round.
	 */
	enum class EMenuItem : uint8
	{
		Move,
		Food,
		Burrow,
		Dig,
		Molt,
		Dance,
		Dash,
		NewRound,
		Num,
	};

	/** How many items are showing on the menu right now: the seven action items while a round is on, or just NEW_ROUND once it is over. */
	inline int32 ActiveItemCount(bool bRoundOver)
	{
		return bRoundOver ? 1 : static_cast<int32>(EMenuItem::NewRound);
	}

	/** The item at this position (0-based, < ActiveItemCount) in the menu that is showing right now. */
	inline EMenuItem ActiveItemAt(bool bRoundOver, int32 Index)
	{
		return bRoundOver ? EMenuItem::NewRound : static_cast<EMenuItem>(Index);
	}

	/** Where the cursor goes when the active list changes (a round ending or a new one starting): the first item on it. */
	inline EMenuItem FirstItem(bool bRoundOver)
	{
		return bRoundOver ? EMenuItem::NewRound : EMenuItem::Move;
	}

	/** Up and down wrap within whichever list (ActiveItemCount) is showing right now. NEW_ROUND, being the
	 * only item once the round is over, always wraps to itself. */
	inline EMenuItem NextItem(EMenuItem Item, bool bRoundOver)
	{
		if (bRoundOver)
		{
			return EMenuItem::NewRound;
		}
		const uint8 Count = static_cast<uint8>(EMenuItem::NewRound);
		return static_cast<EMenuItem>((static_cast<uint8>(Item) + 1) % Count);
	}

	inline EMenuItem PrevItem(EMenuItem Item, bool bRoundOver)
	{
		if (bRoundOver)
		{
			return EMenuItem::NewRound;
		}
		const uint8 Count = static_cast<uint8>(EMenuItem::NewRound);
		return static_cast<EMenuItem>((static_cast<uint8>(Item) + Count - 1) % Count);
	}

	inline const TCHAR* ItemLabel(EMenuItem Item)
	{
		switch (Item)
		{
		case EMenuItem::Move: return TEXT("MOVE");
		case EMenuItem::Food: return TEXT("FOOD");
		case EMenuItem::Burrow: return TEXT("BURROW");
		case EMenuItem::Dig: return TEXT("DIG");
		case EMenuItem::Molt: return TEXT("MOLT");
		case EMenuItem::Dance: return TEXT("DANCE");
		case EMenuItem::Dash: return TEXT("DASH");
		case EMenuItem::NewRound: return TEXT("NEW_ROUND");
		default: return TEXT("");
		}
	}

	/**
	 * How far the stick is pushed (its magnitude) maps to a walk speed multiplier: MinSpeed at Inner
	 * deflection, ramping linearly to 1 at full deflection (magnitude 1). Monotonic and clamped at both
	 * ends, so a magnitude at or below Inner is MinSpeed (moot: STEER does not move the crab there at all)
	 * and one at or above 1 is 1.
	 */
	inline float SteerSpeedMultiplier(float Magnitude, const FTuning& Tuning)
	{
		const float Span = FMath::Max(1.f - Tuning.Inner, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp((Magnitude - Tuning.Inner) / Span, 0.f, 1.f);
		return FMath::Lerp(FMath::Clamp(Tuning.MinSpeed, 0.f, 1.f), 1.f, Alpha);
	}

	/** Which way a stick tap pointed, by its dominant axis at the peak. A click tap has no direction. */
	enum class ETapDir : uint8
	{
		None,
		Up,
		Down,
		Left,
		Right,
	};

	inline const TCHAR* TapDirText(ETapDir Direction)
	{
		switch (Direction)
		{
		case ETapDir::Up: return TEXT("up");
		case ETapDir::Down: return TEXT("down");
		case ETapDir::Left: return TEXT("left");
		case ETapDir::Right: return TEXT("right");
		default: return TEXT("click");
		}
	}

	/** One completed tap, stick or click, as the detectors hand it to the menu state machine. */
	struct FTap
	{
		/** None for a click tap: directionless by design. */
		ETapDir Direction = ETapDir::None;
		bool bFromClick = false;
	};

	inline const TCHAR* TapDirText(const FTap& Tap)
	{
		return Tap.bFromClick ? TEXT("click") : TapDirText(Tap.Direction);
	}

	/** The dominant axis of a stick vector: whichever of up/down/left/right its bigger component points along. */
	inline ETapDir DominantDirection(const FVector2D& Stick)
	{
		if (FMath::Abs(Stick.Y) >= FMath::Abs(Stick.X))
		{
			return Stick.Y >= 0.f ? ETapDir::Up : ETapDir::Down;
		}
		return Stick.X >= 0.f ? ETapDir::Right : ETapDir::Left;
	}

	/**
	 * Detects taps on the stick's magnitude: armed once it crosses Outer, resolved once it falls back under
	 * Inner. A reach that takes longer than MaxTapSeconds to resolve completes with no tap (it only ever
	 * disarms), so a sustained hold used for steering never fires one by accident when it is finally let go.
	 */
	struct FStickTapDetector
	{
		FTuning Tuning;

		/** Feed the stick's raw XY this frame (UE convention: X right, Y up). Appends a tap, if one completed. */
		void Update(float DeltaSeconds, const FVector2D& Stick, TArray<FTap>& OutTaps)
		{
			const float Magnitude = Stick.Size();
			if (!bArmed)
			{
				if (Magnitude >= Tuning.Outer)
				{
					bArmed = true;
					bTooLong = false;
					Elapsed = 0.f;
					PeakMagnitude = Magnitude;
					PeakDirection = DominantDirection(Stick);
				}
				return;
			}

			Elapsed += DeltaSeconds;
			if (Magnitude > PeakMagnitude)
			{
				PeakMagnitude = Magnitude;
				PeakDirection = DominantDirection(Stick);
			}
			if (Elapsed > Tuning.MaxTapSeconds)
			{
				bTooLong = true;
			}
			if (Magnitude < Tuning.Inner)
			{
				if (!bTooLong)
				{
					OutTaps.Add(FTap{PeakDirection, false});
				}
				bArmed = false;
			}
		}

	private:
		bool bArmed = false;
		bool bTooLong = false;
		float Elapsed = 0.f;
		float PeakMagnitude = 0.f;
		ETapDir PeakDirection = ETapDir::None;
	};

	/** Detects taps on the left mouse button, the same way, on down and up instead of a magnitude. Directionless. */
	struct FClickTapDetector
	{
		FTuning Tuning;

		/** Feed whether the button is down this frame. Appends a tap, if one completed. */
		void Update(float DeltaSeconds, bool bDown, TArray<FTap>& OutTaps)
		{
			if (!bArmed)
			{
				if (bDown)
				{
					bArmed = true;
					bTooLong = false;
					Elapsed = 0.f;
				}
				return;
			}

			Elapsed += DeltaSeconds;
			if (Elapsed > Tuning.MaxTapSeconds)
			{
				bTooLong = true;
			}
			if (!bDown)
			{
				if (!bTooLong)
				{
					OutTaps.Add(FTap{ETapDir::None, true});
				}
				bArmed = false;
			}
		}

	private:
		bool bArmed = false;
		bool bTooLong = false;
		float Elapsed = 0.f;
	};

	enum class EMode : uint8
	{
		Menu,
		Steer,
	};

	inline const TCHAR* ModeLabel(EMode Mode)
	{
		return Mode == EMode::Menu ? TEXT("menu") : TEXT("steer");
	}

	/** What one step of the menu state machine did, for the controller to act on and the HUD and log to show. */
	struct FStepResult
	{
		bool bCursorMoved = false;
		/** An item was committed: the chain that held it closed short of three taps. */
		bool bSelected = false;
		EMenuItem SelectedItem = EMenuItem::Move;
		/** The chain reached its third tap: the mode flipped, and if it left MENU the cursor was undone. */
		bool bTripleTapped = false;
		/** The mode changed this step, by a triple tap or by committing a MOVE select. */
		bool bModeChanged = false;
	};

	/**
	 * MENU and STEER, and the chain of taps that moves between them. Up and down taps move the cursor at
	 * once; a left or right tap, or a click, selects the highlighted item, but the select only takes effect
	 * (SELECT is irreversible) once the chain closes with fewer than three taps in it. The third tap of any
	 * chain always fires a toggle instead and resets the chain: from MENU it undoes every cursor move the
	 * chain made (back to where the cursor stood before the chain's first tap) and drops any pending select,
	 * then enters STEER; from STEER it returns to MENU. A tap taken in STEER only ever counts toward that
	 * third tap: there is no cursor or pending select to undo there.
	 *
	 * The round starting or ending (bRoundOver, passed in every step) switches the active item list
	 * (ActiveItemCount/ActiveItemAt): the cursor is put on the first item of whichever list is now showing,
	 * and any open chain is dropped, so the player is never left pointing at an item that just vanished.
	 */
	struct FMenuState
	{
		FTuning Tuning;

		/** Feed this frame's taps (usually none, sometimes one) in the order they happened, and whether the round is over right now. */
		FStepResult Update(float DeltaSeconds, TArrayView<const FTap> Taps, bool bRoundOverNow)
		{
			FStepResult Result;
			if (bRoundOverNow != bRoundOver)
			{
				bRoundOver = bRoundOverNow;
				Mode = EMode::Menu;
				Cursor = FirstItem(bRoundOver);
				bHasPendingSelect = false;
				bChainOpen = false;
				ChainCount = 0;
				Result.bCursorMoved = true;
				Result.bModeChanged = true;
			}
			if (bChainOpen)
			{
				TimeSinceLastTapEnd += DeltaSeconds;
				if (TimeSinceLastTapEnd > Tuning.GapSeconds)
				{
					CloseChain(Result);
				}
			}
			for (const FTap& Tap : Taps)
			{
				ProcessTap(Tap, Result);
			}
			return Result;
		}

		EMode GetMode() const { return Mode; }
		EMenuItem GetCursor() const { return Cursor; }
		/** Taps in the chain still open, 0 to 2 (a third always fires and resets it). For the HUD's pips. */
		int32 GetChainCount() const { return bChainOpen ? ChainCount : 0; }
		bool HasPendingSelect() const { return bHasPendingSelect; }

	private:
		void ProcessTap(const FTap& Tap, FStepResult& Result)
		{
			if (!bChainOpen)
			{
				bChainOpen = true;
				ChainCount = 0;
				CursorBeforeChain = Cursor;
			}
			TimeSinceLastTapEnd = 0.f;
			++ChainCount;

			if (Mode == EMode::Menu)
			{
				if (Tap.Direction == ETapDir::Up)
				{
					Cursor = PrevItem(Cursor, bRoundOver);
					Result.bCursorMoved = true;
				}
				else if (Tap.Direction == ETapDir::Down)
				{
					Cursor = NextItem(Cursor, bRoundOver);
					Result.bCursorMoved = true;
				}
				else
				{
					// Left, right, or a click: select the highlighted item. Held pending, not committed yet.
					PendingSelect = Cursor;
					bHasPendingSelect = true;
				}
			}
			// STEER: a tap here only ever counts toward the chain below; nothing to move or undo.

			if (ChainCount >= 3)
			{
				Result.bTripleTapped = true;
				Result.bModeChanged = true;
				if (Mode == EMode::Menu)
				{
					Cursor = CursorBeforeChain;
					bHasPendingSelect = false;
					Mode = EMode::Steer;
				}
				else
				{
					Mode = EMode::Menu;
				}
				bChainOpen = false;
				ChainCount = 0;
			}
		}

		void CloseChain(FStepResult& Result)
		{
			if (bHasPendingSelect)
			{
				Result.bSelected = true;
				Result.SelectedItem = PendingSelect;
				bHasPendingSelect = false;
				if (PendingSelect == EMenuItem::Move)
				{
					Mode = EMode::Steer;
					Result.bModeChanged = true;
				}
			}
			bChainOpen = false;
			ChainCount = 0;
		}

		EMode Mode = EMode::Menu;
		EMenuItem Cursor = EMenuItem::Move;
		EMenuItem CursorBeforeChain = EMenuItem::Move;
		EMenuItem PendingSelect = EMenuItem::Move;
		bool bHasPendingSelect = false;
		bool bChainOpen = false;
		bool bRoundOver = false;
		int32 ChainCount = 0;
		float TimeSinceLastTapEnd = 0.f;
	};
}
