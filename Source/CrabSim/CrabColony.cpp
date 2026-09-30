// SPDX-License-Identifier: Apache-2.0
#include "CrabColony.h"
#include "CrabBeach.h"
#include "CrabCarryComponent.h"
#include "CrabColonyNpc.h"
#include "CrabColonyView.h"
#include "CrabGull.h"
#include "CrabMovementMath.h"
#include "CrabPawn.h"
#include "CrabShapeVisual.h"
#include "CrabSim.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

// The colony's own life: a plain state machine per NPC (job, then within a job a small numbered Stage), the
// dig/hatch/forage rules all read from CrabColonyMath.h, and the beach-side dressing (mound, scattered pellets)
// that the beach itself does not know about. See GAME.md, "Colony (building)".

namespace
{
	constexpr float BurrowPelletDiameter = 30.f;
	constexpr float FeedingPelletDiameter = 18.f;
	constexpr float MoundBallDiameter = 40.f;
	constexpr int32 MaxFeedingPellets = 200;
	constexpr int32 MaxVisibleMoundBalls = 40;
	// The same dug sand as UCrabColonyViewComponent's own mound pellets (its PelletColor), whether it is shown in
	// the cutaway or rolled all the way out onto the beach.
	const FLinearColor SandPelletColor = FLinearColor(0.66f, 0.53f, 0.32f);

	const TCHAR* NodeKindName(CrabColony::ENodeKind Kind)
	{
		switch (Kind)
		{
		case CrabColony::ENodeKind::Entrance: return TEXT("Entrance");
		case CrabColony::ENodeKind::Junction: return TEXT("Junction");
		case CrabColony::ENodeKind::Pantry: return TEXT("Pantry");
		case CrabColony::ENodeKind::Nursery: return TEXT("Nursery");
		case CrabColony::ENodeKind::Rest: return TEXT("Rest");
		default: return TEXT("Unknown");
		}
	}
}

static TAutoConsoleVariable<int32> CVarColonyPeek(
	TEXT("CrabSim.ColonyPeek"), 0,
	TEXT("Debug only. 1 sends the player underground and points its camera at a fixed spot looking at the game's"
		 " own colony cutaway, so a screenshot shows the colony at work (CrabSim.ColonyPreview builds a separate,"
		 " throwaway one instead). 0 (the default) leaves the player's state alone."),
	ECVF_Default);

ACrabColony::ACrabColony()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	View = CreateDefaultSubobject<UCrabColonyViewComponent>(TEXT("View"));
	View->SetupAttachment(RootComponent);
	Plan = CrabColony::DefaultBlueprint();
	State.Dig = CrabColony::InitialDigState(Plan);
	State.Store = Tuning.StartStore;
	State.Population = Tuning.StartPopulation;
}

void ACrabColony::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(GetEntranceWorld());
	View->Build(Plan);
	BeachPelletMaterial = CrabShapeVisual::LoadBasicMaterial();

	LastActiveEdge = GetActiveDigEdge();
	View->SetDigFace(LastActiveEdge, LastActiveEdge != INDEX_NONE);

	SetPlayerUnderground(false);

	for (int32 Index = 0; Index < Tuning.StartPopulation; ++Index)
	{
		SpawnCrab(NextCrabSeed());
	}
}

void ACrabColony::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (CVarColonyPeek.GetValueOnGameThread() != 0)
	{
		ApplyColonyPeek();
	}

	for (FCrabState& Crab : Crabs)
	{
		TickOneCrab(Crab, DeltaSeconds);
	}

	if (CrabColony::StepHatch(State, Plan, DeltaSeconds, Tuning))
	{
		SpawnJuvenile();
	}

	const int32 ActiveEdge = GetActiveDigEdge();
	if (ActiveEdge != LastActiveEdge)
	{
		if (LastActiveEdge != INDEX_NONE && Plan.Edges.IsValidIndex(LastActiveEdge))
		{
			const CrabColony::FEdge& Completed = Plan.Edges[LastActiveEdge];
			LogEvent(TEXT("colony_dug"), FString::Printf(TEXT("edge=%d kind=%s"), LastActiveEdge, NodeKindName(Plan.Nodes[Completed.B].Kind)));
		}
		LastActiveEdge = ActiveEdge;
	}
	View->SetDug(ComputeDugFractions());
	View->SetDigFace(ActiveEdge, ActiveEdge != INDEX_NONE);

	TickBeachPelletWashAway(DeltaSeconds);

	StateLogTimer += DeltaSeconds;
	if (StateLogTimer >= 1.f)
	{
		StateLogTimer = 0.f;
		const IConsoleVariable* StateLog = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.StateLog"));
		if (StateLog && StateLog->GetInt() != 0)
		{
			LogColonyState();
		}
	}
}

ACrabBeach* ACrabColony::GetBeach() const
{
	TActorIterator<ACrabBeach> It(GetWorld());
	return It ? *It : nullptr;
}

ACrabGull* ACrabColony::GetGull() const
{
	if (UWorld* World = GetWorld())
	{
		TActorIterator<ACrabGull> It(World);
		return It ? *It : nullptr;
	}
	return nullptr;
}

FVector ACrabColony::GetEntranceWorld() const
{
	const ACrabBeach* Beach = GetBeach();
	if (Beach && Beach->GetBurrows().IsValidIndex(GetEntranceBurrow()))
	{
		return Beach->GetBurrows()[GetEntranceBurrow()].Location;
	}
	return FVector::ZeroVector;
}

