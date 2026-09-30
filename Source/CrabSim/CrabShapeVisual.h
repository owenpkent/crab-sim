// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A small shared toolkit for drawing engine-shape geometry at runtime: the pattern ACrabBeach::LoadEngineShape and
 * AddShape set, factored out so the pieces that draw themselves the same way do not each repeat it. Used by the
 * colony's cutaway, its NPC bodies, and what they carry.
 */
namespace CrabShapeVisual
{
	/** An engine basic shape by name ("Cube", "Sphere", "Cylinder", "Cone", "Plane", ...). Null if it is not found. */
	UStaticMesh* LoadEngineShape(const TCHAR* Name);

	/** The plain material basic shapes use everywhere in this game, tinted per instance with a "Color" vector parameter. */
	UMaterialInterface* LoadBasicMaterial();

	/**
	 * A tinted, collision-free, shadowless static mesh component, parented to Parent and registered ready to use.
	 * Outer should be the actor (or a component of it) that owns the whole visual: components made this way are
	 * plain runtime UObjects, not compile-time subobjects, so whatever calls this can shape them from data.
	 */
	UStaticMeshComponent* AddTintedShape(UObject* Outer, USceneComponent* Parent, UStaticMesh* Mesh,
		const FVector& RelativeLocation, const FRotator& RelativeRotation, const FVector& RelativeScale,
		UMaterialInterface* BasicMaterial, const FLinearColor& Color);
}
