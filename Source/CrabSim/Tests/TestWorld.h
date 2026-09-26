// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CrabTestController.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Tests/AutomationCommon.h"

namespace UE::CrabSim::Tests
{
	/**
	 * A transient game world for gameplay automation tests, built on the engine's
	 * FTestWorldWrapper. Play has begun, so BeginPlay runs on everything spawned. The
	 * world never ticks by itself: TickN is the only thing that advances it, at a
	 * fixed step. The destructor tears the world down and forces a GC pass, so a leak
	 * fails in the test that made it.
	 *
	 * One instance per test. Check IsReady(), spawn, tick, assert.
	 */
	class FCrabTestWorld : public FTestWorldWrapper
	{
	public:
		FCrabTestWorld()
		{
			bReady = CreateTestWorld(EWorldType::Game);
			if (bReady)
			{
				// The project default game mode builds a beach and moves crabs about. Tests want an
				// empty world and opt in to what they need, so pin the bare base class first.
				if (UWorld* World = GetTestWorld())
				{
					if (AWorldSettings* Settings = World->GetWorldSettings())
					{
						Settings->DefaultGameMode = AGameModeBase::StaticClass();
					}
				}
				bReady = BeginPlayInTestWorld();
			}
		}

		~FCrabTestWorld()
		{
			DestroyTestWorld(/*bForceGarbageCollect=*/true);
		}

		FCrabTestWorld(const FCrabTestWorld&) = delete;
		FCrabTestWorld& operator=(const FCrabTestWorld&) = delete;

		bool IsReady() const { return bReady; }

		/** Ticks the world Count times at DeltaSeconds, the same as that many fixed-step frames. */
		void TickN(int32 Count, float DeltaSeconds)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				TickTestWorld(DeltaSeconds);
			}
		}

		/** Ticks for about this many seconds at 60 frames a second. */
		void TickSeconds(float Seconds)
		{
			TickN(FMath::CeilToInt(Seconds * 60.f), 1.f / 60.f);
		}

		template <typename TActorClass>
		TActorClass* SpawnActor(const FVector& Location = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			UWorld* World = GetTestWorld();
			if (!World)
			{
				return nullptr;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<TActorClass>(TActorClass::StaticClass(), Location, Rotation, Params);
		}

		/** Spawns a pawn and a plain controller possessing it, so it has a local controller and can move. */
		template <typename TPawnClass>
		TPawnClass* SpawnPossessedPawn(const FVector& Location = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			TPawnClass* Pawn = SpawnActor<TPawnClass>(Location, Rotation);
			if (!Pawn)
			{
				return nullptr;
			}
			if (ACrabTestController* Controller = SpawnActor<ACrabTestController>())
			{
				Controller->Possess(Pawn);
			}
			return Pawn;
		}

	private:
		bool bReady = false;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
