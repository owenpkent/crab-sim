// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CrabHUD.generated.h"

/**
 * On-screen readout: a tide gauge with a marker for how high the crab is
 * standing, the crab's grip and food, its molts, a short message line, the FOOD,
 * BURROW, dig and molt buttons, a help panel, a "Gull!" banner with an arrow to the
 * gull while one is coming, the results panel once the round is over (won or eaten), and, with
 * CrabSim.OneStick 1, a MENU column, a mode banner and tap-chain pips for one-stick mode. Drawn with the
 * canvas, so it needs no assets. The buttons are only drawn here: the controller reads the click, using
 * the same layout (CrabHudMath.h).
 */
UCLASS()
class CRABSIM_API ACrabHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawTideGauge(const class ACrabPawn& Crab, const class ACrabBeach& Beach, float Scale);
	void DrawGripBar(const ACrabPawn& Crab, float Scale);
	void DrawFoodBar(const ACrabPawn& Crab, float Scale);
	/** The buttons return the reason line they drew (empty when they are ready), so the message line can leave out a repeat. */
	FString DrawDigButton(const ACrabPawn& Crab, float Scale);
	FString DrawMoltButton(const ACrabPawn& Crab, float Scale);
	FString DrawFoodButton(const ACrabPawn& Crab, float Scale);
	FString DrawBurrowButton(const ACrabPawn& Crab, float Scale);
	void DrawGotoButton(const FBox2D& Rect, const TCHAR* Label, const TCHAR* Hint, const FLinearColor& ReadyColour, const FString& Reason, float Scale);
	void DrawReasonLine(const FBox2D& Rect, const FString& Reason, float Scale);
	void DrawMoltPips(const ACrabPawn& Crab, float Scale);
	void DrawResults(const ACrabPawn& Crab, float Scale);
	/** The banner and the arrow at the view's edge, while a gull is circling or down. Decorative: neither takes a click. */
	void DrawGullWarning(const ACrabPawn& Crab, const class ACrabGull& Gull, float Scale);
	void DrawMessage(const ACrabPawn& Crab, float Scale, const TArray<FString>& ShownReasons);
	void DrawHints(const ACrabPawn& Crab, bool bGullDown, float Scale);

	/** The ONE STICK toggle, drawn whatever the CVar says: it is how a stick-only player turns this on. */
	void DrawOneStickToggle(float Scale);
	/** The MENU column (only while in MENU), the mode banner and the tap chain's pips. Drawn only while the CVar is on. */
	void DrawOneStickPanel(const class ACrabPlayerController& PC, float Scale, bool bRoundOver);
};
