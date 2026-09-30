// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"

#include "CrabColony.h"
#include "CrabColonyMath.h"
#include "CrabColonyNpc.h"
#include "CrabGull.h"
#include "CrabPlayerController.h"
#include "CrabSimGameMode.h"

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

using namespace UE::CrabSim::Tests;

namespace
{
	/** A beach and a colony over it, tide frozen low by default so foraging is reachable. */
	struct FColonyRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabColony* Colony = nullptr;

		explicit FColonyRig(bool bFreezeLowTide = true)
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Beach->SetTideClock(LowTide);
					Beach->SetTideFrozen(bFreezeLowTide);
					Colony = World.SpawnActor<ACrabColony>();
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Colony; }

		/** Ticks at a fixed 0.1 s step (the cap for a fast-forward) until Done holds or Seconds of sim time pass. */
		bool RunFast(TFunctionRef<bool()> Done, float Seconds, float Dt = 0.1f)
		{
			const int32 Steps = FMath::CeilToInt(Seconds / Dt);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				World.TickN(1, Dt);
				if (Done())
				{
					return true;
				}
			}
			return Done();
		}
	};

	/** Adds a player crab, a gull and its controller to a colony rig, for the tests that need the gull active. */
	struct FColonyGullRig : public FColonyRig
	{
		ACrabPawn* Crab = nullptr;
		ACrabGull* Gull = nullptr;
		ACrabPlayerController* Controller = nullptr;

		FColonyGullRig()
			: FColonyRig(/*bFreezeLowTide=*/true)
		{
			if (Beach)
			{
				Crab = SpawnCrab(World, *Beach, 0.f, 0.f);
				Gull = World.SpawnActor<ACrabGull>();
				Controller = World.SpawnActor<ACrabPlayerController>();
				if (Crab)
				{
					Settle(World);
				}
			}
		}

		bool IsValid() const { return FColonyRig::IsValid() && Crab && Gull && Controller; }
	};

	/** Sets a console variable for the length of a test and puts it back. */
	struct FCVarScope
	{
		IConsoleVariable* Variable = nullptr;
		int32 Before = 0;

		FCVarScope(const TCHAR* Name, int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Before = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		~FCVarScope()
		{
			if (Variable)
			{
				Variable->Set(Before, ECVF_SetByCode);
			}
		}
	};

	int32 CountColonyNpcs(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACrabColonyNpc> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyGameModeSpawnsPopulationTest, "CrabSim.Colony.TheGameModePutsOneColonyInTheWorldWithItsStartingCrabs", TestFlags)
bool FCrabColonyGameModeSpawnsPopulationTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world"), World.IsReady()))
	{
		return false;
	}
	ACrabSimGameMode* Mode = World.SpawnActor<ACrabSimGameMode>();
	if (!TestNotNull(TEXT("game mode"), Mode) || !TestNotNull(TEXT("it has a colony"), Mode->GetColony()))
	{
		return false;
	}
	int32 ColonyCount = 0;
	for (TActorIterator<ACrabColony> It(World.GetTestWorld()); It; ++It)
	{
		++ColonyCount;
	}
	TestEqual(TEXT("one colony in the world"), ColonyCount, 1);
	TestEqual(TEXT("it starts with StartPopulation crabs"), Mode->GetColony()->GetPopulation(), Mode->GetColony()->GetTuning().StartPopulation);
	TestEqual(TEXT("that many NPC bodies were actually spawned"), CountColonyNpcs(World.GetTestWorld()), Mode->GetColony()->GetTuning().StartPopulation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyDiggersProgressAndPelletsMoundTest, "CrabSim.Colony.DiggersProgressTheNurseryAndTheMoundGainsPellets", TestFlags)
bool FCrabColonyDiggersProgressAndPelletsMoundTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const int32 NurseryEdge = Rig.Colony->GetPlan().PreDugEdges;
	TestEqual(TEXT("that edge leads to the nursery"), Rig.Colony->GetPlan().Nodes[Rig.Colony->GetPlan().Edges[NurseryEdge].B].Kind, CrabColony::ENodeKind::Nursery);
	TestEqual(TEXT("untouched at the start"), CrabColony::DigFraction(Rig.Colony->GetPlan(), Rig.Colony->GetState().Dig, NurseryEdge), 0.f);
	TestEqual(TEXT("no mound yet"), Rig.Colony->GetState().PelletsOnMound, 0);

	Rig.World.TickSeconds(60.f);

	TestTrue(TEXT("diggers made progress on the nursery"), CrabColony::DigFraction(Rig.Colony->GetPlan(), Rig.Colony->GetState().Dig, NurseryEdge) > 0.f);
	TestTrue(TEXT("the mound has pellets on it from the diggers hauling"), Rig.Colony->GetState().PelletsOnMound > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyHatchesOnceStoreAllowsTest, "CrabSim.Colony.TheNurseryOpensAndAHatchFollowsOnceStoreAllows", TestFlags)
bool FCrabColonyHatchesOnceStoreAllowsTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	// A store far past any capacity means ChooseJob never picks Forage, isolating dig-then-hatch behaviour.
	Rig.Colony->Test_SetStore(100.f);
	const int32 StartPopulation = Rig.Colony->GetTuning().StartPopulation;

	const bool bHatched = Rig.RunFast([&] { return Rig.Colony->GetPopulation() > StartPopulation; }, 600.f);
	TestTrue(TEXT("a hatch raised the population once the nursery opened"), bHatched);
	TestTrue(TEXT("the nursery is open"), CrabColony::IsNodeOpen(Rig.Colony->GetPlan(), Rig.Colony->GetState().Dig,
		Rig.Colony->GetPlan().Edges[Rig.Colony->GetPlan().PreDugEdges].B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyForagersReturnWithFoodTest, "CrabSim.Colony.ForagersGoToTheSurfaceAtLowTideAndReturnWithFood", TestFlags)
bool FCrabColonyForagersReturnWithFoodTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const float StartStore = Rig.Colony->GetState().Store;

	bool bSawSurface = false;
	const bool bStoreRose = Rig.RunFast([&]
	{
		bSawSurface = bSawSurface || Rig.Colony->CountOnSurface() > 0;
		return Rig.Colony->GetState().Store > StartStore + 0.01f;
	}, 300.f);

	TestTrue(TEXT("a forager went up onto the surface"), bSawSurface);
	TestTrue(TEXT("and came back with food, raising the store"), bStoreRose);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonySurfaceCrabsFleeADownGullTest, "CrabSim.Colony.SurfaceCrabsComeHomeWhenTheGullIsDown", TestFlags)
bool FCrabColonySurfaceCrabsFleeADownGullTest::RunTest(const FString& Parameters)
{
	FColonyGullRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("a forager reaches the surface"), Rig.RunFast([&] { return Rig.Colony->CountOnSurface() > 0; }, 300.f));

	FCVarScope Force(TEXT("CrabSim.GullForce"), 1);
	TestTrue(TEXT("the gull comes down"), Rig.RunFast([&] { return CrabGull::IsThreat(Rig.Gull->GetPhase()); }, 30.f));
	TestTrue(TEXT("surface crabs come home once it has"), Rig.RunFast([&] { return Rig.Colony->CountOnSurface() == 0; }, 60.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonySetPlayerUndergroundTogglesVisibilityTest, "CrabSim.Colony.SetPlayerUndergroundTogglesTheBeachAndTheCrabsByPlace", TestFlags)
bool FCrabColonySetPlayerUndergroundTogglesVisibilityTest::RunTest(const FString& Parameters)
{
	FColonyRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	const int32 Population = Rig.Colony->GetPopulation();
	if (!TestTrue(TEXT("some crabs to check"), Population > 0))
	{
		return false;
	}

	// Fresh from BeginPlay, with no ticking yet, every crab is still underground (whatever job it was handed).
	Rig.Colony->SetPlayerUnderground(true);
	TestTrue(TEXT("the beach hides while the player is down"), Rig.Beach->IsHidden());
	int32 ShownUnderground = 0;
	for (TActorIterator<ACrabColonyNpc> It(Rig.World.GetTestWorld()); It; ++It)
	{
		ShownUnderground += It->IsHidden() ? 0 : 1;
	}
	TestEqual(TEXT("every crab (all still underground) shows while the player is down"), ShownUnderground, Population);

	Rig.Colony->SetPlayerUnderground(false);
	TestFalse(TEXT("the beach shows once the player is back up"), Rig.Beach->IsHidden());
	int32 ShownOnSurface = 0;
	for (TActorIterator<ACrabColonyNpc> It(Rig.World.GetTestWorld()); It; ++It)
	{
		ShownOnSurface += It->IsHidden() ? 0 : 1;
	}
	TestEqual(TEXT("and every one of those same underground crabs hides again"), ShownOnSurface, 0);
	return true;
}
