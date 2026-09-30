// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CrabCarryComponent.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

/** What a crab is carrying in front of its claws. */
UENUM()
enum class ECarry : uint8
{
	None,
	Pellet,
	Food,
};

/**
 * A held thing shown in front of a crab's claws: real fiddler crabs roll the sand they dig into little balls and
 * carry them out of the hole, and carry a scrap of sifted food back the same way (GAME.md, "Colony (building)").
 * Nothing but the look: attach one where a crab's claws meet in front of it and call SetCarrying. Sized for a crab
 * at scale 1, so a parent's own uniform scale (a colony NPC's SetSizeScale, or the player crab's growth) carries
 * it along for free; whoever attaches this under the player crab's mesh is not this component's job.
 */
UCLASS(ClassGroup = (Crab), meta = (BlueprintSpawnableComponent))
class CRABSIM_API UCrabCarryComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCrabCarryComponent();

	virtual void BeginPlay() override;

	/** Shows a pellet or a food wad, or hides both (None). */
	void SetCarrying(ECarry NewCarry);
	ECarry GetCarrying() const { return Carrying; }

private:
	void BuildVisual();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PelletMesh;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> FoodMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	ECarry Carrying = ECarry::None;
};