FVector ACrabColony::PlanToWorld(FVector2D UV) const
{
	return View->PlanToWorld(UV);
}

FVector2D ACrabColony::WorldToPlan(const FVector& World) const
{
	return View->WorldToPlan(World);
}

int32 ACrabColony::GetActiveDigEdge() const
{
	return CrabColony::ActiveDigEdge(Plan, State.Dig);
}

FVector2D ACrabColony::GetDigFacePos() const
{
	const int32 Edge = GetActiveDigEdge();
	return Edge == INDEX_NONE ? FVector2D::ZeroVector : CrabColony::DigFacePos(Plan, State.Dig, Edge);
}

int32 ACrabColony::FindPantryNear(FVector2D UV) const
{
	int32 Best = INDEX_NONE;
	float BestDistance = 0.f;
	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		if (Plan.Nodes[Index].Kind != CrabColony::ENodeKind::Pantry || !CrabColony::IsNodeOpen(Plan, State.Dig, Index))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector2D::Distance(Plan.Nodes[Index].Pos, UV));
		if (Best == INDEX_NONE || Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}

int32 ACrabColony::CountJob(CrabColony::EJob Job) const
{
	int32 Count = 0;
	for (const FCrabState& Crab : Crabs)
	{
		Count += Crab.Job == Job ? 1 : 0;
	}
	return Count;
}

int32 ACrabColony::CountOnSurface() const
{
	int32 Count = 0;
	for (const FCrabState& Crab : Crabs)
	{
		Count += Crab.Place == CrabColony::EPlace::Surface ? 1 : 0;
	}
	return Count;
}

void ACrabColony::SetPlayerUnderground(bool bUnderground)
{
	const bool bChanged = bUnderground != bPlayerUnderground;
	bPlayerUnderground = bUnderground;
	View->SetVisibility(bUnderground, true);
	if (ACrabBeach* Beach = GetBeach())
	{
		Beach->SetActorHiddenInGame(bUnderground);
	}
	if (ACrabGull* Gull = GetGull())
	{
		Gull->SetSuppressed(bUnderground);
	}
	for (FCrabState& Crab : Crabs)
	{
		ApplyVisibility(Crab);
	}
	ApplyBeachPelletVisibility();
	if (bChanged)
	{
		LogEvent(bUnderground ? TEXT("colony_enter") : TEXT("colony_exit"));
	}
}

float ACrabColony::PlayerDig(float DeltaSeconds)
{
	const int32 Edge = GetActiveDigEdge();
	if (Edge == INDEX_NONE)
	{
		return 0.f;
	}
	return CrabColony::DigAt(State.Dig, Plan, Edge, Tuning.PlayerDigRate * DeltaSeconds);
}

float ACrabColony::PlayerEat(float DeltaSeconds, float Room)
{
	const float Eaten = FMath::Min(FMath::Min(Tuning.PlayerEatPerSecond * DeltaSeconds, Room), State.Store);
	State.Store -= Eaten;
	if (Eaten > 0.f)
	{
		LogEvent(TEXT("colony_eat"), FString::Printf(TEXT("amount=%.3f store=%.3f"), Eaten, State.Store));
	}
	return Eaten;
}

void ACrabColony::AddMoundPellet(const TCHAR* Who)
{
	++State.PelletsOnMound;
	View->SetMound(State.PelletsOnMound);
	AddBeachMoundBall();
	LogEvent(TEXT("colony_pellet"), FString::Printf(TEXT("who=%s"), Who));
}

int32 ACrabColony::AddLoosePellet(FVector2D UV)
{
	const int32 Handle = View->AddLoosePellet(UV);
	LoosePelletRecords.Add(FLoosePelletRecord{Handle, UV});
	return Handle;
}

void ACrabColony::RemoveLoosePellet(int32 Handle)
{
	View->RemoveLoosePellet(Handle);
	LoosePelletRecords.RemoveAll([Handle](const FLoosePelletRecord& Rec) { return Rec.Handle == Handle; });
}

int32 ACrabColony::FindLoosePelletNear(FVector2D UV, float Radius) const
{
	int32 Best = INDEX_NONE;
	float BestDistance = Radius;
	for (const FLoosePelletRecord& Rec : LoosePelletRecords)
	{
		const float Distance = static_cast<float>(FVector2D::Distance(Rec.UV, UV));
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Rec.Handle;
		}
	}
	return Best;
}

void ACrabColony::PlayerDanced(FVector2D UV)
{
	for (FCrabState& Crab : Crabs)
	{
		if (Crab.Place == CrabColony::EPlace::Underground && FVector2D::Distance(Crab.PlanPos, UV) <= 300.f)
		{
			Crab.bDancing = true;
			Crab.DanceTimer = 4.f;
			if (Crab.Body)
			{
				Crab.Body->SetAnim(ECrabAnim::Dance);
			}
		}
	}
}

