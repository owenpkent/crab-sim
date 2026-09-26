// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CrabHUD.generated.h"

/**
 * On-screen readout: a tide gauge with a marker for how high the crab is
 * standing, the crab's grip, and a short message line. Drawn with the canvas,
 * so it needs no assets. Never asks for input.
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
	void DrawMessage(const ACrabPawn& Crab, float Scale);
	void DrawHints(float Scale);
};
