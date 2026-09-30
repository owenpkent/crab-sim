// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabColonyMath.h"

namespace UE::CrabSim::Tests::ColonyMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Fully dig an edge, opening whatever it leads to. */
	inline void OpenEdge(CrabColony::FDigState& Dig, const CrabColony::FBlueprint& Plan, int32 EdgeIndex)
	{
		Dig.Dug[EdgeIndex] = CrabColony::EdgeWork(Plan, EdgeIndex);
	}
}

using namespace UE::CrabSim::Tests::ColonyMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyBlueprintWellFormedTest, "CrabSim.Colony.BlueprintIsWellFormed", TestFlags)
bool FCrabColonyBlueprintWellFormedTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();

	TestEqual(TEXT("node 0 is the entrance"), Plan.Nodes[0].Kind, CrabColony::ENodeKind::Entrance);
	TestEqual(TEXT("the entrance sits right at the surface, V 0"), Plan.Nodes[0].Pos.Y, 0.0);

	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		const CrabColony::FNode& Node = Plan.Nodes[Index];
		TestTrue(*FString::Printf(TEXT("node %d is at or below the surface"), Index), Node.Pos.Y <= 0.0);
		if (Index != 0)
		{
			TestTrue(*FString::Printf(TEXT("node %d is not another entrance"), Index), Node.Kind != CrabColony::ENodeKind::Entrance);
		}
		if (CrabColony::IsChamber(Node.Kind))
		{
			TestTrue(*FString::Printf(TEXT("chamber node %d has a radius"), Index), Node.Radius > 0.f);
		}
	}

	// Dig the plan in order, one edge at a time, and check each edge's A end is already open by the time plan
	// order reaches it (the pre-dug edges count as already dug from the start).
	CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);
	for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
	{
		if (!TestTrue(*FString::Printf(TEXT("edge %d's A end is open when plan order reaches it"), Index),
			CrabColony::IsNodeOpen(Plan, Dig, Plan.Edges[Index].A)))
		{
			return false;
		}
		OpenEdge(Dig, Plan, Index);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyInitialDigOpensPreDugEdgesTest, "CrabSim.Colony.InitialDigStateOpensExactlyThePreDugEdges", TestFlags)
bool FCrabColonyInitialDigOpensPreDugEdgesTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	const CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);
	for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
	{
		const bool bExpectOpen = Index < Plan.PreDugEdges;
		TestEqual(*FString::Printf(TEXT("edge %d open exactly when pre-dug"), Index),
			CrabColony::IsEdgeOpen(Plan, Dig, Index), bExpectOpen);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyActiveDigEdgeWalksThePlanTest, "CrabSim.Colony.ActiveDigEdgeWalksThePlanInOrder", TestFlags)
bool FCrabColonyActiveDigEdgeWalksThePlanTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);
	TestEqual(TEXT("the first undug edge is the one right after the pre-dug run"), CrabColony::ActiveDigEdge(Plan, Dig), Plan.PreDugEdges);

	for (int32 Index = Plan.PreDugEdges; Index < Plan.Edges.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("active edge is %d"), Index), CrabColony::ActiveDigEdge(Plan, Dig), Index);
		OpenEdge(Dig, Plan, Index);
	}
	TestEqual(TEXT("nothing left once every edge is open"), CrabColony::ActiveDigEdge(Plan, Dig), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyDigFacePosMovesThenHoldsAtBTest, "CrabSim.Colony.DigFacePosMovesFromAToBThenStaysThereWhileTheChamberHollows", TestFlags)
