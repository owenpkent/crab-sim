// SPDX-License-Identifier: Apache-2.0
#include "CrabShapeVisual.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UStaticMesh* CrabShapeVisual::LoadEngineShape(const TCHAR* Name)
{
	return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
}

UMaterialInterface* CrabShapeVisual::LoadBasicMaterial()
{
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UStaticMeshComponent* CrabShapeVisual::AddTintedShape(UObject* Outer, USceneComponent* Parent, UStaticMesh* Mesh,
	const FVector& RelativeLocation, const FRotator& RelativeRotation, const FVector& RelativeScale,
	UMaterialInterface* BasicMaterial, const FLinearColor& Color)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(Outer);
	Shape->SetStaticMesh(Mesh);
	Shape->SetupAttachment(Parent);
	Shape->SetRelativeLocationAndRotation(RelativeLocation, RelativeRotation);
	Shape->SetRelativeScale3D(RelativeScale);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCastShadow(false);

	if (BasicMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BasicMaterial, Outer);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Shape->SetMaterial(0, Material);
	}

	Shape->RegisterComponent();
	return Shape;
}