void ACrabColony::ResetForNewRound()
{
	for (FCrabState& Crab : Crabs)
	{
		if (Crab.Body)
		{
			Crab.Body->Destroy();
		}
	}
	Crabs.Reset();

	for (const FLoosePelletRecord& Rec : LoosePelletRecords)
	{
		View->RemoveLoosePellet(Rec.Handle);
	}
	LoosePelletRecords.Reset();

	for (UStaticMeshComponent* Pellet : BeachRingPellets)
	{
		if (Pellet)
		{
			Pellet->DestroyComponent();
		}
	}
	BeachRingPellets.Reset();
	for (UStaticMeshComponent* Pellet : FeedingPellets)
	{
		if (Pellet)
		{
			Pellet->DestroyComponent();
		}
	}
	FeedingPellets.Reset();
	for (UStaticMeshComponent* Pellet : BeachMoundBalls)
	{
		if (Pellet)
		{
			Pellet->DestroyComponent();
		}
	}
	BeachMoundBalls.Reset();

	State = CrabColony::FColonyState();
	State.Dig = CrabColony::InitialDigState(Plan);
	State.Store = Tuning.StartStore;
	State.Population = Tuning.StartPopulation;
	LastActiveEdge = GetActiveDigEdge();

	View->SetMound(0);
	View->SetDug(ComputeDugFractions());
	View->SetDigFace(LastActiveEdge, LastActiveEdge != INDEX_NONE);

	for (int32 Index = 0; Index < Tuning.StartPopulation; ++Index)
	{
		SpawnCrab(NextCrabSeed());
	}
}

// --- Beach-side pellets: drawn only, the beach itself does not know about them. ---------------------------

void ACrabColony::ScatterBurrowPellets(const FVector& Hole)
{
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));
	const ACrabBeach* Beach = GetBeach();
	const int32 Count = BeachPelletRandom.RandRange(8, 12);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = BeachPelletRandom.FRandRange(0.f, 2.f * PI);
		const float Radius = BeachPelletRandom.FRandRange(90.f, 140.f);
		const float X = Hole.X + FMath::Cos(Angle) * Radius;
		const float Y = Hole.Y + FMath::Sin(Angle) * Radius;
		const float Ground = Beach ? Beach->GetGroundHeight(X, Y) : Hole.Z;
		const FVector World(X, Y, Ground + BurrowPelletDiameter * 0.5f);
		UStaticMeshComponent* Pellet = CrabShapeVisual::AddTintedShape(this, GetRootComponent(), Sphere,
			World - GetActorLocation(), FRotator::ZeroRotator, FVector(BurrowPelletDiameter / 100.f),
			BeachPelletMaterial, SandPelletColor);
		Pellet->SetVisibility(!bPlayerUnderground);
		BeachRingPellets.Add(Pellet);
	}
}

void ACrabColony::DropFeedingPellet(const FVector& Where)
{
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));
	const ACrabBeach* Beach = GetBeach();
	const float Ground = Beach ? Beach->GetGroundHeight(Where.X, Where.Y) : Where.Z;
	const FVector World(Where.X, Where.Y, Ground + FeedingPelletDiameter * 0.5f);

	if (FeedingPellets.Num() >= MaxFeedingPellets)
	{
		if (UStaticMeshComponent* Oldest = FeedingPellets[0])
		{
			Oldest->DestroyComponent();
		}
		FeedingPellets.RemoveAt(0);
	}

	UStaticMeshComponent* Pellet = CrabShapeVisual::AddTintedShape(this, GetRootComponent(), Sphere,
		World - GetActorLocation(), FRotator::ZeroRotator, FVector(FeedingPelletDiameter / 100.f),
		BeachPelletMaterial, SandPelletColor);
	Pellet->SetVisibility(!bPlayerUnderground);
	FeedingPellets.Add(Pellet);
}

void ACrabColony::AddBeachMoundBall()
{
	if (BeachMoundBalls.Num() >= MaxVisibleMoundBalls)
	{
		// The count (State.PelletsOnMound) is already the true total: past the cap this just stops adding shapes.
		return;
	}
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));
	const ACrabBeach* Beach = GetBeach();
	const FVector Mouth = GetEntranceWorld();
	const float Angle = BeachPelletRandom.FRandRange(0.f, 2.f * PI);
	const float Radius = BeachPelletRandom.FRandRange(70.f, 150.f);
	const float X = Mouth.X + FMath::Cos(Angle) * Radius;
	const float Y = Mouth.Y + FMath::Sin(Angle) * Radius;
	const float Ground = Beach ? Beach->GetGroundHeight(X, Y) : Mouth.Z;
	const FVector World(X, Y, Ground + MoundBallDiameter * 0.5f);
	UStaticMeshComponent* Ball = CrabShapeVisual::AddTintedShape(this, GetRootComponent(), Sphere,
		World - GetActorLocation(), FRotator::ZeroRotator, FVector(MoundBallDiameter / 100.f),
		BeachPelletMaterial, SandPelletColor);
	Ball->SetVisibility(!bPlayerUnderground);
	BeachMoundBalls.Add(Ball);
}

void ACrabColony::ApplyBeachPelletVisibility() const
{
	const bool bShow = !bPlayerUnderground;
	for (UStaticMeshComponent* Pellet : BeachRingPellets)
	{
		if (Pellet)
		{
			Pellet->SetVisibility(bShow);
		}
	}
	for (UStaticMeshComponent* Pellet : FeedingPellets)
	{
		if (Pellet)
		{
			Pellet->SetVisibility(bShow);
		}
	}
	for (UStaticMeshComponent* Pellet : BeachMoundBalls)
	{
		if (Pellet)
		{
			Pellet->SetVisibility(bShow);
		}
	}
}