bool FCrabColonyDigFacePosMovesThenHoldsAtBTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);
	const int32 EdgeIndex = Plan.PreDugEdges; // Lower -> Nursery, a chamber.
	const CrabColony::FEdge& Edge = Plan.Edges[EdgeIndex];
	const FVector2D A = Plan.Nodes[Edge.A].Pos;
	const FVector2D B = Plan.Nodes[Edge.B].Pos;
	const float Length = static_cast<float>(FVector2D::Distance(A, B));
	const float Work = CrabColony::EdgeWork(Plan, EdgeIndex);
	TestTrue(TEXT("this edge ends in a chamber, so its work is more than its length"), Work > Length);

	Dig.Dug[EdgeIndex] = 0.f;
	TestTrue(TEXT("undug, the face sits at A"), CrabColony::DigFacePos(Plan, Dig, EdgeIndex).Equals(A, 1e-2));

	Dig.Dug[EdgeIndex] = Length * 0.5f;
	TestTrue(TEXT("halfway down the corridor, the face is halfway from A to B"),
		CrabColony::DigFacePos(Plan, Dig, EdgeIndex).Equals(FMath::Lerp(A, B, 0.5), 1e-2));

	Dig.Dug[EdgeIndex] = Length;
	TestTrue(TEXT("corridor through: the face is at B"), CrabColony::DigFacePos(Plan, Dig, EdgeIndex).Equals(B, 1e-2));

	Dig.Dug[EdgeIndex] = Length + (Work - Length) * 0.5f; // partway through hollowing the chamber
	TestTrue(TEXT("hollowing the chamber: the face stays at B"), CrabColony::DigFacePos(Plan, Dig, EdgeIndex).Equals(B, 1e-2));

	Dig.Dug[EdgeIndex] = Work;
	TestTrue(TEXT("fully open: still at B"), CrabColony::DigFacePos(Plan, Dig, EdgeIndex).Equals(B, 1e-2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyNearestSpotStaysOnTheOpenNetworkTest, "CrabSim.Colony.NearestSpotStaysOnTheOpenNetwork", TestFlags)
bool FCrabColonyNearestSpotStaysOnTheOpenNetworkTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();

	// Never lands beyond a dig face: half-dig the Lower -> Nursery edge, then click on the (not yet open) Nursery itself.
	{
		CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);
		const int32 EdgeIndex = Plan.PreDugEdges;
		const float Length = static_cast<float>(FVector2D::Distance(Plan.Nodes[Plan.Edges[EdgeIndex].A].Pos, Plan.Nodes[Plan.Edges[EdgeIndex].B].Pos));
		Dig.Dug[EdgeIndex] = Length * 0.5f;
		const FVector2D NurseryPos = Plan.Nodes[Plan.Edges[EdgeIndex].B].Pos;
		const CrabColony::FSpot Spot = CrabColony::NearestSpot(Plan, Dig, NurseryPos);
		TestEqual(TEXT("clicking past the face lands on the edge being dug"), Spot.Edge, EdgeIndex);
		TestTrue(TEXT("never past the dig face"), Spot.Along <= Dig.Dug[EdgeIndex] + 1e-2f);
		TestTrue(TEXT("right at the face"), Spot.Pos.Equals(CrabColony::DigFacePos(Plan, Dig, EdgeIndex), 1e-2));
	}

	// Inside an open chamber's circle, the spot is the point itself.
	{
		const CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan); // the first Pantry is pre-dug, so already open.
		int32 PantryNode = INDEX_NONE;
		for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
		{
			if (Plan.Nodes[Index].Kind == CrabColony::ENodeKind::Pantry)
			{
				PantryNode = Index;
				break;
			}
		}
		const FVector2D Click = Plan.Nodes[PantryNode].Pos + FVector2D(30.0, 20.0);
		const CrabColony::FSpot Spot = CrabColony::NearestSpot(Plan, Dig, Click);
		TestTrue(TEXT("inside the chamber, the spot is the click itself"), Spot.Pos.Equals(Click, 1e-3));
	}

	// Off to the side of a plain tunnel: snaps to the nearest point on it.
	{
		const CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);
		const FVector2D EntranceA = Plan.Nodes[Plan.Edges[0].A].Pos; // Entrance
		const FVector2D UpperB = Plan.Nodes[Plan.Edges[0].B].Pos; // Upper, straight down from the entrance
		const FVector2D Click = FMath::Lerp(EntranceA, UpperB, 0.5) + FVector2D(50.0, 0.0);
		const CrabColony::FSpot Spot = CrabColony::NearestSpot(Plan, Dig, Click);
		TestEqual(TEXT("snaps onto the entrance shaft"), Spot.Edge, 0);
		TestTrue(TEXT("at the perpendicular foot, not the click itself"), Spot.Pos.Equals(FMath::Lerp(EntranceA, UpperB, 0.5), 1e-2));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyFindPathThroughUpperJunctionTest, "CrabSim.Colony.FindPathBetweenPantryAndRestGoesThroughTheUpperJunction", TestFlags)
