// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CrabHUD.generated.h"

/**
 * On-screen readout: a tide gauge with a marker for how high the crab is
 * standing, the crab's grip and food, its molts, a short message line, the dig
 * and molt buttons, and the results panel once the round is won.
 * Drawn with the canvas, so it needs no assets. The buttons are only drawn here:
 * the controller reads the click, using the same layout (CrabHudMath.h).
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
	void DrawDigButton(const ACrabPawn& Crab, float Scale);
	void DrawMoltButton(const ACrabPawn& Crab, float Scale);
	void DrawMoltPips(const ACrabPawn& Crab, float Scale);
	void DrawResults(const ACrabPawn& Crab, float Scale);
	void DrawMessage(const ACrabPawn& Crab, float Scale);
	void DrawHints(float Scale);
};