void ACrabColony::TickBeachPelletWashAway(float DeltaSeconds)
{
	BeachPelletWashTimer += DeltaSeconds;
	if (BeachPelletWashTimer < 1.f)
	{
		return;
	}
	BeachPelletWashTimer = 0.f;

	ACrabBeach* Beach = GetBeach();
	if (!Beach)
	{
		return;
	}
	auto Sweep = [Beach](TArray<TObjectPtr<UStaticMeshComponent>>& Pellets)
	{
		for (int32 Index = Pellets.Num() - 1; Index >= 0; --Index)
		{
			UStaticMeshComponent* Pellet = Pellets[Index];
			if (!Pellet)
			{
				Pellets.RemoveAt(Index);
				continue;
			}
			const FVector Loc = Pellet->GetComponentLocation();
			if (Beach->GetWaterDepthAt(Loc.X, Loc.Y) > 5.f)
			{
				Pellet->DestroyComponent();
				Pellets.RemoveAt(Index);
			}
		}
	};
	Sweep(BeachRingPellets);
	Sweep(FeedingPellets);
	Sweep(BeachMoundBalls);
}

// --- Spawning and population --------------------------------------------------------------------------------

uint32 ACrabColony::NextCrabSeed()
{
	CrabSeedCounter += 1;
	// Knuth's multiplicative hash: spreads consecutive small seeds apart so RestSeconds/ChamberSpot do not line up.
	return CrabSeedCounter * 2654435761u;
}

void ACrabColony::SpawnCrab(uint32 Seed)
{
	UWorld* World = GetWorld();
	ACrabColonyNpc* Body = World ? World->SpawnActor<ACrabColonyNpc>() : nullptr;
	if (!Body)
	{
		return;
	}

	FCrabState Crab;
	Crab.Body = Body;
	Crab.Seed = Seed;
	Crab.Place = CrabColony::EPlace::Underground;
	Crab.PlanPos = Plan.Nodes[GetUpperJunctionNode()].Pos;
	Crab.FacingYaw = UndergroundFacingYaw();
	Body->SetPose(PlanToWorld(Crab.PlanPos), Crab.FacingYaw);

	Crabs.Add(Crab);
	ApplyVisibility(Crabs.Last());
	BeginNextJob(Crabs.Last());
}

void ACrabColony::SpawnJuvenile()
{
	UWorld* World = GetWorld();
	ACrabColonyNpc* Body = World ? World->SpawnActor<ACrabColonyNpc>() : nullptr;
	if (!Body)
	{
		return;
	}

	FCrabState Crab;
	Crab.Body = Body;
	Crab.Seed = NextCrabSeed();
	Crab.Place = CrabColony::EPlace::Underground;
	Crab.PlanPos = Plan.Nodes[FindNurseryNode()].Pos;
	Crab.FacingYaw = UndergroundFacingYaw();
	Crab.SizeScale = Tuning.JuvenileScale;
	Body->SetSizeScale(Crab.SizeScale);
	Body->SetPose(PlanToWorld(Crab.PlanPos), Crab.FacingYaw);

	Crabs.Add(Crab);
	ApplyVisibility(Crabs.Last());
	BeginNextJob(Crabs.Last());
	LogEvent(TEXT("colony_hatch"), FString::Printf(TEXT("pop=%d"), State.Population));
}

// --- Per-crab simulation -------------------------------------------------------------------------------------

void ACrabColony::TickOneCrab(FCrabState& Crab, float DeltaSeconds)
{
	UpdateGrowth(Crab, DeltaSeconds);

	if (Crab.bDancing)
	{
		Crab.DanceTimer -= DeltaSeconds;
		if (Crab.DanceTimer > 0.f)
		{
			return;
		}
		Crab.bDancing = false;
	}

	if (Crab.Place == CrabColony::EPlace::Surface && Crab.Job != CrabColony::EJob::Flee)
	{
		const ACrabBeach* Beach = GetBeach();
		const float SecondsToWater = Beach ? Beach->GetSecondsUntilWaterDeeperThan(Crab.SurfaceXY.X, Crab.SurfaceXY.Y, 5.f, 120.f) : BIG_NUMBER;
		if (CrabColony::ShouldFlee(SecondsToWater, IsGullDown(), Tuning))
		{
			BeginFlee(Crab);
		}
	}

	switch (Crab.Job)
	{
	case CrabColony::EJob::Rest: TickRest(Crab, DeltaSeconds); break;
	case CrabColony::EJob::Dig: TickDig(Crab, DeltaSeconds); break;
	case CrabColony::EJob::Haul: TickHaul(Crab, DeltaSeconds); break;
	case CrabColony::EJob::Forage: TickForage(Crab, DeltaSeconds); break;
	case CrabColony::EJob::Flee: TickFlee(Crab, DeltaSeconds); break;
	case CrabColony::EJob::ReturnHome: TickReturnHome(Crab, DeltaSeconds); break;
	}
}

void ACrabColony::UpdateGrowth(FCrabState& Crab, float DeltaSeconds)
{
	if (Crab.SizeScale >= 1.f)
	{
		return;
	}
	Crab.GrowTimer += DeltaSeconds;
	const float T = Tuning.GrowSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(Crab.GrowTimer / Tuning.GrowSeconds, 0.f, 1.f) : 1.f;
	Crab.SizeScale = FMath::Lerp(Tuning.JuvenileScale, 1.f, T);
	if (Crab.Body)
	{
		Crab.Body->SetSizeScale(Crab.SizeScale);
	}
}

void ACrabColony::BeginNextJob(FCrabState& Crab)
{
	switch (CrabColony::ChooseJob(BuildJobContext(), Crab.Seed, Tuning))
	{
	case CrabColony::EJob::Forage:
		BeginForage(Crab);
		break;
	case CrabColony::EJob::Dig:
		BeginDig(Crab);
		break;
	default:
		BeginRest(Crab);
		break;
	}
}