bool FCrabColonyFindPathThroughUpperJunctionTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	const CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);

	int32 PantryNode = INDEX_NONE;
	int32 RestNode = INDEX_NONE;
	int32 UpperNode = INDEX_NONE;
	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		if (Plan.Nodes[Index].Kind == CrabColony::ENodeKind::Pantry && PantryNode == INDEX_NONE)
		{
			PantryNode = Index;
		}
		else if (Plan.Nodes[Index].Kind == CrabColony::ENodeKind::Rest && RestNode == INDEX_NONE)
		{
			RestNode = Index;
		}
	}
	UpperNode = Plan.Edges[0].B; // Entrance -> Upper

	const CrabColony::FSpot From = CrabColony::SpotAtNode(Plan, PantryNode);
	const CrabColony::FSpot To = CrabColony::SpotAtNode(Plan, RestNode);

	TArray<FVector2D> Waypoints;
	if (!TestTrue(TEXT("a path exists"), CrabColony::FindPath(Plan, Dig, From, To, Waypoints)))
	{
		return false;
	}

	bool bThroughUpper = false;
	for (const FVector2D& Point : Waypoints)
	{
		if (Point.Equals(Plan.Nodes[UpperNode].Pos, 1e-2))
		{
			bThroughUpper = true;
		}
	}
	TestTrue(TEXT("the path passes through the upper junction"), bThroughUpper);

	const float Expected = static_cast<float>(FVector2D::Distance(Plan.Nodes[PantryNode].Pos, Plan.Nodes[UpperNode].Pos)
		+ FVector2D::Distance(Plan.Nodes[UpperNode].Pos, Plan.Nodes[RestNode].Pos));
	TestNearlyEqual(TEXT("its length is the two legs added up"), CrabColony::PathLength(Waypoints), Expected, 1e-1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyStepAlongArrivesExactlyTest, "CrabSim.Colony.StepAlongArrivesExactlyAndNeverOvershoots", TestFlags)
bool FCrabColonyStepAlongArrivesExactlyTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Waypoints = {FVector2D(100.0, 0.0), FVector2D(100.0, 100.0)};

	// One call covering the whole path lands exactly on the last waypoint and reports arrival.
	{
		int32 Waypoint = 0;
		const FVector2D End = CrabColony::StepAlong(Waypoints, FVector2D(0.0, 0.0), 200.f, Waypoint);
		TestTrue(TEXT("lands exactly on the last waypoint"), End.Equals(Waypoints.Last(), 1e-2));
		TestEqual(TEXT("reports arrival"), Waypoint, Waypoints.Num());
	}

	// Asking for more distance than the path has never overshoots past the last waypoint.
	{
		int32 Waypoint = 0;
		const FVector2D End = CrabColony::StepAlong(Waypoints, FVector2D(0.0, 0.0), 500.f, Waypoint);
		TestTrue(TEXT("clamped at the last waypoint"), End.Equals(Waypoints.Last(), 1e-2));
		TestEqual(TEXT("still reports arrival"), Waypoint, Waypoints.Num());
	}

	// A short step covers exactly that much ground and no more.
	{
		int32 Waypoint = 0;
		const FVector2D Mid = CrabColony::StepAlong(Waypoints, FVector2D(0.0, 0.0), 30.f, Waypoint);
		TestNearlyEqual(TEXT("covers exactly the distance asked for"), static_cast<float>(FVector2D::Distance(FVector2D(0.0, 0.0), Mid)), 30.f, 1e-2f);
		TestEqual(TEXT("not at a waypoint yet"), Waypoint, 0);

		// Two more calls that together cover the rest of the path arrive the same place a single call would.
		const FVector2D Mid2 = CrabColony::StepAlong(Waypoints, Mid, 120.f, Waypoint);
		const FVector2D EndOfTwo = CrabColony::StepAlong(Waypoints, Mid2, 200.f, Waypoint);
		TestTrue(TEXT("several short steps add up to the same arrival as one long one"), EndOfTwo.Equals(Waypoints.Last(), 1e-2));
		TestEqual(TEXT("arrival reported here too"), Waypoint, Waypoints.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyHatchPaysAndCapsTest, "CrabSim.Colony.HatchPaysAndCaps", TestFlags)
bool FCrabColonyHatchPaysAndCapsTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	const CrabColony::FTuning Tuning;

	CrabColony::FColonyState State;
	State.Dig = CrabColony::InitialDigState(Plan);
	OpenEdge(State.Dig, Plan, Plan.PreDugEdges); // opens the Nursery
	State.Store = Tuning.HatchCost;
	State.Population = CrabColony::PopulationCap(Plan, State.Dig, Tuning) - 1;

	TestTrue(TEXT("can hatch: nursery open, food enough, room for one more"), CrabColony::CanHatch(Plan, State, Tuning));

	const int32 PopulationBefore = State.Population;
	const float StoreBefore = State.Store;
	const int32 Steps = 5;
	bool bHatched = false;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		bHatched = CrabColony::StepHatch(State, Plan, Tuning.HatchSeconds / Steps, Tuning) || bHatched;
	}
	TestTrue(TEXT("hatches once HatchSeconds have passed"), bHatched);
	TestEqual(TEXT("population goes up by one"), State.Population, PopulationBefore + 1);
	TestNearlyEqual(TEXT("HatchCost is paid"), State.Store, StoreBefore - Tuning.HatchCost, 1e-4f);

	// Now full: cannot hatch again, and progress does not bank.
	TestFalse(TEXT("at the cap now"), CrabColony::CanHatch(Plan, State, Tuning));
	State.Store = Tuning.HatchCost; // food is not the blocker here
	CrabColony::StepHatch(State, Plan, 1.f, Tuning);
	TestNearlyEqual(TEXT("progress does not bank while capped"), State.HatchProgress, 0.f, 1e-4f);

	// An empty store also blocks it and drops any progress made.
	CrabColony::FColonyState Poor;
	Poor.Dig = State.Dig;
	Poor.Population = 0;
	Poor.Store = 0.f;
	Poor.HatchProgress = Tuning.HatchSeconds * 0.5f;
	TestFalse(TEXT("no food, cannot hatch"), CrabColony::CanHatch(Plan, Poor, Tuning));
	CrabColony::StepHatch(Poor, Plan, 1.f, Tuning);
	TestNearlyEqual(TEXT("progress resets rather than banking"), Poor.HatchProgress, 0.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyChooseJobPrioritiesTest, "CrabSim.Colony.ChooseJobPrioritizesForageThenDigThenRest", TestFlags)
