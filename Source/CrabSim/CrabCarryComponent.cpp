// SPDX-License-Identifier: Apache-2.0
#include "CrabCarryComponent.h"
#include "CrabShapeVisual.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	// A touch lighter than the beach's own sand (ACrabBeach's SandColor, 0.55/0.42/0.2): a freshly rolled pellet
	// is damp sand from under the surface, paler than what has dried on top.
	const FLinearColor PelletColor = FLinearColor(0.68f, 0.55f, 0.34f);
	// A scrap of sifted algae, greener and darker than a pellet.
	const FLinearColor FoodColor = FLinearColor(0.32f, 0.35f, 0.1f);
	// A rolled ball of dug sand, uu across: big enough to read against the crab, small enough to fit its claws.
	constexpr float PelletDiameter = 45.f;
}

UCrabCarryComponent::UCrabCarryComponent()
{
}

void UCrabCarryComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildVisual();
	SetCarrying(Carrying);
}

void UCrabCarryComponent::BuildVisual()
{
	BasicMaterial = CrabShapeVisual::LoadBasicMaterial();
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));

	// Held right at this component's own origin: whatever attaches it (a fixed offset off the claws, or later a
	// socket) is what puts it in front of them.
	PelletMesh = CrabShapeVisual::AddTintedShape(this, this, Sphere, FVector::ZeroVector, FRotator::ZeroRotator,
		FVector(PelletDiameter / 100.f), BasicMaterial, PelletColor);
	// A mouthful of sifted food, flatter than a rolled pellet: a crab sifts a wad of mud, not a marble.
	FoodMesh = CrabShapeVisual::AddTintedShape(this, this, Sphere, FVector::ZeroVector, FRotator::ZeroRotator,
		FVector(0.5f, 0.5f, 0.26f), BasicMaterial, FoodColor);
}

void UCrabCarryComponent::SetCarrying(ECarry NewCarry)
{
	Carrying = NewCarry;
	if (PelletMesh)
	{
		PelletMesh->SetVisibility(NewCarry == ECarry::Pellet);
	}
	if (FoodMesh)
	{
		FoodMesh->SetVisibility(NewCarry == ECarry::Food);
	}
}