void ACrabColony::BeginRest(FCrabState& Crab)
{
	Crab.Job = CrabColony::EJob::Rest;
	Crab.Stage = 0;
	BeginUndergroundWalk(Crab, FindRestSpotUV(Crab));
}

void ACrabColony::TickRest(FCrabState& Crab, float DeltaSeconds)
{
	if (Crab.Stage == 0)
	{
		AdvanceUnderground(Crab, Tuning.NpcWalkSpeed * DeltaSeconds);
		if (Crab.Body)
		{
			Crab.Body->SetAnim(ECrabAnim::Scuttle);
		}
		if (!HasArrived(Crab))
		{
			return;
		}
		if (Crab.Body)
		{
			Crab.Body->SetAnim(ECrabAnim::Idle);
		}
		Crab.Timer = CrabColony::RestSeconds(Crab.Seed, Tuning);
		Crab.Stage = 1;
		return;
	}

	Crab.Timer -= DeltaSeconds;
	if (Crab.Timer <= 0.f)
	{
		BeginNextJob(Crab);
	}
}

void ACrabColony::BeginDig(FCrabState& Crab)
{
	Crab.Job = CrabColony::EJob::Dig;
	Crab.Stage = 0;
	const int32 Edge = GetActiveDigEdge();
	BeginUndergroundWalk(Crab, Edge == INDEX_NONE ? Crab.PlanPos : CrabColony::DigFacePos(Plan, State.Dig, Edge));
}

void ACrabColony::TickDig(FCrabState& Crab, float DeltaSeconds)
{
	if (Crab.Stage == 0)
	{
		AdvanceUnderground(Crab, Tuning.NpcWalkSpeed * DeltaSeconds);
		if (Crab.Body)
		{
			Crab.Body->SetAnim(ECrabAnim::Scuttle);
		}
		if (!HasArrived(Crab))
		{
			return;
		}
		Crab.Stage = 1;
		return;
	}

	const int32 Edge = GetActiveDigEdge();
	if (Edge == INDEX_NONE)
	{
		// Someone else finished the plan while this one was walking over: nothing left to dig here.
		BeginNextJob(Crab);
		return;
	}
	if (Crab.Body)
	{
		// No Dig clip yet (see ACrabColonyNpc::SetAnim): Scuttle in place stands in for it.
		Crab.Body->SetAnim(ECrabAnim::Scuttle);
	}
	const float Dug = CrabColony::DigAt(State.Dig, Plan, Edge, Tuning.NpcDigRate * DeltaSeconds);
	if (Dug <= 0.f)
	{
		// The edge finished under another digger's hands this tick: rebalance rather than sit at a closed face.
		BeginNextJob(Crab);
		return;
	}
	if (CrabColony::PelletsFor(Dug, State.PelletCarry, Tuning) > 0)
	{
		BeginHaul(Crab);
	}
}

void ACrabColony::BeginHaul(FCrabState& Crab)
{
	Crab.Job = CrabColony::EJob::Haul;
	Crab.Stage = 0;
	Crab.Carrying = ECarry::Pellet;
	if (Crab.Body)
	{
		Crab.Body->SetCarrying(ECarry::Pellet);
	}
	BeginUndergroundWalk(Crab, GetEntranceNodePos());
}

void ACrabColony::TickHaul(FCrabState& Crab, float DeltaSeconds)
{
	AdvanceUnderground(Crab, Tuning.NpcWalkSpeed * DeltaSeconds);
	if (Crab.Body)
	{
		Crab.Body->SetAnim(ECrabAnim::Scuttle);
	}
	if (!HasArrived(Crab))
	{
		return;
	}
	AddMoundPellet(TEXT("npc"));
	Crab.Carrying = ECarry::None;
	if (Crab.Body)
	{
		Crab.Body->SetCarrying(ECarry::None);
	}
	// Re-evaluate rather than assume Dig: the plan may have finished, or the store may now call for foraging.
	BeginNextJob(Crab);
}

void ACrabColony::BeginForage(FCrabState& Crab)
{
	Crab.Job = CrabColony::EJob::Forage;
	Crab.Stage = 0;
	Crab.CarriedAmount = 0.f;
	Crab.ForagePatch = INDEX_NONE;
	BeginUndergroundWalk(Crab, GetEntranceNodePos());
}

