// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"

#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

using namespace UE::CrabSim::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachBuildsTest, "CrabSim.Beach.BuildsGroundAndSea", TestFlags)
bool FCrabBeachBuildsTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach spawned"), Beach))
	{
		return false;
	}

	UProceduralMeshComponent* Terrain = Beach->GetTerrainMesh();
	UProceduralMeshComponent* Water = Beach->GetWaterMesh();
	TestNotNull(TEXT("terrain mesh"), Terrain);
	TestNotNull(TEXT("water mesh"), Water);
	if (Terrain && Water)
	{
		TestEqual(TEXT("terrain is one section"), Terrain->GetNumSections(), 1);
		TestEqual(TEXT("water is one section"), Water->GetNumSections(), 1);
		TestTrue(TEXT("terrain is dense enough to hold relief"), Terrain->GetProcMeshSection(0)->ProcVertexBuffer.Num() > 10000);
		TestTrue(TEXT("terrain has collision"), Terrain->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
		TestTrue(TEXT("water has none"), Water->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachTerrainMatchesTest, "CrabSim.Beach.TerrainMeshMatchesTheHeightFunction", TestFlags)
bool FCrabBeachTerrainMatchesTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach) || !TestNotNull(TEXT("terrain"), Beach->GetTerrainMesh()))
	{
		return false;
	}

	const TArray<FProcMeshVertex>& Vertices = Beach->GetTerrainMesh()->GetProcMeshSection(0)->ProcVertexBuffer;
	int32 Checked = 0;
	for (int32 Index = 0; Index < Vertices.Num(); Index += 397)
	{
		const FProcMeshVertex& Vertex = Vertices[Index];
		TestNearlyEqual(*FString::Printf(TEXT("vertex %d sits on the height function"), Index), static_cast<float>(Vertex.Position.Z),
			CrabTerrain::Height(Vertex.Position.X, Vertex.Position.Y), 0.01f);
		TestTrue(*FString::Printf(TEXT("vertex %d normal points up"), Index), Vertex.Normal.Z > 0.f);
		++Checked;
	}
	TestTrue(TEXT("sampled a useful number of vertices"), Checked > 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachCollisionTest, "CrabSim.Beach.GroundIsSolidWhereTheMeshIs", TestFlags)
bool FCrabBeachCollisionTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach))
	{
		return false;
	}

	const FVector2D Spots[] = {FVector2D(0.f, 0.f), FVector2D(-1500.f, 800.f), FVector2D(1500.f, -600.f), FVector2D(-3000.f, 0.f), FVector2D(3500.f, 1200.f)};
	for (const FVector2D& Spot : Spots)
	{
		FHitResult Hit;
		const bool bHit = World.GetTestWorld()->LineTraceSingleByChannel(Hit, FVector(Spot.X, Spot.Y, 3000.f), FVector(Spot.X, Spot.Y, -3000.f), ECC_Visibility);
		if (TestTrue(*FString::Printf(TEXT("a trace hits the ground at %.0f,%.0f"), Spot.X, Spot.Y), bHit))
		{
			TestNearlyEqual(*FString::Printf(TEXT("and it hits at the ground height at %.0f,%.0f"), Spot.X, Spot.Y), static_cast<float>(Hit.ImpactPoint.Z),
				CrabTerrain::Height(Spot.X, Spot.Y), 12.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachWaterFollowsTideTest, "CrabSim.Beach.WaterMeshRidesTheTide", TestFlags)
bool FCrabBeachWaterFollowsTideTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach) || !TestNotNull(TEXT("water"), Beach->GetWaterMesh()))
	{
		return false;
	}

	Beach->SetTideClock(LowTide);
	const float LowZ = Beach->GetWaterMesh()->GetComponentLocation().Z;
	TestNearlyEqual(TEXT("water sits at the surface level at low tide"), LowZ, Beach->GetSurfaceLevel(), 0.01f);

	Beach->SetTideClock(HighTide);
	const float HighZ = Beach->GetWaterMesh()->GetComponentLocation().Z;
	TestNearlyEqual(TEXT("water sits at the surface level at high tide"), HighZ, Beach->GetSurfaceLevel(), 0.01f);
	TestTrue(TEXT("high tide water is well above low tide water"), HighZ - LowZ > 200.f);

	// It keeps following as the clock runs.
	Beach->SetTideClock(LowTide);
	World.TickSeconds(5.f);
	TestNearlyEqual(TEXT("still tracking after ticking"), static_cast<float>(Beach->GetWaterMesh()->GetComponentLocation().Z), Beach->GetSurfaceLevel(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachClockTest, "CrabSim.Beach.TideClockRunsAndClamps", TestFlags)
bool FCrabBeachClockTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach))
	{
		return false;
	}

	TestNearlyEqual(TEXT("starts at low tide"), Beach->GetTideClock(), 0.f, 0.05f);
	TestTrue(TEXT("and rising"), Beach->IsTideRising());

	Beach->SetTideClock(10.f);
	World.TickSeconds(2.f);
	TestNearlyEqual(TEXT("clock advances in real time"), Beach->GetTideClock(), 12.f, 0.1f);

	Beach->SetTideClock(-30.f);
	TestNearlyEqual(TEXT("negative time clamps to zero"), Beach->GetTideClock(), 0.f, 1e-4f);

	Beach->SetTideClock(HighTide);
	TestNearlyEqual(TEXT("fraction is full at high tide"), Beach->GetTideFraction(), 1.f, 1e-3f);
	Beach->SetTideClock(LowTide);
	TestNearlyEqual(TEXT("fraction is empty at low tide"), Beach->GetTideFraction(), 0.f, 1e-3f);
	Beach->SetTideClock(RisingSurge);
	TestTrue(TEXT("rising after low"), Beach->IsTideRising());
	Beach->SetTideClock(FallingSurge);
	TestFalse(TEXT("falling after high"), Beach->IsTideRising());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachDepthTest, "CrabSim.Beach.WaterDepthFollowsGroundAndTide", TestFlags)
