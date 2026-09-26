// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CrabPlayerController.generated.h"

/**
 * Pointer-first control. Hold the left button to walk toward the cursor. A
 * quick click walks to that spot and stops there. Right click dashes. No
 * key, wheel or timing is required for anything.
 */
UCLASS()
class CRABSIM_API ACrabPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACrabPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** The cursor projected onto the flat ground at the crab's feet. False when there is no cursor or it points at the sky. */
	bool GetCursorGroundPoint(FVector& OutPoint) const;

	/** Farthest a click can send the crab from where it stands, uu. Keeps a click near the horizon sane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Input")
	float MaxClickDistance = 3000.f;
};
