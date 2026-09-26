// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrabBeach.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

/**
 * A blockout beach built from engine basic shapes: a sand slab, dune walls on
 * the edge, scattered rocks and a few burrow holes. Deterministic for a given
 * Seed. Built at BeginPlay so the project needs no level asset yet.
 */
UCLASS()
class CRABSIM_API ACrabBeach : public AActor
{
	GENERATED_BODY()

public:
	ACrabBeach();

	virtual void BeginPlay() override;

	/** Where the burrows are, in world space. Valid after BeginPlay. */
	const TArray<FVector>& GetBurrowLocations() const { return BurrowLocations; }

	/** Half the sand slab's side, uu. */
	UPROPERTY(EditAnywhere, Category = "Beach")
	float HalfExtent = 4000.f;

	UPROPERTY(EditAnywhere, Category = "Beach")
	int32 RockCount = 12;

	UPROPERTY(EditAnywhere, Category = "Beach")
	int32 BurrowCount = 3;

	UPROPERTY(EditAnywhere, Category = "Beach")
	int32 Seed = 7;

	/** Z of the sand's top surface. Sits a hair above the template floor so the two never fight. */
	static constexpr float SandTopZ = 2.f;

private:
	UStaticMeshComponent* AddShape(const TCHAR* MeshPath, const FVector& Location, const FVector& Scale, const FLinearColor& Color, bool bBlocks);
	void BuildRocks();

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Shapes;

	TArray<FVector> BurrowLocations;
};
