// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CrabSimGameMode.generated.h"

class ACrabBeach;
class ACrabColony;
class ACrabGull;

/**
 * Sets the crab, its pointer controller and its HUD as project defaults, and
 * builds the beach when the level does not already have one. Puts the gull in the world.
 */
UCLASS()
class CRABSIM_API ACrabSimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrabSimGameMode();

	virtual void BeginPlay() override;

	ACrabBeach* GetBeach() const { return Beach; }
	ACrabGull* GetGull() const { return Gull; }
	ACrabColony* GetColony() const { return Colony; }

	/** Where the crab starts, world XY. The beach keeps rocks well clear of it. */
	UPROPERTY(EditAnywhere, Category = "Crab")
	FVector2D StartXY = FVector2D::ZeroVector;

private:
	/** The engine's template map has a floor at Z = 0 that would poke through the sea. Removes it. */
	void RemoveTemplateFloor();
	void PlaceCrabsOnTheGround();

	UPROPERTY()
	TObjectPtr<ACrabBeach> Beach;

	UPROPERTY()
	TObjectPtr<ACrabGull> Gull;

	UPROPERTY()
	TObjectPtr<ACrabColony> Colony;
};
