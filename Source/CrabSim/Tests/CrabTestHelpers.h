// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CrabBeach.h"
#include "CrabPawn.h"
#include "CrabTerrainMath.h"
#include "TestWorld.h"

namespace UE::CrabSim::Tests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Tide clock values, seconds, for the default 180 s tide. */
	constexpr float LowTide = 0.f;
	constexpr float HighTide = 90.f;

	/** Rising and falling, both about a metre deep at the crab's start. */
	constexpr float RisingSurge = 70.f;
	constexpr float FallingSurge = 110.f;

	/** Index of the burrows the beach builds: highest first, lowest last. */
	constexpr int32 HighBurrow = 0;
	constexpr int32 LowBurrow = 3;

	inline ACrabBeach* SpawnBeach(FCrabTestWorld& World)
	{
		return World.SpawnActor<ACrabBeach>();
	}

	/** A possessed crab standing on the ground at XY. */
	inline ACrabPawn* SpawnCrab(FCrabTestWorld& World, const ACrabBeach& Beach, float X = 0.f, float Y = 0.f)
	{
		const float Z = Beach.GetGroundHeight(X, Y) + 60.f;
		return World.SpawnPossessedPawn<ACrabPawn>(FVector(X, Y, Z));
	}

	/** Lets a freshly spawned crab drop onto the ground and settle. */
	inline void Settle(FCrabTestWorld& World)
	{
		World.TickSeconds(0.6f);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