bool FCrabBeachDepthTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach))
	{
		return false;
	}

	Beach->SetTideClock(LowTide);
	TestNearlyEqual(TEXT("the start is dry at low tide"), Beach->GetWaterDepthAt(0.f, 0.f), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("the dunes are dry at low tide"), Beach->GetWaterDepthAt(-3800.f, 0.f), 0.f, 1e-3f);
	TestTrue(TEXT("the seabed is under water at low tide"), Beach->GetWaterDepthAt(5000.f, 0.f) > 100.f);

	Beach->SetTideClock(HighTide);
	TestTrue(TEXT("the start is flooded at high tide"), Beach->GetWaterDepthAt(0.f, 0.f) > 60.f);
	TestNearlyEqual(TEXT("the dunes stay dry at high tide"), Beach->GetWaterDepthAt(-3800.f, 0.f), 0.f, 1e-3f);

	// Depth is surface minus ground, never negative.
	for (float X = -4000.f; X <= 8000.f; X += 800.f)
	{
		const float Depth = Beach->GetWaterDepthAt(X, 300.f);
		TestTrue(*FString::Printf(TEXT("depth is not negative at x=%.0f"), X), Depth >= 0.f);
		TestNearlyEqual(*FString::Printf(TEXT("depth equals surface minus ground at x=%.0f"), X), Depth,
			FMath::Max(0.f, Beach->GetSurfaceLevel() - Beach->GetGroundHeight(X, 300.f)), 1e-3f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachBurrowsTest, "CrabSim.Beach.BurrowsSitOnTheGroundAndAreFoundByProximity", TestFlags)
bool FCrabBeachBurrowsTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach))
	{
		return false;
	}

	const TArray<FCrabBurrow>& Burrows = Beach->GetBurrows();
	TestEqual(TEXT("four burrows"), Burrows.Num(), 4);
	for (int32 Index = 0; Index < Burrows.Num(); ++Index)
	{
		TestNearlyEqual(*FString::Printf(TEXT("burrow %d sits on the ground"), Index), static_cast<float>(Burrows[Index].Location.Z),
			CrabTerrain::Height(Burrows[Index].Location.X, Burrows[Index].Location.Y), 0.01f);
	}

	// Each is found from a little to one side, and not from far away.
	for (int32 Index = 0; Index < Burrows.Num(); ++Index)
	{
		const FVector Near = Burrows[Index].Location + FVector(40.f, 30.f, 0.f);
		TestEqual(*FString::Printf(TEXT("burrow %d found from nearby"), Index), Beach->FindBurrowNear(Near, 100.f), Index);
		TestEqual(*FString::Printf(TEXT("burrow %d not found from 500 away"), Index), Beach->FindBurrowNear(Burrows[Index].Location + FVector(500.f, 0.f, 0.f), 100.f), static_cast<int32>(INDEX_NONE));
	}

	// Height is ignored: the search is on the ground plane.
	TestEqual(TEXT("height is ignored"), Beach->FindBurrowNear(Burrows[1].Location + FVector(0.f, 0.f, 900.f), 50.f), 1);

	// With two in range the nearer one wins.
	const FVector Between = 0.5f * (Burrows[0].Location + Burrows[1].Location);
	const int32 Winner = Beach->FindBurrowNear(Between + (Burrows[1].Location - Burrows[0].Location).GetSafeNormal() * 10.f, 100000.f);
	TestEqual(TEXT("the nearer burrow wins"), Winner, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachFloodingTest, "CrabSim.Beach.TideFloodsBurrowsLowestFirst", TestFlags)
