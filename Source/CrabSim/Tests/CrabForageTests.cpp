// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabDigMath.h"
#include "CrabFoodMath.h"
#include "CrabHudMath.h"
#include "CrabPlayerController.h"
#include "CrabSurvivalMath.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

using namespace UE::CrabSim::Tests;

namespace
{
	/** Index of the food patches the beach builds: high and poor first, low and rich last. */
	constexpr int32 PoorPatch = 0;
	constexpr int32 NearPatch = 3;
	constexpr int32 RichPatch = 4;
	constexpr int32 LowPatch = 6;

	/** A beach, a crab standing on it, and the pointer controller (which does not possess the crab). */
	struct FForageRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;
		ACrabPlayerController* Controller = nullptr;

		explicit FForageRig(float X = 0.f, float Y = 0.f)
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Crab = SpawnCrab(World, *Beach, X, Y);
					Controller = World.SpawnActor<ACrabPlayerController>();
				}
				if (Crab)
				{
					Settle(World);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Crab && Controller; }
		const FCrabFoodPatch& Patch(int32 Index) const { return Beach->GetFoodPatches()[Index]; }
		FVector Ground(float DX, float DY) const
		{
			const FVector At = Crab->GetActorLocation();
			return FVector(At.X + DX, At.Y + DY, Crab->GetFeetZ());
		}

		/** Put the crab on the ground at XY and let it settle. */
		void Teleport(float X, float Y)
		{
			Crab->SetActorLocation(FVector(X, Y, Beach->GetGroundHeight(X, Y) + 70.f), false, nullptr, ETeleportType::TeleportPhysics);
			Settle(World);
		}

		/** Dig where the crab stands, start to finish. Returns whether a burrow was dug. */
		bool DigHere()
		{
			const int32 Before = Beach->GetDugBurrowCount();
			Crab->SetFood(1.f);
			if (!Crab->StartDig())
			{
				return false;
			}
			World.TickSeconds(CrabDig::Duration + 0.3f);
			return Beach->GetDugBurrowCount() == Before + 1;
		}
	};

	int32 DugIndex(const ACrabBeach& Beach)
	{
		for (int32 Index = Beach.GetBurrows().Num() - 1; Index >= 0; --Index)
		{
			if (Beach.GetBurrows()[Index].bDug)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

// --- The patches on the beach --------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodPatchesExistTest, "CrabSim.Food.TheBeachHasSixToEightPatchesOnTheFlats", TestFlags)
bool FCrabFoodPatchesExistTest::RunTest(const FString& Parameters)
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

	const TArray<FCrabFoodPatch>& Patches = Beach->GetFoodPatches();
	TestTrue(*FString::Printf(TEXT("six to eight patches (%d)"), Patches.Num()), Patches.Num() >= 6 && Patches.Num() <= 8);
	Beach->SetTideClock(LowTide);
	for (int32 Index = 0; Index < Patches.Num(); ++Index)
	{
		const FCrabFoodPatch& Patch = Patches[Index];
		TestNearlyEqual(*FString::Printf(TEXT("patch %d sits on the ground"), Index), static_cast<float>(Patch.Location.Z),
			CrabTerrain::Height(Patch.Location.X, Patch.Location.Y), 0.01f);
		TestTrue(*FString::Printf(TEXT("patch %d has richness in (0, 1] (%.2f)"), Index, Patch.Richness), Patch.Richness > 0.f && Patch.Richness <= 1.f);
		TestNearlyEqual(*FString::Printf(TEXT("patch %d starts full"), Index), Patch.Richness, Patch.FullRichness, 1e-6f);
		TestTrue(*FString::Printf(TEXT("patch %d has a generous click radius (%.0f)"), Index, Patch.ClickRadius), Patch.ClickRadius >= 150.f);
		TestTrue(*FString::Printf(TEXT("patch %d is wider than the crab and no wider than its click radius"), Index), Patch.Radius >= 100.f && Patch.Radius <= Patch.ClickRadius);
		TestNearlyEqual(*FString::Printf(TEXT("patch %d is dry at low tide"), Index), Beach->GetWaterDepthAt(Patch.Location.X, Patch.Location.Y), 0.f, 1e-3f);
		TestTrue(*FString::Printf(TEXT("patch %d is on the exposed flats, not the dunes or the sea"), Index), Patch.Location.X > -2000.f && Patch.Location.X < 2200.f);
		TestFalse(*FString::Printf(TEXT("patch %d has not been soaked"), Index), Patch.bSoaked);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodPatchesLowerRicherTest, "CrabSim.Food.LowerPatchesAreRicherAndFloodSooner", TestFlags)
bool FCrabFoodPatchesLowerRicherTest::RunTest(const FString& Parameters)
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

	const TArray<FCrabFoodPatch>& Patches = Beach->GetFoodPatches();
	int32 Lowest = 0;
	int32 Highest = 0;
	for (int32 Index = 0; Index < Patches.Num(); ++Index)
	{
		Lowest = Patches[Index].Location.Z < Patches[Lowest].Location.Z ? Index : Lowest;
		Highest = Patches[Index].Location.Z > Patches[Highest].Location.Z ? Index : Highest;
	}
	TestTrue(TEXT("the lowest patch is richer than the highest"), Patches[Lowest].FullRichness > Patches[Highest].FullRichness + 0.3f);
	TestTrue(TEXT("the poor high patch is worth having"), Patches[Highest].FullRichness >= 0.3f);

	// Richer, and the tide reaches it earlier: it is under water at a tide that leaves the high one dry.
	auto FirstWetClock = [Beach](int32 Index) -> float
	{
		for (float Clock = 0.f; Clock <= 90.f; Clock += 0.5f)
		{
			Beach->SetTideClock(Clock);
			if (Beach->GetWaterDepthAt(Beach->GetFoodPatches()[Index].Location.X, Beach->GetFoodPatches()[Index].Location.Y) > CrabFood::SoakDepth)
			{
				return Clock;
			}
		}
		return 1000.f;
	};
	const float LowWet = FirstWetClock(Lowest);
	const float HighWet = FirstWetClock(Highest);
	TestTrue(*FString::Printf(TEXT("the low patch floods early (t=%.1f)"), LowWet), LowWet < 40.f);
	TestTrue(*FString::Printf(TEXT("the high patch floods much later (t=%.1f against t=%.1f)"), HighWet, LowWet), HighWet > LowWet + 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodPatchesClearTest, "CrabSim.Food.PatchesKeepClearOfRocksBurrowsAndEachOther", TestFlags)
bool FCrabFoodPatchesClearTest::RunTest(const FString& Parameters)
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

	const TArray<FCrabFoodPatch>& Patches = Beach->GetFoodPatches();
	TArray<UStaticMeshComponent*> Meshes;
	Beach->GetComponents<UStaticMeshComponent>(Meshes);
	for (int32 Index = 0; Index < Patches.Num(); ++Index)
	{
		const FVector Where = Patches[Index].Location;
		TestTrue(*FString::Printf(TEXT("patch %d is off the crab's start"), Index), FVector::Dist2D(Where, FVector::ZeroVector) >= 300.f);
		for (const FCrabBurrow& Burrow : Beach->GetBurrows())
		{
			TestTrue(*FString::Printf(TEXT("patch %d is clear of a burrow"), Index), FVector::Dist2D(Where, Burrow.Location) >= 300.f);
		}
		for (int32 Other = Index + 1; Other < Patches.Num(); ++Other)
		{
			TestTrue(*FString::Printf(TEXT("patches %d and %d are apart"), Index, Other), FVector::Dist2D(Where, Patches[Other].Location) >= 300.f);
		}
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision || !Mesh->IsVisible())
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("patch %d has no blocking prop within %.0f"), Index, Patches[Index].Radius),
				FVector::Dist2D(Where, Mesh->GetComponentLocation()) >= Patches[Index].Radius + 120.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodPatchesDeterministicTest, "CrabSim.Food.SameSeedGivesTheSamePatches", TestFlags)