void ACrabColony::TickForage(FCrabState& Crab, float DeltaSeconds)
{
	if (Crab.Stage == 0)
	{
		// Underground: walking to the entrance.
		AdvanceUnderground(Crab, Tuning.NpcWalkSpeed * DeltaSeconds);
		if (Crab.Body)
		{
			Crab.Body->SetAnim(ECrabAnim::Scuttle);
		}
		if (!HasArrived(Crab))
		{
			return;
		}
		int32 Patch = INDEX_NONE;
		if (!FindBestForagePatch(Patch))
		{
			// Nowhere worth going: already home, so just rest.
			BeginRest(Crab);
			return;
		}
		Crab.ForagePatch = Patch;
		Crab.Place = CrabColony::EPlace::Surface;
		ApplyVisibility(Crab);
		const FVector Mouth = GetEntranceWorld();
		Crab.SurfaceXY = FVector2D(Mouth.X, Mouth.Y);
		const ACrabBeach* Beach = GetBeach();
		const FVector PatchLoc = Beach ? Beach->GetFoodPatches()[Patch].Location : Mouth;
		BeginSurfaceWalk(Crab, FVector2D(PatchLoc.X, PatchLoc.Y));
		Crab.Stage = 1;
		return;
	}

	if (Crab.Stage == 1)
	{
		// Surface: walking to the patch.
		AdvanceSurface(Crab, Tuning.NpcSurfaceWalkSpeed * DeltaSeconds);
		if (!HasArrivedSurface(Crab))
		{
			return;
		}
		Crab.Timer = 0.f;
		Crab.Stage = 2;
		return;
	}

	if (Crab.Stage == 2)
	{
		// Sifting.
		if (Crab.Body)
		{
			Crab.Body->SetAnim(ECrabAnim::Scuttle);
		}
		if (ACrabBeach* Beach = GetBeach())
		{
			Crab.CarriedAmount += Beach->TakeFood(Crab.ForagePatch, Tuning.ForageTakePerSecond * DeltaSeconds);
		}
		Crab.Timer += DeltaSeconds;
		if (Crab.Timer < Tuning.ForageSiftSeconds)
		{
			return;
		}
		Crab.Carrying = ECarry::Food;
		if (Crab.Body)
		{
			Crab.Body->SetCarrying(ECarry::Food);
		}
		const FVector Mouth = GetEntranceWorld();
		BeginSurfaceWalk(Crab, FVector2D(Mouth.X, Mouth.Y));
		Crab.Stage = 3;
		return;
	}

	if (Crab.Stage == 3)
	{
		// Surface: carrying food back to the mouth.
		AdvanceSurface(Crab, Tuning.NpcSurfaceWalkSpeed * DeltaSeconds);
		if (!HasArrivedSurface(Crab))
		{
			return;
		}
		Crab.Place = CrabColony::EPlace::Underground;
		ApplyVisibility(Crab);
		Crab.PlanPos = GetEntranceNodePos();
		const int32 Pantry = FindPantryNear(Crab.PlanPos);
		if (Pantry == INDEX_NONE)
		{
			// No pantry open (should not happen from the default blueprint): drop it rather than carry it forever.
			Crab.Carrying = ECarry::None;
			if (Crab.Body)
			{
				Crab.Body->SetCarrying(ECarry::None);
			}
			BeginRest(Crab);
			return;
		}
		BeginUndergroundWalk(Crab, ChamberSpot(Pantry, Crab.Seed));
		Crab.Stage = 4;
		return;
	}

	// Stage 4: underground, carrying food to the pantry.
	AdvanceUnderground(Crab, Tuning.NpcWalkSpeed * DeltaSeconds);
	if (Crab.Body)
	{
		Crab.Body->SetAnim(ECrabAnim::Scuttle);
	}
	if (!HasArrived(Crab))
	{
		return;
	}
	DepositFood(Crab);
	BeginNextJob(Crab);
}

void ACrabColony::BeginFlee(FCrabState& Crab)
{
	Crab.Job = CrabColony::EJob::Flee;
	Crab.Stage = 0;
	const FVector Mouth = GetEntranceWorld();
	BeginSurfaceWalk(Crab, FVector2D(Mouth.X, Mouth.Y));
}

void ACrabColony::TickFlee(FCrabState& Crab, float DeltaSeconds)
{
	AdvanceSurface(Crab, Tuning.NpcSurfaceWalkSpeed * DeltaSeconds);
	if (!HasArrivedSurface(Crab))
	{
		return;
	}
	Crab.Place = CrabColony::EPlace::Underground;
	ApplyVisibility(Crab);
	Crab.PlanPos = GetEntranceNodePos();
	BeginReturnHome(Crab);
}

void ACrabColony::BeginReturnHome(FCrabState& Crab)
{
	Crab.Job = CrabColony::EJob::ReturnHome;
	Crab.Stage = 0;
	if (Crab.Carrying == ECarry::Food)
	{
		const int32 Pantry = FindPantryNear(Crab.PlanPos);
		if (Pantry != INDEX_NONE)
		{
			BeginUndergroundWalk(Crab, ChamberSpot(Pantry, Crab.Seed));
			return;
		}
		// No pantry open: drop it rather than carry it forever.
		Crab.Carrying = ECarry::None;
		if (Crab.Body)
		{
			Crab.Body->SetCarrying(ECarry::None);
		}
	}
	BeginRest(Crab);
}

void ACrabColony::TickReturnHome(FCrabState& Crab, float DeltaSeconds)
{
	AdvanceUnderground(Crab, Tuning.NpcWalkSpeed * DeltaSeconds);
	if (Crab.Body)
	{
		Crab.Body->SetAnim(ECrabAnim::Scuttle);
	}
	if (!HasArrived(Crab))
	{
		return;
	}
	DepositFood(Crab);
	BeginRest(Crab);
}

void ACrabColony::DepositFood(FCrabState& Crab)
{
	if (Crab.Carrying != ECarry::Food)
	{
		return;
	}
	const float Cap = CrabColony::StoreCapacity(Plan, State.Dig, Tuning);
	const float Room = FMath::Max(Cap - State.Store, 0.f);
	const float Added = FMath::Min(Crab.CarriedAmount, Room);
	State.Store = FMath::Min(State.Store + Added, Cap);
	if (Added > 0.f)
	{
		LogEvent(TEXT("colony_forage"), FString::Printf(TEXT("patch=%d amount=%.3f"), Crab.ForagePatch, Added));
	}
	Crab.Carrying = ECarry::None;
	Crab.CarriedAmount = 0.f;
	if (Crab.Body)
	{
		Crab.Body->SetCarrying(ECarry::None);
	}
}

// --- Movement primitives -------------------------------------------------------------------------------------