bool FCrabBeachFloodingTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach))
	{
		return false;
	}

	const int32 Safest = Beach->FindSafestBurrow();
	TestEqual(TEXT("the dune-foot burrow is the safest"), Safest, HighBurrow);
	for (int32 Index = 0; Index < Beach->GetBurrows().Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("burrow %d is no higher than the safest"), Index), Beach->GetBurrows()[Index].Location.Z <= Beach->GetBurrows()[Safest].Location.Z);
	}

	// At low tide nothing is flooded.
	Beach->SetTideClock(LowTide);
	for (int32 Index = 0; Index < Beach->GetBurrows().Num(); ++Index)
	{
		TestFalse(*FString::Printf(TEXT("burrow %d is dry at low tide"), Index), Beach->IsBurrowFlooded(Index));
	}

	// Across a whole tide the safest burrow never floods, and the lowest floods at some point.
	bool bLowestFlooded = false;
	bool bOrderKept = true;
	for (float Clock = 0.f; Clock <= 180.f; Clock += 2.f)
	{
		Beach->SetTideClock(Clock);
		TestFalse(*FString::Printf(TEXT("safest burrow stays dry at t=%.0f"), Clock), Beach->IsBurrowFlooded(Safest));
		bLowestFlooded |= Beach->IsBurrowFlooded(LowBurrow);
		// A higher burrow never floods while a lower one is dry.
		for (int32 A = 0; A < Beach->GetBurrows().Num(); ++A)
		{
			for (int32 B = 0; B < Beach->GetBurrows().Num(); ++B)
			{
				if (Beach->GetBurrows()[A].Location.Z > Beach->GetBurrows()[B].Location.Z && Beach->IsBurrowFlooded(A) && !Beach->IsBurrowFlooded(B))
				{
					bOrderKept = false;
				}
			}
		}
	}
	TestTrue(TEXT("the lowest burrow floods at some point in the tide"), bLowestFlooded);
	TestTrue(TEXT("higher burrows never flood before lower ones"), bOrderKept);

	TestFalse(TEXT("an invalid index is not flooded"), Beach->IsBurrowFlooded(99));
	TestFalse(TEXT("nor is INDEX_NONE"), Beach->IsBurrowFlooded(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachPropsTest, "CrabSim.Beach.PropsKeepClearOfTheStartAndBurrows", TestFlags)
bool FCrabBeachPropsTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Beach = SpawnBeach(World);
	if (!TestNotNull(TEXT("beach"), Beach))
	{
		return false;
	}

	TArray<UStaticMeshComponent*> Meshes;
	Beach->GetComponents<UStaticMeshComponent>(Meshes);
	TestTrue(TEXT("the beach has props"), Meshes.Num() > 20);

	int32 Rocks = 0;
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision || !Mesh->IsVisible())
		{
			continue;
		}
		++Rocks;
		const FVector Where = Mesh->GetComponentLocation();
		TestTrue(*FString::Printf(TEXT("blocking prop at %.0f,%.0f is clear of the start"), Where.X, Where.Y), FVector::Dist2D(Where, FVector::ZeroVector) >= 600.f);
		for (const FCrabBurrow& Burrow : Beach->GetBurrows())
		{
			TestTrue(*FString::Printf(TEXT("blocking prop at %.0f,%.0f is clear of a burrow"), Where.X, Where.Y), FVector::Dist2D(Where, Burrow.Location) >= 350.f);
		}
	}
	TestTrue(TEXT("rocks were placed"), Rocks >= Beach->RockCount / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabBeachDeterministicTest, "CrabSim.Beach.SameSeedGivesTheSameBeach", TestFlags)
bool FCrabBeachDeterministicTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* A = SpawnBeach(World);
	ACrabBeach* B = SpawnBeach(World);
	if (!TestNotNull(TEXT("first beach"), A) || !TestNotNull(TEXT("second beach"), B))
	{
		return false;
	}

	TArray<UStaticMeshComponent*> MeshesA;
	TArray<UStaticMeshComponent*> MeshesB;
	A->GetComponents<UStaticMeshComponent>(MeshesA);
	B->GetComponents<UStaticMeshComponent>(MeshesB);
	if (TestEqual(TEXT("same number of props"), MeshesA.Num(), MeshesB.Num()))
	{
		for (int32 Index = 0; Index < MeshesA.Num(); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("prop %d is in the same place"), Index), MeshesA[Index]->GetComponentLocation().Equals(MeshesB[Index]->GetComponentLocation(), 0.01f));
		}
	}
	return true;
}