bool FCrabFoodPatchesDeterministicTest::RunTest(const FString& Parameters)
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
	if (TestEqual(TEXT("same number of patches"), A->GetFoodPatches().Num(), B->GetFoodPatches().Num()))
	{
		for (int32 Index = 0; Index < A->GetFoodPatches().Num(); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("patch %d is in the same place"), Index), A->GetFoodPatches()[Index].Location.Equals(B->GetFoodPatches()[Index].Location, 0.01f));
			TestNearlyEqual(*FString::Printf(TEXT("patch %d is as rich"), Index), A->GetFoodPatches()[Index].Richness, B->GetFoodPatches()[Index].Richness, 1e-6f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodFindPatchTest, "CrabSim.Food.PatchesAreFoundByAGenerousClick", TestFlags)
bool FCrabFoodFindPatchTest::RunTest(const FString& Parameters)
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

	for (int32 Index = 0; Index < Beach->GetFoodPatches().Num(); ++Index)
	{
		const FCrabFoodPatch& Patch = Beach->GetFoodPatches()[Index];
		TestEqual(*FString::Printf(TEXT("patch %d is found on its centre"), Index), Beach->FindFoodPatchAt(Patch.Location), Index);
		TestEqual(*FString::Printf(TEXT("patch %d is found from 150 uu away"), Index), Beach->FindFoodPatchAt(Patch.Location + FVector(0.f, 150.f, 0.f)), Index);
		TestEqual(*FString::Printf(TEXT("patch %d is found just inside its click radius"), Index), Beach->FindFoodPatchAt(Patch.Location + FVector(Patch.ClickRadius - 5.f, 0.f, 0.f)), Index);
		TestEqual(*FString::Printf(TEXT("patch %d is not found from beyond it"), Index), Beach->FindFoodPatchAt(Patch.Location + FVector(0.f, Patch.ClickRadius + 20.f, 0.f)), static_cast<int32>(INDEX_NONE));
		TestEqual(*FString::Printf(TEXT("patch %d is found whatever the height"), Index), Beach->FindFoodPatchAt(Patch.Location + FVector(0.f, 0.f, 900.f)), Index);
	}

	// A bare patch is still a patch: clicking it says so instead of walking past it.
	Beach->TakeFood(RichPatch, 10.f);
	TestEqual(TEXT("a bare patch is still found"), Beach->FindFoodPatchAt(Beach->GetFoodPatches()[RichPatch].Location), RichPatch);

	TestNearlyEqual(TEXT("nearest patch distance on a centre"), Beach->GetNearestFoodPatchDistance(Beach->GetFoodPatches()[2].Location), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("nearest burrow distance on a centre"), Beach->GetNearestBurrowDistance(Beach->GetBurrows()[1].Location), 0.f, 1e-3f);
	TestTrue(TEXT("far out to sea there is nothing near"), Beach->GetNearestFoodPatchDistance(FVector(8000.f, 0.f, 0.f)) > 3000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodTakeTest, "CrabSim.Food.TakingFoodDepletesAPatchAndNeverOverdraws", TestFlags)
bool FCrabFoodTakeTest::RunTest(const FString& Parameters)
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

	const float Full = Beach->GetFoodPatches()[LowPatch].Richness;
	TestNearlyEqual(TEXT("takes what is asked"), Beach->TakeFood(LowPatch, 0.3f), 0.3f, 1e-6f);
	TestNearlyEqual(TEXT("the patch has that much less"), Beach->GetFoodPatches()[LowPatch].Richness, Full - 0.3f, 1e-6f);
	TestNearlyEqual(TEXT("asking for more than is left gives what is left"), Beach->TakeFood(LowPatch, 5.f), Full - 0.3f, 1e-6f);
	TestNearlyEqual(TEXT("and leaves it bare"), Beach->GetFoodPatches()[LowPatch].Richness, 0.f, 1e-6f);
	TestNearlyEqual(TEXT("a bare patch gives nothing"), Beach->TakeFood(LowPatch, 1.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("a negative ask gives nothing"), Beach->TakeFood(PoorPatch, -1.f), 0.f, 1e-6f);
	TestNearlyEqual(TEXT("a bad index gives nothing"), Beach->TakeFood(99, 1.f), 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodRefreshTest, "CrabSim.Food.TheTideRefillsASoakedPatchOnceTheWaterLeaves", TestFlags)
bool FCrabFoodRefreshTest::RunTest(const FString& Parameters)
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
	Beach->TakeFood(LowPatch, 0.6f);
	const float LowFull = Beach->GetFoodPatches()[LowPatch].FullRichness;

	// Under water the patch is not refilled yet.
	Beach->SetTideClock(HighTide);
	World.TickN(2, 1.f / 60.f);
	TestTrue(TEXT("the low patch is under water at high tide"), Beach->GetWaterDepthAt(Beach->GetFoodPatches()[LowPatch].Location.X, Beach->GetFoodPatches()[LowPatch].Location.Y) > CrabFood::SoakDepth);
	TestTrue(TEXT("and is marked soaked"), Beach->GetFoodPatches()[LowPatch].bSoaked);
	TestNearlyEqual(TEXT("but not yet refilled"), Beach->GetFoodPatches()[LowPatch].Richness, LowFull - 0.6f, 1e-4f);

	// Once the water leaves, it is fresh.
	Beach->SetTideClock(LowTide);
	World.TickN(2, 1.f / 60.f);
	TestNearlyEqual(TEXT("refilled to full when the water has left"), Beach->GetFoodPatches()[LowPatch].Richness, LowFull, 1e-4f);
	TestFalse(TEXT("no longer soaked"), Beach->GetFoodPatches()[LowPatch].bSoaked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodRefreshHighTest, "CrabSim.Food.EvenTheHighPatchRefillsAfterAHighTide", TestFlags)
bool FCrabFoodRefreshHighTest::RunTest(const FString& Parameters)
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
	Beach->TakeFood(PoorPatch, 10.f);
	TestNearlyEqual(TEXT("bare"), Beach->GetFoodPatches()[PoorPatch].Richness, 0.f, 1e-6f);
	Beach->SetTideClock(HighTide);
	TestTrue(TEXT("the high tide covers the high patch"), Beach->GetWaterDepthAt(Beach->GetFoodPatches()[PoorPatch].Location.X, Beach->GetFoodPatches()[PoorPatch].Location.Y) > CrabFood::SoakDepth);
	Beach->SetTideClock(LowTide);
	TestNearlyEqual(TEXT("and it is full again after"), Beach->GetFoodPatches()[PoorPatch].Richness, Beach->GetFoodPatches()[PoorPatch].FullRichness, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFoodShallowWaterTest, "CrabSim.Food.AFilmOfWaterDoesNotRefillAPatch", TestFlags)
bool FCrabFoodShallowWaterTest::RunTest(const FString& Parameters)
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

	// Find a moment when the sea only just touches the low patch: over the ground, under the soak line.
	const FVector Where = Beach->GetFoodPatches()[LowPatch].Location;
	float Film = -1.f;
	for (float Clock = 0.f; Clock < 90.f && Film < 0.f; Clock += 0.05f)
	{
		Beach->SetTideClock(Clock);
		const float Depth = Beach->GetWaterDepthAt(Where.X, Where.Y);
		if (Depth > 2.f && Depth <= 8.f)
		{
			Film = Clock;
		}
	}
	if (!TestTrue(TEXT("found a moment with a film of water on the patch"), Film >= 0.f))
	{
		return false;
	}

	Beach->TakeFood(LowPatch, 0.5f);
	const float Left = Beach->GetFoodPatches()[LowPatch].Richness;
	TestFalse(TEXT("a film is not a soaking"), Beach->GetFoodPatches()[LowPatch].bSoaked);
	Beach->SetTideClock(LowTide);
	TestNearlyEqual(TEXT("so when it goes the patch is as it was"), Beach->GetFoodPatches()[LowPatch].Richness, Left, 1e-6f);
	return true;
}