float ACrabColony::UndergroundFacingYaw()
{
	// A yaw is a rotation about world Z only, but the cutaway's "up and down the shaft" axis (V) is world Z too,
	// not a horizontal direction: no yaw can turn a crab's front to point along a tunnel that runs into the
	// screen's own vertical. The best a yaw can do is choose which horizontal direction the front points, and
	// only -Y ("the glass": the camera looks along ViewDirection, +Y, from the -Y side) satisfies "front faces
	// the glass". That leaves the model's own left-right span, where its claws sit, lying along world X, which
	// is exactly the tunnel's left-right reach on screen: as good a "side faces along the tunnel" as a yaw-only
	// pose can give, for a horizontal run or a vertical one alike. Checked in a screenshot (CrabSim.ColonyPeek).
	return UCrabColonyViewComponent::ViewDirection().Rotation().Yaw + 180.f;
}

void ACrabColony::BeginUndergroundWalk(FCrabState& Crab, const FVector2D& ToUV) const
{
	const CrabColony::FSpot From = CrabColony::NearestSpot(Plan, State.Dig, Crab.PlanPos);
	const CrabColony::FSpot To = CrabColony::NearestSpot(Plan, State.Dig, ToUV);
	Crab.WaypointIndex = 0;
	if (!CrabColony::FindPath(Plan, State.Dig, From, To, Crab.Waypoints))
	{
		Crab.Waypoints.Reset();
		Crab.Waypoints.Add(Crab.PlanPos);
	}
}

void ACrabColony::AdvanceUnderground(FCrabState& Crab, float Distance) const
{
	Crab.PlanPos = CrabColony::StepAlong(Crab.Waypoints, Crab.PlanPos, Distance, Crab.WaypointIndex);
	if (Crab.Body)
	{
		Crab.Body->SetPose(PlanToWorld(Crab.PlanPos), UndergroundFacingYaw());
	}
}

bool ACrabColony::HasArrived(const FCrabState& Crab) const
{
	return Crab.WaypointIndex >= Crab.Waypoints.Num();
}

void ACrabColony::BeginSurfaceWalk(FCrabState& Crab, const FVector2D& Target) const
{
	const FVector2D Delta = Target - Crab.SurfaceXY;
	if (!Delta.IsNearlyZero())
	{
		const float MoveYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
		Crab.FacingYaw = CrabMovementMath::SideFacingYaw(Crab.FacingYaw, MoveYaw);
	}
	Crab.SurfaceTarget = Target;
}

void ACrabColony::AdvanceSurface(FCrabState& Crab, float Distance) const
{
	const FVector2D Delta = Crab.SurfaceTarget - Crab.SurfaceXY;
	const float Remaining = static_cast<float>(Delta.Size());
	if (Distance >= Remaining || Remaining <= KINDA_SMALL_NUMBER)
	{
		Crab.SurfaceXY = Crab.SurfaceTarget;
	}
	else
	{
		Crab.SurfaceXY += Delta / Remaining * Distance;
	}
	ApplySurfacePose(Crab);
}

void ACrabColony::ApplySurfacePose(FCrabState& Crab) const
{
	if (!Crab.Body)
	{
		return;
	}
	const ACrabBeach* Beach = GetBeach();
	const float Z = Beach ? Beach->GetGroundHeight(Crab.SurfaceXY.X, Crab.SurfaceXY.Y) : 0.f;
	Crab.Body->SetPose(FVector(Crab.SurfaceXY.X, Crab.SurfaceXY.Y, Z), Crab.FacingYaw);
}

bool ACrabColony::HasArrivedSurface(const FCrabState& Crab) const
{
	return FVector2D::DistSquared(Crab.SurfaceXY, Crab.SurfaceTarget) <= 1.f;
}

// --- Plan queries ----------------------------------------------------------------------------------------

int32 ACrabColony::GetEntranceNode() const
{
	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		if (Plan.Nodes[Index].Kind == CrabColony::ENodeKind::Entrance)
		{
			return Index;
		}
	}
	return 0;
}

FVector2D ACrabColony::GetEntranceNodePos() const
{
	return Plan.Nodes[GetEntranceNode()].Pos;
}

int32 ACrabColony::GetUpperJunctionNode() const
{
	const int32 Entrance = GetEntranceNode();
	for (const CrabColony::FEdge& Edge : Plan.Edges)
	{
		if (Edge.A == Entrance)
		{
			return Edge.B;
		}
		if (Edge.B == Entrance)
		{
			return Edge.A;
		}
	}
	return Entrance;
}

int32 ACrabColony::FindNurseryNode() const
{
	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		if (Plan.Nodes[Index].Kind == CrabColony::ENodeKind::Nursery)
		{
			return Index;
		}
	}
	return GetEntranceNode();
}