bool FCrabColonyChooseJobPrioritiesTest::RunTest(const FString& Parameters)
{
	const CrabColony::FTuning Tuning;

	CrabColony::FJobContext Context;
	Context.StoreFraction = 0.f;
	Context.bDigLeft = true;
	Context.bForageOpen = true;
	Context.Foragers = 0;
	Context.Diggers = 0;
	Context.Population = 9; // MaxForagers = 3

	TestEqual(TEXT("low store, surface open, foragers under the cap: forage"), CrabColony::ChooseJob(Context, 1, Tuning), CrabColony::EJob::Forage);

	CrabColony::FJobContext AtForagerCap = Context;
	AtForagerCap.Foragers = 3;
	TestEqual(TEXT("foragers at the cap: falls through to dig"), CrabColony::ChooseJob(AtForagerCap, 1, Tuning), CrabColony::EJob::Dig);

	CrabColony::FJobContext StoreFull = Context;
	StoreFull.StoreFraction = Tuning.ForageBelow; // not below it any more
	TestEqual(TEXT("store not low: skips forage, digs"), CrabColony::ChooseJob(StoreFull, 1, Tuning), CrabColony::EJob::Dig);

	CrabColony::FJobContext NoSurface = Context;
	NoSurface.bForageOpen = false;
	TestEqual(TEXT("surface not open: skips forage, digs"), CrabColony::ChooseJob(NoSurface, 1, Tuning), CrabColony::EJob::Dig);

	CrabColony::FJobContext AtDiggerCap = Context;
	AtDiggerCap.bForageOpen = false;
	AtDiggerCap.Diggers = Tuning.MaxDiggers;
	TestEqual(TEXT("nothing to forage, diggers at the cap: rest"), CrabColony::ChooseJob(AtDiggerCap, 1, Tuning), CrabColony::EJob::Rest);

	CrabColony::FJobContext NothingToDo = Context;
	NothingToDo.bForageOpen = false;
	NothingToDo.bDigLeft = false;
	TestEqual(TEXT("nothing left to forage or dig: rest"), CrabColony::ChooseJob(NothingToDo, 1, Tuning), CrabColony::EJob::Rest);

	// A small population still guarantees at least one forager slot (max(1, Population/3)).
	CrabColony::FJobContext SmallColony = Context;
	SmallColony.Population = 2;
	TestEqual(TEXT("small colony, no forager yet: forage is still allowed"), CrabColony::ChooseJob(SmallColony, 1, Tuning), CrabColony::EJob::Forage);
	SmallColony.Foragers = 1;
	TestEqual(TEXT("small colony, one forager already out: the single slot is taken"), CrabColony::ChooseJob(SmallColony, 1, Tuning), CrabColony::EJob::Dig);

	// RestSeconds is deterministic given the seed and always falls within [RestMin, RestMax].
	const float RestA = CrabColony::RestSeconds(42, Tuning);
	const float RestB = CrabColony::RestSeconds(42, Tuning);
	TestEqual(TEXT("same seed, same rest length"), RestA, RestB);
	TestTrue(TEXT("within [RestMin, RestMax]"), RestA >= Tuning.RestMin && RestA <= Tuning.RestMax);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyShouldFleeThresholdsTest, "CrabSim.Colony.ShouldFleeThresholds", TestFlags)
