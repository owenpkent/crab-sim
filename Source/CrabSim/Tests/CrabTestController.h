// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

// UHT will not parse a UCLASS inside a preprocessor block, so this always compiles in.
// It has no behaviour of its own.

#include "GameFramework/Controller.h"
#include "CrabTestController.generated.h"

/**
 * A minimal concrete AController. AController is abstract and cannot be spawned,
 * and a character only runs its movement when it has a local controller. Possessing
 * the crab with this gives it one, without any local player or input machinery.
 */
UCLASS()
class ACrabTestController : public AController
{
	GENERATED_BODY()
};
