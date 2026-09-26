// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTerrainMath.h"

namespace UE::CrabSim::Tests::Terrain
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Average ground height along a line of constant X across the walkable width. */
	float AverageHeightAtX(float X)
	{
		float Sum = 0.f;
		int32 Count = 0;
		for (float Y = -1500.f; Y <= 1500.f; Y += 100.f)
		{
			Sum += CrabTerrain::Height(X, Y);
			++Count;
		}
		return Sum / static_cast<float>(Count);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainDeterministicTest, "CrabSim.Terrain.HeightIsDeterministicAndFinite", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainDeterministicTest::RunTest(const FString& Parameters)
{
	for (float X = CrabTerrain::MinX; X <= CrabTerrain::MaxX; X += 700.f)
	{
		for (float Y = -CrabTerrain::HalfWidthY; Y <= CrabTerrain::HalfWidthY; Y += 640.f)
		{
			const float A = CrabTerrain::Height(X, Y);
			const float B = CrabTerrain::Height(X, Y);
			TestTrue(*FString::Printf(TEXT("finite at %.0f,%.0f"), X, Y), FMath::IsFinite(A));
			TestEqual(*FString::Printf(TEXT("repeatable at %.0f,%.0f"), X, Y), A, B);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainSlopeTest, "CrabSim.Terrain.LandIsHigherThanSea", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainSlopeTest::RunTest(const FString& Parameters)
{
	using namespace UE::CrabSim::Tests::Terrain;
	const float Dunes = AverageHeightAtX(-3800.f);
	const float Flats = AverageHeightAtX(-500.f);
	const float Shallows = AverageHeightAtX(2500.f);
	const float Seabed = AverageHeightAtX(6000.f);

	TestTrue(TEXT("dunes are above the flats"), Dunes > Flats);
	TestTrue(TEXT("flats are above the shallows"), Flats > Shallows);
	TestTrue(TEXT("shallows are above the seabed"), Shallows > Seabed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainSidesTest, "CrabSim.Terrain.SidesRiseIntoBanks", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainSidesTest::RunTest(const FString& Parameters)
{
	for (float X = -1000.f; X <= 3000.f; X += 1000.f)
	{
		const float Middle = CrabTerrain::Height(X, 0.f);
		TestTrue(*FString::Printf(TEXT("left bank is high at x=%.0f"), X), CrabTerrain::Height(X, -CrabTerrain::HalfWidthY) - Middle > 700.f);
		TestTrue(*FString::Printf(TEXT("right bank is high at x=%.0f"), X), CrabTerrain::Height(X, CrabTerrain::HalfWidthY) - Middle > 700.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainCreekTest, "CrabSim.Terrain.CreekIsADepression", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainCreekTest::RunTest(const FString& Parameters)
{
	// The creek follows Y = 420 sin(X / 1100 + 0.7). Compare its bed with the ground either side.
	for (float X = -800.f; X <= 3000.f; X += 700.f)
	{
		const float CentreY = 420.f * FMath::Sin(X / 1100.f + 0.7f);
		const float Bed = CrabTerrain::Height(X, CentreY);
		const float Sides = 0.25f * (CrabTerrain::Height(X, CentreY + 500.f) + CrabTerrain::Height(X, CentreY + 700.f)
			+ CrabTerrain::Height(X, CentreY - 500.f) + CrabTerrain::Height(X, CentreY - 700.f));
		TestTrue(*FString::Printf(TEXT("creek bed is below its banks at x=%.0f (bed %.1f, sides %.1f)"), X, Bed, Sides), Bed < Sides - 25.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainNormalTest, "CrabSim.Terrain.NormalsAreUnitAndUpward", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainNormalTest::RunTest(const FString& Parameters)
{
	for (float X = CrabTerrain::MinX; X <= CrabTerrain::MaxX; X += 900.f)
	{
		for (float Y = -2400.f; Y <= 2400.f; Y += 600.f)
		{
			const FVector N = CrabTerrain::Normal(X, Y);
			TestNearlyEqual(*FString::Printf(TEXT("unit length at %.0f,%.0f"), X, Y), static_cast<float>(N.Size()), 1.f, 1e-3f);
			TestTrue(*FString::Printf(TEXT("upward at %.0f,%.0f"), X, Y), N.Z > 0.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainWalkableTest, "CrabSim.Terrain.BeachIsWalkableBanksAreNot", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainWalkableTest::RunTest(const FString& Parameters)
{
	// Unreal's default walkable floor angle is 44.765 degrees, a floor normal Z of about 0.71.
	constexpr float WalkableNormalZ = 0.71f;

	for (float X = -1500.f; X <= 3500.f; X += 500.f)
	{
		for (float Y = -1800.f; Y <= 1800.f; Y += 450.f)
		{
			TestTrue(*FString::Printf(TEXT("beach is walkable at %.0f,%.0f"), X, Y), CrabTerrain::Normal(X, Y).Z > WalkableNormalZ);
		}
	}
	// On the steepest part of the bank the crab cannot climb out.
	for (float X = -500.f; X <= 3000.f; X += 500.f)
	{
		TestTrue(*FString::Printf(TEXT("bank is too steep to climb at x=%.0f"), X), CrabTerrain::Normal(X, 2850.f).Z < WalkableNormalZ);
		TestTrue(*FString::Printf(TEXT("far bank is too steep to climb at x=%.0f"), X), CrabTerrain::Normal(X, -2850.f).Z < WalkableNormalZ);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabTerrainTideReachTest, "CrabSim.Terrain.TideShorelineMovesAcrossTheFlats", UE::CrabSim::Tests::Terrain::TestFlags)
bool FCrabTerrainTideReachTest::RunTest(const FString& Parameters)
{
	// The crab starts at the origin. At the default low tide the water must be well
	// away from it, and at the default high tide it must have reached and passed it.
	const float Ground = CrabTerrain::Height(0.f, 0.f);
	TestTrue(TEXT("origin is dry at low tide"), -190.f < Ground - 40.f);
	TestTrue(TEXT("origin is flooded at high tide"), 90.f > Ground + 60.f);
	return true;
}
