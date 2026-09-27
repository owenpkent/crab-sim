// SPDX-License-Identifier: Apache-2.0
#include "CrabSimGameMode.h"
#include "CrabBeach.h"
#include "CrabGull.h"
#include "CrabHUD.h"
#include "CrabPawn.h"
#include "CrabPlayerController.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ACrabSimGameMode::ACrabSimGameMode()
{
	DefaultPawnClass = ACrabPawn::StaticClass();
	PlayerControllerClass = ACrabPlayerController::StaticClass();
	HUDClass = ACrabHUD::StaticClass();
}

void ACrabSimGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A real level with its own beach is left exactly as designed.
	TActorIterator<ACrabBeach> Existing(World);
	if (Existing)
	{
		Beach = *Existing;
	}
	if (!Beach)
	{
		RemoveTemplateFloor();
		Beach = World->SpawnActor<ACrabBeach>(FVector::ZeroVector, FRotator::ZeroRotator);
	}

	// One gull to a world. It stays out of sight until the rules send it.
	TActorIterator<ACrabGull> ExistingGull(World);
	Gull = ExistingGull ? *ExistingGull : World->SpawnActor<ACrabGull>(FVector::ZeroVector, FRotator::ZeroRotator);

	PlaceCrabsOnTheGround();
}

void ACrabSimGameMode::RemoveTemplateFloor()
{
	TArray<AStaticMeshActor*> Floors;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* Component = It->GetStaticMeshComponent();
		if (Component && Component->GetStaticMesh() && Component->GetStaticMesh()->GetName().Contains(TEXT("Floor")))
		{
			Floors.Add(*It);
		}
	}
	for (AStaticMeshActor* Floor : Floors)
	{
		Floor->Destroy();
	}
}

void ACrabSimGameMode::PlaceCrabsOnTheGround()
{
	if (!Beach)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACrabPawn* Crab = Cast<ACrabPawn>(It->Get()->GetPawn()))
		{
			const float Half = Crab->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Ground = Beach->GetGroundHeight(StartXY.X, StartXY.Y);
			Crab->SetActorLocation(FVector(StartXY.X, StartXY.Y, Ground + Half + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
			Crab->SetRoundStart(StartXY);
		}
	}
}
