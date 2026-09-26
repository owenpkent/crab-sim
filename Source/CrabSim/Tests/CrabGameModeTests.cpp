// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabHUD.h"
#include "CrabPlayerController.h"
#include "CrabSimGameMode.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/PackageName.h"

using namespace UE::CrabSim::Tests;

namespace
{
	int32 CountBeaches(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACrabBeach> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	AStaticMeshActor* SpawnMeshActor(FCrabTestWorld& World, const TCHAR* MeshPath)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>();
		if (Actor && Mesh)
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		}
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGameModeDefaultsTest, "CrabSim.GameMode.UsesTheCrabItsControllerAndItsHud", TestFlags)
bool FCrabGameModeDefaultsTest::RunTest(const FString& Parameters)
{
	const ACrabSimGameMode* Defaults = GetDefault<ACrabSimGameMode>();
	TestTrue(TEXT("default pawn is the crab"), Defaults->DefaultPawnClass == ACrabPawn::StaticClass());
	TestTrue(TEXT("controller is the pointer controller"), Defaults->PlayerControllerClass == ACrabPlayerController::StaticClass());
	TestTrue(TEXT("HUD is the crab HUD"), Defaults->HUDClass == ACrabHUD::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGameModeBuildsBeachTest, "CrabSim.GameMode.BuildsABeachWhenTheLevelHasNone", TestFlags)
bool FCrabGameModeBuildsBeachTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	TestEqual(TEXT("no beach to start with"), CountBeaches(World.GetTestWorld()), 0);

	ACrabSimGameMode* Mode = World.SpawnActor<ACrabSimGameMode>();
	if (!TestNotNull(TEXT("game mode"), Mode))
	{
		return false;
	}
	TestEqual(TEXT("one beach now"), CountBeaches(World.GetTestWorld()), 1);
	TestNotNull(TEXT("the game mode knows its beach"), Mode->GetBeach());
	TestNotNull(TEXT("and the beach has ground"), Mode->GetBeach() ? Mode->GetBeach()->GetTerrainMesh() : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGameModeKeepsBeachTest, "CrabSim.GameMode.KeepsALevelsOwnBeach", TestFlags)
bool FCrabGameModeKeepsBeachTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	ACrabBeach* Existing = SpawnBeach(World);
	if (!TestNotNull(TEXT("existing beach"), Existing))
	{
		return false;
	}
	Existing->Tide.Period = 60.f;

	ACrabSimGameMode* Mode = World.SpawnActor<ACrabSimGameMode>();
	if (!TestNotNull(TEXT("game mode"), Mode))
	{
		return false;
	}
	TestEqual(TEXT("still exactly one beach"), CountBeaches(World.GetTestWorld()), 1);
	TestTrue(TEXT("and it is the level's own"), Mode->GetBeach() == Existing);
	TestNearlyEqual(TEXT("with its settings untouched"), Existing->Tide.Period, 60.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabGameModeRemovesFloorTest, "CrabSim.GameMode.RemovesTheTemplateFloorAndLeavesOtherMeshes", TestFlags)
bool FCrabGameModeRemovesFloorTest::RunTest(const FString& Parameters)
{
	const TCHAR* FloorPath = TEXT("/Engine/Maps/Templates/SM_Template_Map_Floor.SM_Template_Map_Floor");
	if (!FPackageName::DoesPackageExist(TEXT("/Engine/Maps/Templates/SM_Template_Map_Floor")))
	{
		AddInfo(TEXT("the engine's template floor mesh is not installed, nothing to remove"));
		return true;
	}

	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}
	AStaticMeshActor* Floor = SpawnMeshActor(World, FloorPath);
	AStaticMeshActor* Cube = SpawnMeshActor(World, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("floor"), Floor) || !TestNotNull(TEXT("cube"), Cube))
	{
		return false;
	}

	World.SpawnActor<ACrabSimGameMode>();
	TestTrue(TEXT("the template floor is gone"), !IsValid(Floor));
	TestTrue(TEXT("other static meshes are left alone"), IsValid(Cube));
	return true;
}
