// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"

#include "CrabColonyMath.h"
#include "CrabColonyView.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

using namespace UE::CrabSim::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyViewBuildsTest, "CrabSim.Colony.ViewBuildsOneHollowPerEdgeAndChamber", TestFlags)
bool FCrabColonyViewBuildsTest::RunTest(const FString& Parameters)
{
	FCrabTestWorld World;
	if (!TestTrue(TEXT("test world is ready"), World.IsReady()))
	{
		return false;
	}

	AActor* Owner = World.SpawnActor<AActor>();
	if (!TestNotNull(TEXT("owner actor"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner);
	Root->RegisterComponent();
	Owner->SetRootComponent(Root);

	UCrabColonyViewComponent* View = NewObject<UCrabColonyViewComponent>(Owner);
	View->SetupAttachment(Root);
	View->RegisterComponent();

	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	View->Build(Plan);

	int32 ChamberCount = 0;
	for (const CrabColony::FNode& Node : Plan.Nodes)
	{
		if (CrabColony::IsChamber(Node.Kind))
		{
			++ChamberCount;
		}
	}
	if (!TestTrue(TEXT("the default plan has at least one edge and one chamber"), Plan.Edges.Num() > 0 && ChamberCount > 0))
	{
		return false;
	}

	TArray<USceneComponent*> Children;
	View->GetChildrenComponents(true, Children);
	// A tunnel per edge and a disc per chamber, plus the backdrop, the surface strip, the dig-face marker and the
	// pooled mound pellets: comfortably more children than edges and chambers put together.
	TestTrue(TEXT("built at least one hollow per edge and per chamber"), Children.Num() >= Plan.Edges.Num() + ChamberCount);

	// PlanToWorld and WorldToPlan round-trip through wherever the component itself sits in the world.
	const FVector2D SampleUV(123.f, -456.f);
	const FVector WorldPoint = View->PlanToWorld(SampleUV);
	const FVector2D RoundTrip = View->WorldToPlan(WorldPoint);
	TestNearlyEqual(TEXT("U round-trips through PlanToWorld/WorldToPlan"), RoundTrip.X, SampleUV.X, 0.01);
	TestNearlyEqual(TEXT("V round-trips through PlanToWorld/WorldToPlan"), RoundTrip.Y, SampleUV.Y, 0.01);

	return true;
}