bool FCrabColonyShouldFleeThresholdsTest::RunTest(const FString& Parameters)
{
	const CrabColony::FTuning Tuning;
	TestFalse(TEXT("plenty of time, no gull: stay"), CrabColony::ShouldFlee(Tuning.FleeWaterSeconds + 1.f, false, Tuning));
	TestTrue(TEXT("right at the threshold: flee"), CrabColony::ShouldFlee(Tuning.FleeWaterSeconds, false, Tuning));
	TestTrue(TEXT("water close, no gull: flee"), CrabColony::ShouldFlee(Tuning.FleeWaterSeconds - 5.f, false, Tuning));
	TestTrue(TEXT("plenty of time but a gull is down: flee anyway"), CrabColony::ShouldFlee(Tuning.FleeWaterSeconds + 100.f, true, Tuning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabColonyCapacityGrowsAsChambersOpenTest, "CrabSim.Colony.StoreCapacityAndPopulationCapGrowAsChambersOpen", TestFlags)
bool FCrabColonyCapacityGrowsAsChambersOpenTest::RunTest(const FString& Parameters)
{
	const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
	const CrabColony::FTuning Tuning;
	CrabColony::FDigState Dig = CrabColony::InitialDigState(Plan);

	TestNearlyEqual(TEXT("one pantry open at the start"), CrabColony::StoreCapacity(Plan, Dig, Tuning), Tuning.PantryCapacity, 1e-4f);
	TestEqual(TEXT("one rest chamber open at the start"), CrabColony::PopulationCap(Plan, Dig, Tuning), Tuning.BasePopulation + Tuning.PerRestChamber);

	// Dig through the nursery and the second rest chamber: no new pantry yet, but a second rest chamber.
	OpenEdge(Dig, Plan, Plan.PreDugEdges); // Lower -> Nursery
	OpenEdge(Dig, Plan, Plan.PreDugEdges + 1); // Lower -> Rest2
	TestNearlyEqual(TEXT("still one pantry"), CrabColony::StoreCapacity(Plan, Dig, Tuning), Tuning.PantryCapacity, 1e-4f);
	TestEqual(TEXT("now two rest chambers"), CrabColony::PopulationCap(Plan, Dig, Tuning), Tuning.BasePopulation + Tuning.PerRestChamber * 2);

	// Dig the rest of the plan: a third rest chamber and a second pantry.
	OpenEdge(Dig, Plan, Plan.PreDugEdges + 2); // Lower -> Deep
	OpenEdge(Dig, Plan, Plan.PreDugEdges + 3); // Deep -> Rest3
	OpenEdge(Dig, Plan, Plan.PreDugEdges + 4); // Deep -> Pantry2
	TestNearlyEqual(TEXT("two pantries now"), CrabColony::StoreCapacity(Plan, Dig, Tuning), Tuning.PantryCapacity * 2, 1e-4f);
	TestEqual(TEXT("three rest chambers now"), CrabColony::PopulationCap(Plan, Dig, Tuning), Tuning.BasePopulation + Tuning.PerRestChamber * 3);
	return true;
}