int32 ACrabColony::FindNearestOpenNode(const FVector2D& From, TFunctionRef<bool(const CrabColony::FNode&)> Match) const
{
	int32 Best = INDEX_NONE;
	float BestDistance = 0.f;
	for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
	{
		if (!Match(Plan.Nodes[Index]) || !CrabColony::IsNodeOpen(Plan, State.Dig, Index))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector2D::Distance(Plan.Nodes[Index].Pos, From));
		if (Best == INDEX_NONE || Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}

FVector2D ACrabColony::FindRestSpotUV(const FCrabState& Crab) const
{
	int32 Node = FindNearestOpenNode(Crab.PlanPos, [](const CrabColony::FNode& N) { return N.Kind == CrabColony::ENodeKind::Rest; });
	if (Node == INDEX_NONE)
	{
		Node = FindNearestOpenNode(Crab.PlanPos, [](const CrabColony::FNode& N) { return CrabColony::IsChamber(N.Kind); });
	}
	if (Node == INDEX_NONE)
	{
		Node = GetUpperJunctionNode();
	}
	return ChamberSpot(Node, Crab.Seed);
}

FVector2D ACrabColony::ChamberSpot(int32 NodeIndex, uint32 Seed) const
{
	if (!Plan.Nodes.IsValidIndex(NodeIndex))
	{
		return FVector2D::ZeroVector;
	}
	const CrabColony::FNode& Node = Plan.Nodes[NodeIndex];
	if (Node.Radius <= 0.f)
	{
		return Node.Pos;
	}
	FRandomStream Rnd(static_cast<int32>(Seed ^ (static_cast<uint32>(NodeIndex) * 2654435761u)));
	const float Angle = Rnd.FRandRange(0.f, 2.f * PI);
	const float Radius = Rnd.FRandRange(0.2f, 0.7f) * Node.Radius;
	return Node.Pos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
}

bool ACrabColony::FindBestForagePatch(int32& OutPatch) const
{
	OutPatch = INDEX_NONE;
	const ACrabBeach* Beach = GetBeach();
	if (!Beach)
	{
		return false;
	}
	const FVector Mouth = GetEntranceWorld();
	const TArray<FCrabFoodPatch>& Patches = Beach->GetFoodPatches();
	float BestDistance = 0.f;
	for (int32 Index = 0; Index < Patches.Num(); ++Index)
	{
		const FCrabFoodPatch& Patch = Patches[Index];
		if (Patch.Richness <= 0.2f || Patch.bSoaked)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Mouth, Patch.Location));
		const float WalkSeconds = Distance / FMath::Max(Tuning.NpcSurfaceWalkSpeed, 1.f);
		const float Needed = Tuning.ForageSiftSeconds + WalkSeconds + Tuning.FleeWaterSeconds;
		if (Beach->GetSecondsUntilWaterDeeperThan(Patch.Location.X, Patch.Location.Y, 5.f, 120.f) <= Needed)
		{
			continue;
		}
		if (OutPatch == INDEX_NONE || Distance < BestDistance)
		{
			OutPatch = Index;
			BestDistance = Distance;
		}
	}
	return OutPatch != INDEX_NONE;
}

bool ACrabColony::IsGullDown() const
{
	const ACrabGull* Gull = GetGull();
	return Gull && CrabGull::IsThreat(Gull->GetPhase());
}

CrabColony::FJobContext ACrabColony::BuildJobContext() const
{
	CrabColony::FJobContext Context;
	const float Cap = CrabColony::StoreCapacity(Plan, State.Dig, Tuning);
	Context.StoreFraction = Cap > KINDA_SMALL_NUMBER ? State.Store / Cap : 1.f;
	Context.bDigLeft = GetActiveDigEdge() != INDEX_NONE;
	int32 Unused = INDEX_NONE;
	Context.bForageOpen = FindBestForagePatch(Unused) && !IsGullDown();
	Context.Foragers = CountJob(CrabColony::EJob::Forage);
	Context.Diggers = CountJob(CrabColony::EJob::Dig);
	Context.Population = State.Population;
	return Context;
}

TArray<float> ACrabColony::ComputeDugFractions() const
{
	TArray<float> Fractions;
	Fractions.SetNumUninitialized(Plan.Edges.Num());
	for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
	{
		Fractions[Index] = CrabColony::DigFraction(Plan, State.Dig, Index);
	}
	return Fractions;
}

// --- Visibility --------------------------------------------------------------------------------------------

void ACrabColony::ApplyVisibility(FCrabState& Crab) const
{
	if (!Crab.Body)
	{
		return;
	}
	const bool bShow = bPlayerUnderground ? (Crab.Place == CrabColony::EPlace::Underground) : (Crab.Place == CrabColony::EPlace::Surface);
	Crab.Body->SetActorHiddenInGame(!bShow);
}

// --- Log -----------------------------------------------------------------------------------------------------

void ACrabColony::LogEvent(const TCHAR* Name, const FString& Detail) const
{
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_EVENT %s t=%.2f%s%s"), Name, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
		Detail.IsEmpty() ? TEXT("") : TEXT(" "), *Detail);
}

void ACrabColony::LogColonyState() const
{
	const int32 Edge = GetActiveDigEdge();
	const float Face = Edge == INDEX_NONE ? 0.f : CrabColony::DigFraction(Plan, State.Dig, Edge);
	UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_COLONY t=%.2f pop=%d store=%.3f cap=%.3f dig=%d face=%.2f mound=%d surface=%d rest=%d diggers=%d haulers=%d foragers=%d under=%d"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f, State.Population, State.Store, CrabColony::StoreCapacity(Plan, State.Dig, Tuning),
		Edge, Face, State.PelletsOnMound, CountOnSurface(), CountJob(CrabColony::EJob::Rest),
		CountJob(CrabColony::EJob::Dig), CountJob(CrabColony::EJob::Haul), CountJob(CrabColony::EJob::Forage),
		bPlayerUnderground ? 1 : 0);
}

// --- Debug ---------------------------------------------------------------------------------------------------

void ACrabColony::ApplyColonyPeek()
{
	SetPlayerUnderground(true);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACrabPawn* Crab = Cast<ACrabPawn>(It->Get()->GetPawn()))
		{
			Crab->SetColonyCameraPreview(GetEntranceWorld() + FVector(0.f, 0.f, -600.f), UCrabColonyViewComponent::ViewDirection(), 2600.f);
		}
	}
}
