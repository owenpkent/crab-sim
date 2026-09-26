// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CrabSimGameMode.generated.h"

/**
 * Sets the crab and its pointer controller as project defaults, and builds the
 * beach when the level does not already have one.
 */
UCLASS()
class CRABSIM_API ACrabSimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrabSimGameMode();

	virtual void BeginPlay() override;
};
