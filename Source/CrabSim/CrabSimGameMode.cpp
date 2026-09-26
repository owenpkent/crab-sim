// SPDX-License-Identifier: Apache-2.0
#include "CrabSimGameMode.h"
#include "CrabBeach.h"
#include "CrabPawn.h"
#include "CrabPlayerController.h"

#include "EngineUtils.h"
#include "Engine/World.h"

ACrabSimGameMode::ACrabSimGameMode()
{
	DefaultPawnClass = ACrabPawn::StaticClass();
	PlayerControllerClass = ACrabPlayerController::StaticClass();
}

void ACrabSimGameMode::BeginPlay()
{
	Super::BeginPlay();

	// A real level with its own beach is left exactly as designed.
	UWorld* World = GetWorld();
	if (World && !TActorIterator<ACrabBeach>(World))
	{
		World->SpawnActor<ACrabBeach>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
}