// --- The crab feeding ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedMovesFoodTest, "CrabSim.Food.SiftingMovesRichnessFromThePatchIntoTheCrab", TestFlags)
bool FCrabFeedMovesFoodTest::RunTest(const FString& Parameters)
{
	FForageRig Rig(-1300.f, 450.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const float FoodBefore = Rig.Crab->GetFood();
	const float PatchBefore = Rig.Patch(PoorPatch).Richness;
	TestTrue(TEXT("the crab has started a little hungry"), FoodBefore > 0.2f && FoodBefore < CrabFood::StartFood + 0.001f);

	TestTrue(TEXT("feeding starts on the patch"), Rig.Crab->StartFeeding(PoorPatch));
	TestTrue(TEXT("and reports it"), Rig.Crab->IsFeeding() && Rig.Crab->GetFeedingPatch() == PoorPatch);
	Rig.World.TickSeconds(2.f);

	const float Gained = Rig.Crab->GetFood() - FoodBefore + CrabFood::DrainPerSecond * 2.f;
	const float Lost = PatchBefore - Rig.Patch(PoorPatch).Richness;
	TestTrue(*FString::Printf(TEXT("food rises about 0.015 a second on a 0.4 patch (gained %.3f in 2 s)"), Gained), Gained > 0.02f && Gained < 0.04f);
	TestNearlyEqual(TEXT("the patch loses what the crab gains"), Lost, Gained, 0.012f);
	TestTrue(TEXT("still feeding: neither bare nor full"), Rig.Crab->IsFeeding());
	TestNearlyEqual(TEXT("the crab stood still"), static_cast<float>(FVector::Dist2D(Rig.Crab->GetActorLocation(), Rig.Patch(PoorPatch).Location)), 0.f, 10.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedRateTest, "CrabSim.Food.ARichPatchFeedsFasterThanAPoorOne", TestFlags)
bool FCrabFeedRateTest::RunTest(const FString& Parameters)
{
	auto FoodAfterThreeSeconds = [](int32 Patch, float X, float Y) -> float
	{
		FForageRig Rig(X, Y);
		if (!Rig.IsValid())
		{
			return -1.f;
		}
		const float Before = Rig.Crab->GetFood();
		Rig.Crab->StartFeeding(Patch);
		Rig.World.TickSeconds(3.f);
		return Rig.Crab->GetFood() - Before;
	};
	const float Poor = FoodAfterThreeSeconds(PoorPatch, -1300.f, 450.f);
	const float Rich = FoodAfterThreeSeconds(RichPatch, 1150.f, -80.f);
	TestTrue(TEXT("both runs worked"), Poor > 0.f && Rich > 0.f);
	TestTrue(*FString::Printf(TEXT("the rich patch fed more in the same time (%.3f against %.3f)"), Rich, Poor), Rich > Poor + 0.01f);
	TestTrue(*FString::Printf(TEXT("about 0.02 a second on the 0.8 patch (%.3f in 3 s)"), Rich), Rich > 0.04f && Rich < 0.07f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedEmptiesTest, "CrabSim.Food.FeedingRunsUntilThePatchIsEmpty", TestFlags)
bool FCrabFeedEmptiesTest::RunTest(const FString& Parameters)
{
	FForageRig Rig(-1300.f, 450.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const float FoodBefore = Rig.Crab->GetFood();
	const float Full = Rig.Patch(PoorPatch).FullRichness;
	Rig.Crab->StartFeeding(PoorPatch);

	float Seconds = 0.f;
	while (Rig.Crab->IsFeeding() && Seconds < 90.f)
	{
		Rig.World.TickSeconds(0.5f);
		Seconds += 0.5f;
	}
	TestFalse(TEXT("feeding stopped by itself"), Rig.Crab->IsFeeding());
	TestTrue(*FString::Printf(TEXT("after about 37 seconds on a 0.4 patch (%.1f)"), Seconds), Seconds > 30.f && Seconds < 45.f);
	TestTrue(TEXT("the patch is empty"), CrabFood::IsEmpty(Rig.Patch(PoorPatch).Richness));
	TestTrue(TEXT("and says so"), Rig.Crab->GetMessage().Contains(TEXT("empty")));
	const float Gained = Rig.Crab->GetFood() - FoodBefore + CrabFood::DrainPerSecond * Seconds;
	TestNearlyEqual(TEXT("the crab got what the patch held"), Gained, Full, 0.02f);
	TestTrue(TEXT("and is not full"), !CrabFood::IsFull(Rig.Crab->GetFood()));

	// It does not start again on bare mud.
	TestFalse(TEXT("a bare patch cannot be fed on"), Rig.Crab->StartFeeding(PoorPatch));
	Rig.World.TickSeconds(1.f);
	TestFalse(TEXT("still not feeding"), Rig.Crab->IsFeeding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedFullTest, "CrabSim.Food.FeedingStopsWhenTheCrabIsFull", TestFlags)
bool FCrabFeedFullTest::RunTest(const FString& Parameters)
{
	FForageRig Rig(1150.f, -80.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(0.9f);
	TestTrue(TEXT("feeding starts"), Rig.Crab->StartFeeding(RichPatch));
	for (int32 Frame = 0; Frame < 300 && Rig.Crab->IsFeeding(); ++Frame)
	{
		Rig.World.TickN(1, 1.f / 60.f);
	}

	TestFalse(TEXT("feeding stopped"), Rig.Crab->IsFeeding());
	TestTrue(*FString::Printf(TEXT("the crab is full (%.3f)"), Rig.Crab->GetFood()), CrabFood::IsFull(Rig.Crab->GetFood()));
	TestTrue(*FString::Printf(TEXT("the patch still has food (%.2f)"), Rig.Patch(RichPatch).Richness), Rig.Patch(RichPatch).Richness > 0.6f);
	TestTrue(TEXT("and it says so"), Rig.Crab->GetMessage().Contains(TEXT("Fed")));
	TestFalse(TEXT("a full crab will not start again"), Rig.Crab->StartFeeding(RichPatch));
	TestTrue(TEXT("and says it is not hungry"), Rig.Crab->GetMessage().Contains(TEXT("hungry")));

	// Hunger takes it back under the line in time, and it can eat a little more.
	Rig.World.TickSeconds(8.f);
	TestFalse(TEXT("after a while it is hungry again"), CrabFood::IsFull(Rig.Crab->GetFood()));
	TestTrue(TEXT("and can feed"), Rig.Crab->StartFeeding(RichPatch));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedArrivalTest, "CrabSim.Food.WalkingToAPatchStartsFeedingOnArrival", TestFlags)
bool FCrabFeedArrivalTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Centre = Rig.Patch(NearPatch).Location;
	Rig.Crab->SetMoveTarget(Centre, INDEX_NONE, NearPatch);
	TestTrue(TEXT("it sets off"), Rig.Crab->HasMoveTarget());
	TestFalse(TEXT("not feeding yet"), Rig.Crab->IsFeeding());

	Rig.World.TickSeconds(7.f);
	TestFalse(TEXT("arrived"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("and feeding"), Rig.Crab->IsFeeding() && Rig.Crab->GetFeedingPatch() == NearPatch);
	TestTrue(TEXT("standing on the patch"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Centre) < 60.f);
	TestTrue(*FString::Printf(TEXT("and eating (food %.3f)"), Rig.Crab->GetFood()), Rig.Crab->GetFood() > CrabFood::StartFood - 0.02f + 0.03f);
	TestTrue(TEXT("the patch is being drawn down"), Rig.Patch(NearPatch).Richness < Rig.Patch(NearPatch).FullRichness);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedStopsOnMoveTest, "CrabSim.Food.AnyMoveStopsFeeding", TestFlags)
bool FCrabFeedStopsOnMoveTest::RunTest(const FString& Parameters)
{
	auto FeedThenDo = [](TFunctionRef<void(FForageRig&)> Interrupt) -> bool
	{
		FForageRig Rig(1150.f, -80.f);
		if (!Rig.IsValid())
		{
			return false;
		}
		Rig.Crab->StartFeeding(RichPatch);
		Rig.World.TickSeconds(1.f);
		if (!Rig.Crab->IsFeeding())
		{
			return false;
		}
		Interrupt(Rig);
		const bool bStopped = !Rig.Crab->IsFeeding();
		const float Left = Rig.Patch(RichPatch).Richness;
		Rig.World.TickSeconds(1.5f);
		return bStopped && FMath::IsNearlyEqual(Rig.Patch(RichPatch).Richness, Left, 1e-6f);
	};

	TestTrue(TEXT("a click elsewhere stops it, and the patch stops draining"),
		FeedThenDo([](FForageRig& Rig) { Rig.Crab->SetMoveTarget(Rig.Ground(0.f, 400.f)); }));
	TestTrue(TEXT("a dash stops it"),
		FeedThenDo([](FForageRig& Rig) { Rig.Crab->TryDash(Rig.Ground(0.f, 400.f)); }));
	TestTrue(TEXT("a dance stops it"),
		FeedThenDo([](FForageRig& Rig) { Rig.Crab->StartDance(); }));
	TestTrue(TEXT("digging into a burrow stops it"),
		FeedThenDo([](FForageRig& Rig) { Rig.Crab->EnterBurrow(HighBurrow); }));
	TestTrue(TEXT("a click on another patch stops it"),
		FeedThenDo([](FForageRig& Rig) { Rig.Crab->SetMoveTarget(Rig.Patch(LowPatch).Location, INDEX_NONE, LowPatch); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedHoldKeepsFeedingTest, "CrabSim.Food.HoldingOnTheSamePatchKeepsFeeding", TestFlags)
bool FCrabFeedHoldKeepsFeedingTest::RunTest(const FString& Parameters)
{
	FForageRig Rig(1150.f, -80.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->StartFeeding(RichPatch);
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		// The held button re-targets the patch every frame.
		Rig.Controller->HandleHold(*Rig.Crab, Rig.Patch(RichPatch).Location + FVector(60.f, 40.f, 0.f));
		Rig.World.TickN(1, 1.f / 60.f);
	}
	TestTrue(TEXT("still feeding after a second and a half of holding"), Rig.Crab->IsFeeding());
	TestFalse(TEXT("with no walk started"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedSurgeTest, "CrabSim.Food.TheSurgeStopsFeeding", TestFlags)
bool FCrabFeedSurgeTest::RunTest(const FString& Parameters)
{
	FForageRig Rig(1150.f, -80.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->StartFeeding(RichPatch);
	Rig.World.TickSeconds(1.f);
	TestTrue(TEXT("feeding while dry"), Rig.Crab->IsFeeding());

	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickSeconds(0.5f);
	TestTrue(TEXT("the crab is in the surge"), Rig.Crab->GetWaterDepth() > CrabSurvival::SurgeDepth);
	TestFalse(TEXT("and no longer feeding"), Rig.Crab->IsFeeding());
	TestFalse(TEXT("and cannot start"), Rig.Crab->StartFeeding(RichPatch));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedRefusalsTest, "CrabSim.Food.CannotFeedOffThePatchOrOnABadIndex", TestFlags)
bool FCrabFeedRefusalsTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("not on the patch"), Rig.Crab->StartFeeding(NearPatch));
	TestFalse(TEXT("a bad index"), Rig.Crab->StartFeeding(99));
	TestFalse(TEXT("no index"), Rig.Crab->StartFeeding(INDEX_NONE));
	TestFalse(TEXT("nothing started"), Rig.Crab->IsFeeding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabFeedDrainTest, "CrabSim.Food.HungerDrainsSlowlyAndDoesNotKill", TestFlags)
bool FCrabFeedDrainTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const float Before = Rig.Crab->GetFood();
	Rig.World.TickSeconds(10.f);
	TestNearlyEqual(TEXT("ten seconds cost about 0.04"), Before - Rig.Crab->GetFood(), 0.04f, 0.006f);

	Rig.Crab->SetFood(0.001f);
	Rig.World.TickSeconds(2.f);
	TestNearlyEqual(TEXT("food stops at zero"), Rig.Crab->GetFood(), 0.f, 1e-6f);
	TestEqual(TEXT("an empty crab is not swept away"), Rig.Crab->GetSweptCount(), 0);
	TestNearlyEqual(TEXT("or weakened"), Rig.Crab->GetGrip(), 1.f, 1e-4f);
	const FVector Goal = Rig.Ground(0.f, 300.f);
	Rig.Crab->SetMoveTarget(Goal);
	Rig.World.TickSeconds(1.5f);
	TestTrue(TEXT("it still walks"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Goal) < 80.f);

	Rig.Crab->SetFood(5.f);
	TestNearlyEqual(TEXT("food is capped at 1"), Rig.Crab->GetFood(), 1.f, 1e-6f);
	Rig.Crab->SetFood(-5.f);
	TestNearlyEqual(TEXT("and floored at 0"), Rig.Crab->GetFood(), 0.f, 1e-6f);
	return true;
}

// --- Clicking a patch ------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerPatchClickTest, "CrabSim.Food.ClickingAPatchWalksThereAndFeeds", TestFlags)
bool FCrabControllerPatchClickTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Centre = Rig.Patch(NearPatch).Location;
	// Off the middle by a generous margin: still a click on the patch, and it heads for the middle.
	Rig.Controller->HandleClick(*Rig.Crab, Centre + FVector(150.f, -90.f, 0.f));
	TestTrue(TEXT("target set"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("the target is the patch's middle"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Centre) < 1.f);
	TestFalse(TEXT("not a dance"), Rig.Crab->IsDancing());

	Rig.World.TickSeconds(7.f);
	TestTrue(TEXT("it walks there and feeds"), Rig.Crab->IsFeeding() && Rig.Crab->GetFeedingPatch() == NearPatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerPatchMissTest, "CrabSim.Food.ClickingBeyondAPatchIsJustAWalk", TestFlags)
bool FCrabControllerPatchMissTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector Centre = Rig.Patch(NearPatch).Location;
	const FVector Miss = Centre + FVector(0.f, Rig.Patch(NearPatch).ClickRadius + 60.f, 0.f);
	Rig.Controller->HandleClick(*Rig.Crab, Miss);
	TestTrue(TEXT("a plain walk to the click"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Miss) < 1.f);

	Rig.World.TickSeconds(7.f);
	TestFalse(TEXT("and no feeding on arrival"), Rig.Crab->IsFeeding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabControllerPatchPriorityTest, "CrabSim.Food.TheCrabAndABurrowBeatThePatchUnderThem", TestFlags)
bool FCrabControllerPatchPriorityTest::RunTest(const FString& Parameters)
{
	// A click on the crab is a dance even when the crab stands in a patch.
	{
		FForageRig Rig(1150.f, -80.f);
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->StartFeeding(RichPatch);
		Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(25.f, 15.f));
		TestTrue(TEXT("the click on the crab starts the dance"), Rig.Crab->IsDancing());
		TestFalse(TEXT("which ends the feeding"), Rig.Crab->IsFeeding());
	}
	// A burrow dug beside a patch wins where their click zones overlap.
	{
		FForageRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		const FVector Centre = Rig.Patch(NearPatch).Location;
		const int32 Hole = Rig.Beach->AddDugBurrow(Centre + FVector(260.f, 0.f, 0.f));
		const FVector Click = Rig.Beach->GetBurrows()[Hole].Location + FVector(-90.f, 0.f, 0.f);
		TestTrue(TEXT("the click is inside both click zones"), FVector::Dist2D(Click, Centre) <= Rig.Patch(NearPatch).ClickRadius);
		Rig.Controller->HandleClick(*Rig.Crab, Click);
		TestTrue(TEXT("the burrow wins"), FVector::Dist2D(Rig.Crab->GetMoveTarget(), Rig.Beach->GetBurrows()[Hole].Location) < 1.f);
	}
	return true;
}

// --- Digging -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigSucceedsTest, "CrabSim.Dig.DiggingTakesFourSecondsAndCostsFood", TestFlags)
bool FCrabDigSucceedsTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(0.8f);
	const FVector Where = Rig.Crab->GetActorLocation();
	TestEqual(TEXT("digging is allowed here"), Rig.Crab->CheckDig(), CrabDig::EResult::Ok);
	TestTrue(TEXT("digging starts"), Rig.Crab->StartDig());
	TestTrue(TEXT("and reports it"), Rig.Crab->IsDigging());
	TestFalse(TEXT("standing still, not walking"), Rig.Crab->HasMoveTarget());
	TestEqual(TEXT("no burrow yet"), Rig.Beach->GetDugBurrowCount(), 0);

	Rig.World.TickSeconds(2.f);
	TestTrue(TEXT("still digging at two seconds"), Rig.Crab->IsDigging());
	TestNearlyEqual(TEXT("halfway"), Rig.Crab->GetDigProgress(), 0.5f, 0.06f);
	TestEqual(TEXT("and no burrow yet"), Rig.Beach->GetDugBurrowCount(), 0);
	TestNearlyEqual(TEXT("no cost until it is finished"), Rig.Crab->GetFood(), 0.8f - CrabFood::DrainPerSecond * 2.f, 0.005f);

	Rig.World.TickSeconds(2.3f);
	TestFalse(TEXT("done"), Rig.Crab->IsDigging());
	TestEqual(TEXT("a burrow was dug"), Rig.Beach->GetDugBurrowCount(), 1);
	const int32 Index = DugIndex(*Rig.Beach);
	if (TestTrue(TEXT("it is in the list"), Index != INDEX_NONE))
	{
		const FCrabBurrow& Burrow = Rig.Beach->GetBurrows()[Index];
		TestTrue(TEXT("marked as dug"), Burrow.bDug);
		TestTrue(TEXT("where the crab stood"), FVector::Dist2D(Burrow.Location, Where) < 40.f);
		TestNearlyEqual(TEXT("on the ground"), static_cast<float>(Burrow.Location.Z), CrabTerrain::Height(Burrow.Location.X, Burrow.Location.Y), 0.01f);
		TestEqual(TEXT("with the same flood rule as the others"), Burrow.FloodDepth, Rig.Beach->GetBurrows()[LowBurrow].FloodDepth);
		TestEqual(TEXT("and the same size"), Burrow.Radius, Rig.Beach->GetBurrows()[LowBurrow].Radius);
	}
	TestNearlyEqual(TEXT("it cost 0.30 food"), Rig.Crab->GetFood(), 0.8f - CrabDig::FoodCost - CrabFood::DrainPerSecond * 4.3f, 0.01f);
	TestTrue(TEXT("and the crab says so"), Rig.Crab->GetMessage().Contains(TEXT("dug")));
	TestEqual(TEXT("the beach's own burrows are untouched"), Rig.Beach->GetBurrows().Num(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigCancelTest, "CrabSim.Dig.AnyMoveCancelsTheDigWithNoCost", TestFlags)
bool FCrabDigCancelTest::RunTest(const FString& Parameters)
{
	auto DigThenDo = [](TFunctionRef<void(FForageRig&)> Interrupt, bool bExpectMessage = false) -> bool
	{
		FForageRig Rig;
		if (!Rig.IsValid())
		{
			return false;
		}
		Rig.Crab->SetFood(0.8f);
		Rig.Crab->StartDig();
		Rig.World.TickSeconds(1.5f);
		if (!Rig.Crab->IsDigging())
		{
			return false;
		}
		const float FoodBefore = Rig.Crab->GetFood();
		Interrupt(Rig);
		const bool bCancelled = !Rig.Crab->IsDigging() && (!bExpectMessage || Rig.Crab->GetMessage().Contains(TEXT("cancel")));
		Rig.World.TickSeconds(3.f);
		const bool bNothing = Rig.Beach->GetDugBurrowCount() == 0;
		const bool bFree = Rig.Crab->GetFood() > FoodBefore - CrabFood::DrainPerSecond * 3.5f - 0.001f;
		return bCancelled && bNothing && bFree;
	};

	TestTrue(TEXT("a walk cancels it, and says so"), DigThenDo([](FForageRig& Rig) { Rig.Crab->SetMoveTarget(Rig.Ground(0.f, 400.f)); }, true));
	TestTrue(TEXT("a click on the ground cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(300.f, 300.f)); }));
	TestTrue(TEXT("a hold that follows the cursor cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Controller->HandleHold(*Rig.Crab, Rig.Ground(-300.f, 300.f)); }));
	TestTrue(TEXT("a dash cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Crab->TryDash(Rig.Ground(0.f, 400.f)); }));
	TestTrue(TEXT("a click on a burrow cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Controller->HandleClick(*Rig.Crab, Rig.Beach->GetBurrows()[2].Location); }));
	TestTrue(TEXT("a click on a food patch cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Controller->HandleClick(*Rig.Crab, Rig.Patch(NearPatch).Location); }));
	TestTrue(TEXT("a dance cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Crab->StartDance(); }));
	TestTrue(TEXT("digging into a burrow cancels it"), DigThenDo([](FForageRig& Rig) { Rig.Crab->EnterBurrow(HighBurrow); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigWaterTest, "CrabSim.Dig.TheWaterArrivingCancelsTheDig", TestFlags)
bool FCrabDigWaterTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.Crab->SetFood(0.8f);
	TestTrue(TEXT("digging starts on dry sand"), Rig.Crab->StartDig());
	Rig.World.TickSeconds(1.f);
	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickSeconds(0.5f);
	TestFalse(TEXT("the water reached the spot: the dig is off"), Rig.Crab->IsDigging());
	TestEqual(TEXT("no burrow"), Rig.Beach->GetDugBurrowCount(), 0);
	Rig.World.TickSeconds(4.f);
	TestEqual(TEXT("and none later"), Rig.Beach->GetDugBurrowCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigRefusalsTest, "CrabSim.Dig.EachRefusalHasItsOwnReasonAndMessage", TestFlags)
bool FCrabDigRefusalsTest::RunTest(const FString& Parameters)
{
	// Not enough food.
	{
		FForageRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(CrabDig::FoodCost - 0.05f);
		TestEqual(TEXT("short of food"), Rig.Crab->CheckDig(), CrabDig::EResult::NotEnoughFood);
		TestFalse(TEXT("so it will not dig"), Rig.Crab->StartDig());
		TestFalse(TEXT("and does not start"), Rig.Crab->IsDigging());
		TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("Cannot dig")) && Rig.Crab->GetMessage().Contains(TEXT("food")));
	}
	// Wet sand.
	{
		FForageRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(0.8f);
		Rig.Beach->SetTideClock(HighTide);
		TestEqual(TEXT("under the sea"), Rig.Crab->CheckDig(), CrabDig::EResult::Wet);
		TestFalse(TEXT("so it will not dig"), Rig.Crab->StartDig());
		TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("wet")));
	}
	// Too close to a burrow.
	{
		FForageRig Rig(700.f + 150.f, 760.f);
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(0.8f);
		TestEqual(TEXT("150 from a burrow"), Rig.Crab->CheckDig(), CrabDig::EResult::TooCloseToBurrow);
		TestFalse(TEXT("so it will not dig"), Rig.Crab->StartDig());
		TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("Cannot dig: too close to a burrow")));
	}
	// Too close to a food patch.
	{
		FForageRig Rig(600.f - 120.f, -650.f);
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(0.8f);
		TestEqual(TEXT("120 from a patch"), Rig.Crab->CheckDig(), CrabDig::EResult::TooCloseToPatch);
		TestFalse(TEXT("so it will not dig"), Rig.Crab->StartDig());
		TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("food patch")));
	}
	// Feeding on the patch is too close to dig, too.
	{
		FForageRig Rig(1150.f, -80.f);
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(0.5f);
		Rig.Crab->StartFeeding(RichPatch);
		TestFalse(TEXT("no digging on the patch itself"), Rig.Crab->StartDig());
		TestTrue(TEXT("and the crab is still feeding"), Rig.Crab->IsFeeding());
	}
	// In a burrow.
	{
		FForageRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Crab->SetFood(0.8f);
		Rig.Crab->EnterBurrow(HighBurrow);
		TestEqual(TEXT("inside a burrow"), Rig.Crab->CheckDig(), CrabDig::EResult::InBurrow);
		TestFalse(TEXT("so it will not dig"), Rig.Crab->StartDig());
		TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("burrow")));
	}
	// Three dug already.
	{
		FForageRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Rig.Beach->AddDugBurrow(FVector(-2000.f, 1200.f, 0.f));
		Rig.Beach->AddDugBurrow(FVector(-2000.f, -1200.f, 0.f));
		Rig.Beach->AddDugBurrow(FVector(-3000.f, 0.f, 0.f));
		Rig.Crab->SetFood(0.8f);
		TestEqual(TEXT("three dug"), Rig.Crab->CheckDig(), CrabDig::EResult::TooManyDug);
		TestFalse(TEXT("so it will not dig"), Rig.Crab->StartDig());
		TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("3 burrows")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigLimitWorldTest, "CrabSim.Dig.AtMostThreeDugBurrowsAlive", TestFlags)
bool FCrabDigLimitWorldTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const FVector2D Spots[] = {FVector2D(0.f, 0.f), FVector2D(0.f, 450.f), FVector2D(-450.f, 0.f)};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Rig.Teleport(Spots[Index].X, Spots[Index].Y);
		TestTrue(*FString::Printf(TEXT("dig %d succeeds"), Index + 1), Rig.DigHere());
		TestEqual(*FString::Printf(TEXT("%d dug"), Index + 1), Rig.Beach->GetDugBurrowCount(), Index + 1);
	}

	Rig.Teleport(-450.f, 450.f);
	Rig.Crab->SetFood(1.f);
	TestEqual(TEXT("a fourth is refused: too many"), Rig.Crab->CheckDig(), CrabDig::EResult::TooManyDug);
	TestFalse(TEXT("it will not start"), Rig.Crab->StartDig());
	TestEqual(TEXT("still three"), Rig.Beach->GetDugBurrowCount(), 3);
	TestEqual(TEXT("and the direct route is closed too"), Rig.Beach->AddDugBurrow(FVector(-450.f, 450.f, 0.f)), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("the beach holds four of its own and three dug"), Rig.Beach->GetBurrows().Num(), 7);

	// The new holes are as far apart as the rule says.
	for (int32 A = 4; A < Rig.Beach->GetBurrows().Num(); ++A)
	{
		for (int32 B = A + 1; B < Rig.Beach->GetBurrows().Num(); ++B)
		{
			TestTrue(*FString::Printf(TEXT("dug burrows %d and %d are 250 apart"), A, B),
				FVector::Dist2D(Rig.Beach->GetBurrows()[A].Location, Rig.Beach->GetBurrows()[B].Location) >= CrabDig::MinSpacing);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDigNextToADugOneTest, "CrabSim.Dig.ADugBurrowCountsAsABurrowForSpacing", TestFlags)
bool FCrabDigNextToADugOneTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("first dig"), Rig.DigHere());
	Rig.Crab->SetFood(1.f);
	TestEqual(TEXT("standing on the new hole: too close"), Rig.Crab->CheckDig(), CrabDig::EResult::TooCloseToBurrow);
	Rig.Teleport(0.f, 200.f);
	Rig.Crab->SetFood(1.f);
	TestEqual(TEXT("200 away is still too close"), Rig.Crab->CheckDig(), CrabDig::EResult::TooCloseToBurrow);
	Rig.Teleport(0.f, 330.f);
	Rig.Crab->SetFood(1.f);
	TestEqual(TEXT("330 away is fine"), Rig.Crab->CheckDig(), CrabDig::EResult::Ok);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDugBurrowEnterTest, "CrabSim.Dig.ADugBurrowCanBeEnteredByClickingIt", TestFlags)
bool FCrabDugBurrowEnterTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("dug a burrow"), Rig.DigHere());
	const int32 Hole = DugIndex(*Rig.Beach);
	if (!TestTrue(TEXT("and found it"), Hole != INDEX_NONE))
	{
		return false;
	}

	// Walk off, then click the hole: the crab goes back and digs in.
	Rig.Crab->SetMoveTarget(Rig.Ground(0.f, 500.f));
	Rig.World.TickSeconds(2.5f);
	TestFalse(TEXT("out in the open"), Rig.Crab->IsInBurrow());
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Beach->GetBurrows()[Hole].Location + FVector(30.f, 20.f, 0.f));
	TestTrue(TEXT("a click on the hole is a burrow click"), Rig.Crab->HasMoveTarget());
	Rig.World.TickSeconds(4.f);
	TestTrue(TEXT("dug in"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("in the new burrow"), Rig.Crab->GetCurrentBurrow(), Hole);
	TestTrue(TEXT("on the hole"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Rig.Beach->GetBurrows()[Hole].Location) < 30.f);

	// And out again by clicking elsewhere, like any burrow.
	Rig.Controller->HandleClick(*Rig.Crab, Rig.Ground(0.f, 500.f));
	TestFalse(TEXT("out"), Rig.Crab->IsInBurrow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDugBurrowFloodTest, "CrabSim.Dig.ADugBurrowFloodsByTheSameRuleAsTheOthers", TestFlags)
bool FCrabDugBurrowFloodTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	TestTrue(TEXT("dug a burrow"), Rig.DigHere());
	const int32 Hole = DugIndex(*Rig.Beach);
	if (!TestTrue(TEXT("and found it"), Hole != INDEX_NONE))
	{
		return false;
	}
	const FVector Where = Rig.Beach->GetBurrows()[Hole].Location;

	TestFalse(TEXT("dry at low tide"), Rig.Beach->IsBurrowFlooded(Hole));
	bool bEverFlooded = false;
	for (float Clock = 0.f; Clock <= 180.f; Clock += 2.f)
	{
		Rig.Beach->SetTideClock(Clock);
		const bool bRule = Rig.Beach->GetWaterDepthAt(Where.X, Where.Y) > 50.f;
		TestEqual(*FString::Printf(TEXT("flooded exactly when the water is over 50 cm at t=%.0f"), Clock), Rig.Beach->IsBurrowFlooded(Hole), bRule);
		bEverFlooded |= bRule;
	}
	TestTrue(TEXT("a burrow dug this low floods in the tide"), bEverFlooded);

	// Inside it at the wrong time, the sea forces the crab out.
	Rig.Beach->SetTideClock(LowTide);
	TestTrue(TEXT("dig in"), Rig.Crab->EnterBurrow(Hole));
	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickSeconds(0.5f);
	TestFalse(TEXT("flooded out"), Rig.Crab->IsInBurrow());
	TestTrue(TEXT("and told so"), Rig.Crab->GetMessage().Contains(TEXT("Flooded")));
	TestFalse(TEXT("and cannot dig back in while it is flooded"), Rig.Crab->EnterBurrow(Hole));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDugHighBurrowTest, "CrabSim.Dig.ABurrowDugHighIsSafeAllTideLong", TestFlags)
bool FCrabDugHighBurrowTest::RunTest(const FString& Parameters)
{
	FForageRig Rig(-3000.f, 0.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	TestTrue(TEXT("dug a burrow on the dunes"), Rig.DigHere());
	const int32 Hole = DugIndex(*Rig.Beach);
	if (!TestTrue(TEXT("and found it"), Hole != INDEX_NONE))
	{
		return false;
	}
	for (float Clock = 0.f; Clock <= 180.f; Clock += 2.f)
	{
		Rig.Beach->SetTideClock(Clock);
		TestFalse(*FString::Printf(TEXT("the high burrow stays dry at t=%.0f"), Clock), Rig.Beach->IsBurrowFlooded(Hole));
	}

	Rig.Beach->SetTideClock(HighTide - 2.f);
	TestTrue(TEXT("dig in"), Rig.Crab->EnterBurrow(Hole));
	Rig.World.TickSeconds(4.f);
	TestTrue(TEXT("still in it at the top of the tide"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("never swept"), Rig.Crab->GetSweptCount(), 0);
	TestNearlyEqual(TEXT("grip intact"), Rig.Crab->GetGrip(), 1.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDugBurrowHavenTest, "CrabSim.Dig.TheSeaStillWashesTheCrabUpByTheDunesNotAtADugBurrow", TestFlags)
bool FCrabDugBurrowHavenTest::RunTest(const FString& Parameters)
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
	TestEqual(TEXT("the highest of the beach's burrows is the haven"), Beach->FindSafestBurrow(), HighBurrow);
	// A hole dug higher than any of the beach's own does not take its place.
	const int32 Hole = Beach->AddDugBurrow(FVector(-3300.f, 0.f, 0.f));
	TestTrue(TEXT("the dug hole is the highest of all"), Beach->GetBurrows()[Hole].Location.Z > Beach->GetBurrows()[HighBurrow].Location.Z);
	TestEqual(TEXT("but the haven is the same"), Beach->FindSafestBurrow(), HighBurrow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabDugBurrowBeachTest, "CrabSim.Dig.TheBeachAddsDugBurrowsOnTheGroundAndStopsAtThree", TestFlags)
bool FCrabDugBurrowBeachTest::RunTest(const FString& Parameters)
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
	TestEqual(TEXT("none dug at the start"), Beach->GetDugBurrowCount(), 0);
	TestEqual(TEXT("four burrows to begin with"), Beach->GetBurrows().Num(), 4);

	for (int32 Index = 0; Index < CrabDig::MaxDug; ++Index)
	{
		const FVector At(-700.f * (Index + 1), 900.f, 5000.f);
		const int32 Hole = Beach->AddDugBurrow(At);
		if (TestEqual(*FString::Printf(TEXT("dug burrow %d gets the next index"), Index + 1), Hole, 4 + Index))
		{
			TestTrue(TEXT("marked as dug"), Beach->GetBurrows()[Hole].bDug);
			TestNearlyEqual(TEXT("dropped onto the ground whatever the height asked for"), static_cast<float>(Beach->GetBurrows()[Hole].Location.Z),
				CrabTerrain::Height(At.X, At.Y), 0.01f);
			TestEqual(TEXT("found by proximity like any burrow"), Beach->FindBurrowNear(At, 100.f), Hole);
		}
	}
	TestEqual(TEXT("three dug"), Beach->GetDugBurrowCount(), 3);
	TestEqual(TEXT("a fourth is refused"), Beach->AddDugBurrow(FVector(-1500.f, -1200.f, 0.f)), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and changes nothing"), Beach->GetBurrows().Num(), 7);
	TestEqual(TEXT("the beach's own burrows keep their indices"), Beach->FindBurrowNear(FVector(700.f, 760.f, 0.f), 50.f), 2);
	return true;
}

// --- The HUD's dig button ------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudClickDigsNotWalksTest, "CrabSim.Hud.ClickingTheDigButtonDigsAndDoesNotWalk", TestFlags)
bool FCrabHudClickDigsNotWalksTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(0.8f);
	const FVector2D View(1280.f, 720.f);
	const FVector2D Button = CrabHud::DigButtonRect(View.X, View.Y).GetCenter();
	// The ground the cursor points at, behind the button, is somewhere a click would walk to.
	const FVector Behind = Rig.Ground(0.f, 900.f);
	const FVector Start = Rig.Crab->GetActorLocation();

	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, View, &Behind);
	TestTrue(TEXT("the dig started"), Rig.Crab->IsDigging());
	TestFalse(TEXT("and the click was not a walk"), Rig.Crab->HasMoveTarget());

	// The rest of the press, held over the button or dragged off it, is not a walk either.
	Rig.Controller->HandleHold(*Rig.Crab, Behind);
	TestFalse(TEXT("holding the button does not follow the ground behind it"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("and the dig goes on"), Rig.Crab->IsDigging());

	Rig.World.TickSeconds(CrabDig::Duration + 0.3f);
	TestEqual(TEXT("the burrow was dug"), Rig.Beach->GetDugBurrowCount(), 1);
	TestTrue(TEXT("and the crab never left the spot"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Start) < 25.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudClickRefusedTest, "CrabSim.Hud.AGreyedDigButtonStillSwallowsTheClick", TestFlags)
bool FCrabHudClickRefusedTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(0.1f);
	const FVector2D View(1280.f, 720.f);
	const FVector2D Button = CrabHud::DigButtonRect(View.X, View.Y).GetCenter();
	const FVector Behind = Rig.Ground(0.f, 900.f);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, View, &Behind);
	TestFalse(TEXT("no dig without food"), Rig.Crab->IsDigging());
	TestFalse(TEXT("and no walk either"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("but a reason on the message line"), Rig.Crab->GetMessage().Contains(TEXT("Cannot dig: not enough food")));

	// With no ground under the cursor at all (it points at the sky) the button still works.
	Rig.Crab->SetFood(0.8f);
	Rig.Controller->NotifyLeftButtonReleased();
	Rig.Controller->HandleLeftPress(*Rig.Crab, Button, View, nullptr);
	TestTrue(TEXT("the dig starts even with no ground point"), Rig.Crab->IsDigging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudClickElsewhereWalksTest, "CrabSim.Hud.AClickAnywhereElseIsAnOrdinaryClick", TestFlags)
bool FCrabHudClickElsewhereWalksTest::RunTest(const FString& Parameters)
{
	FForageRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Crab->SetFood(0.8f);
	const FVector2D View(1280.f, 720.f);
	const FVector2D Middle(640.f, 360.f);
	const FVector Ground = Rig.Ground(0.f, 500.f);

	Rig.Controller->HandleLeftPress(*Rig.Crab, Middle, View, &Ground);
	TestTrue(TEXT("a click in the middle walks"), Rig.Crab->HasMoveTarget());
	TestFalse(TEXT("and does not dig"), Rig.Crab->IsDigging());

	// While digging, a click on the ground cancels it and walks.
	Rig.Crab->ClearMoveTarget();
	Rig.Crab->StartDig();
	TestTrue(TEXT("digging"), Rig.Crab->IsDigging());
	Rig.Controller->HandleLeftPress(*Rig.Crab, Middle, View, &Ground);
	TestFalse(TEXT("the click cancelled the dig"), Rig.Crab->IsDigging());
	TestTrue(TEXT("and walks"), Rig.Crab->HasMoveTarget());

	// A press with no ground point and nowhere on the HUD does nothing.
	Rig.Crab->ClearMoveTarget();
	Rig.Controller->HandleLeftPress(*Rig.Crab, Middle, View, nullptr);
	TestFalse(TEXT("nothing happens"), Rig.Crab->HasMoveTarget());
	return true;
}
