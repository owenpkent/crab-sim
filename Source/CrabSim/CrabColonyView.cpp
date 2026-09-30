// SPDX-License-Identifier: Apache-2.0
#include "CrabColonyView.h"
#include "CrabBeach.h"
#include "CrabCarryComponent.h"
#include "CrabColonyNpc.h"
#include "CrabGull.h"
#include "CrabPawn.h"
#include "CrabShapeVisual.h"
#include "CrabSim.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// Depths (local Y, toward +Y being away from the camera per ViewDirection): the backdrop sits behind the
	// plan's own nominal Y=0, everything else is pulled a little toward the camera from it so nothing z-fights
	// the slab behind it. Nearer to the camera (more negative) reads as more "in front".
	constexpr float BackdropFrontY = 0.f;
	constexpr float BackdropThickness = 200.f;
	constexpr float SurfaceStripY = -12.f;
	constexpr float TunnelY = -15.f;
	constexpr float ChamberY = -18.f;
	constexpr float DigFaceY = -22.f;
	constexpr float PelletY = -22.f;

	constexpr float BackdropWidth = 3200.f;
	constexpr float BackdropHeight = 2200.f;
	// Centred low: the plan runs from the surface (V=0) down past -1300, and this leaves headroom above the
	// surface strip for the sky band.
	constexpr float BackdropCenterV = -800.f;

	// The surface strip: a damp-sand lip straddling V=0 (so it reads whether a chamber is right at the mouth or
	// a little below it), then a hint of sky above. The beach itself is hidden while the player is down, so this
	// is the only "up" the cutaway shows (GAME.md, "Colony (building)").
	constexpr float DampSandBottomV = -20.f;
	constexpr float DampSandTopV = 90.f;
	constexpr float SkyTopV = 280.f;
	constexpr float StripThickness = 40.f;

	constexpr float DigFaceDiameter = 85.f;
	constexpr float MoundPelletDiameter = 40.f;
	constexpr int32 MaxMoundPellets = 20;
	// A chamber not yet reached shows nothing; the moment digging reaches it, its disc shows at this hint of a
	// hollow rather than popping straight to full size.
	constexpr float MinChamberVisualRadius = 18.f;
	constexpr float ChamberThickness = 24.f;

	const FLinearColor SandColor = FLinearColor(0.72f, 0.6f, 0.4f);
	const FLinearColor DampSandColor = FLinearColor(0.5f, 0.38f, 0.24f);
	const FLinearColor SkyColor = FLinearColor(0.55f, 0.75f, 0.92f);
	const FLinearColor HollowColor = FLinearColor(0.03f, 0.02f, 0.015f);
	// Noticeably lighter than SandColor (the pale backdrop it sits on) as well as the dark hollow behind it: freshly
	// turned sand, not just a slightly paler patch of the same sand.
	const FLinearColor DigFaceColor = FLinearColor(0.97f, 0.9f, 0.68f);
	const FLinearColor PelletColor = FLinearColor(0.66f, 0.53f, 0.32f);
}

UCrabColonyViewComponent::UCrabColonyViewComponent()
{
	BasicMaterial = CrabShapeVisual::LoadBasicMaterial();
}

UStaticMeshComponent* UCrabColonyViewComponent::AddTintedShape(UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color)
{
	return CrabShapeVisual::AddTintedShape(this, this, Mesh, Loc, Rot, Scale, BasicMaterial, Color);
}

void UCrabColonyViewComponent::Reset()
{
	for (FEdgeVisual& Visual : EdgeVisuals)
	{
		if (Visual.Tunnel)
		{
			Visual.Tunnel->DestroyComponent();
		}
	}
	EdgeVisuals.Reset();

	for (FChamberVisual& Chamber : ChamberVisuals)
	{
		if (Chamber.Disc)
		{
			Chamber.Disc->DestroyComponent();
		}
	}
	ChamberVisuals.Reset();
	ChamberOfNode.Reset();

	if (DigFaceMarker)
	{
		DigFaceMarker->DestroyComponent();
		DigFaceMarker = nullptr;
	}
	for (UStaticMeshComponent* Pellet : MoundPellets)
	{
		if (Pellet)
		{
			Pellet->DestroyComponent();
		}
	}
	MoundPellets.Reset();
	ShownMoundPellets = 0;

	for (UStaticMeshComponent* Pellet : LoosePellets)
	{
		if (Pellet)
		{
			Pellet->DestroyComponent();
		}
	}
	LoosePellets.Reset();

	if (Backdrop) { Backdrop->DestroyComponent(); Backdrop = nullptr; }
	if (DampSand) { DampSand->DestroyComponent(); DampSand = nullptr; }
	if (SkyBand) { SkyBand->DestroyComponent(); SkyBand = nullptr; }
}

void UCrabColonyViewComponent::BuildBackdrop()
{
	UStaticMesh* Cube = CrabShapeVisual::LoadEngineShape(TEXT("Cube"));
	Backdrop = AddTintedShape(Cube, FVector(0.f, BackdropFrontY + BackdropThickness * 0.5f, BackdropCenterV), FRotator::ZeroRotator,
		FVector(BackdropWidth / 100.f, BackdropThickness / 100.f, BackdropHeight / 100.f), SandColor);
}

void UCrabColonyViewComponent::BuildSurfaceStrip()
{
	UStaticMesh* Cube = CrabShapeVisual::LoadEngineShape(TEXT("Cube"));

	const float DampCenterV = (DampSandTopV + DampSandBottomV) * 0.5f;
	DampSand = AddTintedShape(Cube, FVector(0.f, SurfaceStripY, DampCenterV), FRotator::ZeroRotator,
		FVector(BackdropWidth / 100.f, StripThickness / 100.f, (DampSandTopV - DampSandBottomV) / 100.f), DampSandColor);

	const float SkyCenterV = (SkyTopV + DampSandTopV) * 0.5f;
	SkyBand = AddTintedShape(Cube, FVector(0.f, SurfaceStripY, SkyCenterV), FRotator::ZeroRotator,
		FVector(BackdropWidth / 100.f, StripThickness / 100.f, (SkyTopV - DampSandTopV) / 100.f), SkyColor);
}

void UCrabColonyViewComponent::BuildMoundPellets()
{
	FVector2D EntranceUV = FVector2D::ZeroVector;
	for (const CrabColony::FNode& Node : Plan.Nodes)
	{
		if (Node.Kind == CrabColony::ENodeKind::Entrance)
		{
			EntranceUV = Node.Pos;
			break;
		}
	}

	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));
	FRandomStream Rng(917);
	for (int32 Index = 0; Index < MaxMoundPellets; ++Index)
	{
		// Piled to one side of the mouth, low over the surface: a scatter rather than a grid, so it reads as a
		// heap that grows, not a row of balls.
		const float Angle = Rng.FRandRange(0.f, PI);
		const float Radius = 40.f + Rng.FRand() * 130.f;
		const FVector2D Offset(120.f + FMath::Cos(Angle) * Radius, FMath::Abs(FMath::Sin(Angle)) * Radius * 0.5f);
		const FVector2D UV = EntranceUV + Offset;
		UStaticMeshComponent* Pellet = AddTintedShape(Sphere, FVector(UV.X, PelletY, UV.Y), FRotator::ZeroRotator,
			FVector(MoundPelletDiameter / 100.f), PelletColor);
		Pellet->SetVisibility(false);
		MoundPellets.Add(Pellet);
	}
}

void UCrabColonyViewComponent::Build(const CrabColony::FBlueprint& InPlan)
{
	Reset();
	Plan = InPlan;

	BuildBackdrop();
	BuildSurfaceStrip();

	UStaticMesh* Cylinder = CrabShapeVisual::LoadEngineShape(TEXT("Cylinder"));
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));

	ChamberOfNode.Init(INDEX_NONE, Plan.Nodes.Num());
	EdgeVisuals.SetNum(Plan.Edges.Num());

	for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
	{
		const CrabColony::FEdge& Edge = Plan.Edges[Index];
		const CrabColony::FNode& NodeA = Plan.Nodes[Edge.A];
		const CrabColony::FNode& NodeB = Plan.Nodes[Edge.B];

		FEdgeVisual& EdgeVisual = EdgeVisuals[Index];
		EdgeVisual.LocalA = FVector(NodeA.Pos.X, TunnelY, NodeA.Pos.Y);
		const FVector LocalB = FVector(NodeB.Pos.X, TunnelY, NodeB.Pos.Y);
		const FVector Delta = LocalB - EdgeVisual.LocalA;
		EdgeVisual.GeometricLength = Delta.Size();
		EdgeVisual.Direction = EdgeVisual.GeometricLength > KINDA_SMALL_NUMBER ? Delta / EdgeVisual.GeometricLength : FVector::ForwardVector;
		EdgeVisual.CurrentEnd = EdgeVisual.LocalA;

		const float Work = CrabColony::EdgeWork(Plan, Index);
		EdgeVisual.TunnelCompleteFraction = Work > KINDA_SMALL_NUMBER ? FMath::Clamp(EdgeVisual.GeometricLength / Work, 0.f, 1.f) : 1.f;

		// Laid down along A->B the way ACrabBeach::AddBurrowVisual tilts a shape onto the ground: MakeFromZ turns
		// the mesh's own Z axis (its length, before scaling) to face the direction we want instead.
		const FRotator TunnelRot = FRotationMatrix::MakeFromZ(EdgeVisual.Direction).Rotator();
		EdgeVisual.Tunnel = AddTintedShape(Cylinder, EdgeVisual.LocalA, TunnelRot,
			FVector(Edge.Width / 100.f, Edge.Width / 100.f, 0.01f), HollowColor);

		if (CrabColony::IsChamber(NodeB.Kind))
		{
			FChamberVisual Chamber;
			Chamber.FullRadius = NodeB.Radius;
			// Facing the camera: MakeFromZ turns the cylinder's cap normal (its own Z) to face ViewDirection instead.
			const FRotator DiscRot = FRotationMatrix::MakeFromZ(ViewDirection()).Rotator();
			Chamber.Disc = AddTintedShape(Cylinder, FVector(NodeB.Pos.X, ChamberY, NodeB.Pos.Y), DiscRot,
				FVector(MinChamberVisualRadius * 2.f / 100.f, MinChamberVisualRadius * 2.f / 100.f, ChamberThickness / 100.f), HollowColor);
			Chamber.Disc->SetVisibility(false);
			ChamberVisuals.Add(Chamber);
			EdgeVisual.ChamberNode = Edge.B;
			ChamberOfNode[Edge.B] = ChamberVisuals.Num() - 1;
		}
	}

	DigFaceMarker = AddTintedShape(Sphere, FVector::ZeroVector, FRotator::ZeroRotator, FVector(DigFaceDiameter / 100.f), DigFaceColor);
	DigFaceMarker->SetVisibility(false);

	BuildMoundPellets();

	// A bare Build (a test, or before the caller's first SetDug) still reads as a finished colony rather than an
	// empty one.
	TArray<float> FullyDug;
	FullyDug.Init(1.f, Plan.Edges.Num());
	SetDug(FullyDug);
}

void UCrabColonyViewComponent::SetDug(const TArray<float>& FractionPerEdge)
{
	for (int32 Index = 0; Index < EdgeVisuals.Num() && Index < FractionPerEdge.Num(); ++Index)
	{
		FEdgeVisual& EdgeVisual = EdgeVisuals[Index];
		if (!EdgeVisual.Tunnel)
		{
			continue;
		}

		const float Fraction = FMath::Clamp(FractionPerEdge[Index], 0.f, 1.f);
		const float TunnelT = EdgeVisual.TunnelCompleteFraction > KINDA_SMALL_NUMBER
			? FMath::Clamp(Fraction / EdgeVisual.TunnelCompleteFraction, 0.f, 1.f)
			: (Fraction >= 1.f ? 1.f : 0.f);
		const float CurrentLength = EdgeVisual.GeometricLength * TunnelT;

		EdgeVisual.CurrentEnd = EdgeVisual.LocalA + EdgeVisual.Direction * CurrentLength;
		EdgeVisual.Tunnel->SetVisibility(CurrentLength > KINDA_SMALL_NUMBER);
		// Scaled from the A end: the centre (and so the far end) advances with it, the A end never moves.
		EdgeVisual.Tunnel->SetRelativeLocation(EdgeVisual.LocalA + EdgeVisual.Direction * (CurrentLength * 0.5f));
		const FVector TunnelScale = EdgeVisual.Tunnel->GetRelativeScale3D();
		EdgeVisual.Tunnel->SetRelativeScale3D(FVector(TunnelScale.X, TunnelScale.Y, FMath::Max(CurrentLength / 100.f, KINDA_SMALL_NUMBER)));

		if (EdgeVisual.ChamberNode == INDEX_NONE)
		{
			continue;
		}
		FChamberVisual& Chamber = ChamberVisuals[ChamberOfNode[EdgeVisual.ChamberNode]];
		if (!Chamber.Disc)
		{
			continue;
		}
		if (Fraction <= EdgeVisual.TunnelCompleteFraction)
		{
			Chamber.Disc->SetVisibility(false);
			continue;
		}
		const float ChamberSpan = FMath::Max(1.f - EdgeVisual.TunnelCompleteFraction, KINDA_SMALL_NUMBER);
		const float ChamberT = FMath::Clamp((Fraction - EdgeVisual.TunnelCompleteFraction) / ChamberSpan, 0.f, 1.f);
		const float Radius = FMath::Lerp(MinChamberVisualRadius, Chamber.FullRadius, ChamberT);
		Chamber.Disc->SetVisibility(true);
		const FVector DiscScale = Chamber.Disc->GetRelativeScale3D();
		const float DiscDiameterScale = Radius * 2.f / 100.f;
		Chamber.Disc->SetRelativeScale3D(FVector(DiscDiameterScale, DiscDiameterScale, DiscScale.Z));
	}
}

void UCrabColonyViewComponent::SetDigFace(int32 Edge, bool bActive)
{
	if (!DigFaceMarker)
	{
		return;
	}
	if (!bActive || !EdgeVisuals.IsValidIndex(Edge))
	{
		DigFaceMarker->SetVisibility(false);
		return;
	}
	DigFaceMarker->SetRelativeLocation(FVector(EdgeVisuals[Edge].CurrentEnd.X, DigFaceY, EdgeVisuals[Edge].CurrentEnd.Z));
	DigFaceMarker->SetVisibility(true);
}

void UCrabColonyViewComponent::SetMound(int32 Pellets)
{
	const int32 Shown = FMath::Clamp(Pellets, 0, MoundPellets.Num());
	for (int32 Index = 0; Index < MoundPellets.Num(); ++Index)
	{
		if (MoundPellets[Index])
		{
			MoundPellets[Index]->SetVisibility(Index < Shown);
		}
	}
	ShownMoundPellets = Shown;
}

int32 UCrabColonyViewComponent::AddLoosePellet(FVector2D UV)
{
	UStaticMesh* Sphere = CrabShapeVisual::LoadEngineShape(TEXT("Sphere"));
	// Rests a touch low, as if it had settled onto the tunnel floor rather than floating at the tunnel's centre.
	UStaticMeshComponent* Pellet = AddTintedShape(Sphere, FVector(UV.X, PelletY, UV.Y - 12.f), FRotator::ZeroRotator,
		FVector(MoundPelletDiameter / 100.f), PelletColor);

	for (int32 Index = 0; Index < LoosePellets.Num(); ++Index)
	{
		if (!LoosePellets[Index])
		{
			LoosePellets[Index] = Pellet;
			return Index;
		}
	}
	LoosePellets.Add(Pellet);
	return LoosePellets.Num() - 1;
}

void UCrabColonyViewComponent::RemoveLoosePellet(int32 Handle)
{
	if (LoosePellets.IsValidIndex(Handle) && LoosePellets[Handle])
	{
		LoosePellets[Handle]->DestroyComponent();
		LoosePellets[Handle] = nullptr;
	}
}

// --- CrabSim.ColonyPreview: spawns a cutaway to look at, for iterating on the look. Not part of the game. ---

namespace
{
	void RunColonyPreview(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		TActorIterator<ACrabBeach> BeachIt(World);
		ACrabBeach* Beach = BeachIt ? *BeachIt : nullptr;
		if (!Beach || Beach->GetBurrows().Num() == 0)
		{
			UE_LOG(LogCrabSim, Warning, TEXT("CrabSim.ColonyPreview: no beach, or it has no burrows yet."));
			return;
		}
		Beach->SetActorHiddenInGame(true);

		for (TActorIterator<ACrabGull> It(World); It; ++It)
		{
			It->SetActorHiddenInGame(true);
		}

		const FVector BurrowLocation = Beach->GetBurrows()[0].Location;

		AActor* ViewActor = World->SpawnActor<AActor>(BurrowLocation, FRotator::ZeroRotator);
		if (!ViewActor)
		{
			return;
		}
		USceneComponent* Root = NewObject<USceneComponent>(ViewActor);
		Root->RegisterComponent();
		ViewActor->SetRootComponent(Root);

		UCrabColonyViewComponent* View = NewObject<UCrabColonyViewComponent>(ViewActor);
		View->SetupAttachment(Root);
		View->RegisterComponent();

		const CrabColony::FBlueprint Plan = CrabColony::DefaultBlueprint();
		View->Build(Plan);

		// Pre-dug edges fully open, the nursery (the first dug after them, per DefaultBlueprint's order) partway,
		// the rest untouched: a colony in the middle of its second stretch of digging.
		TArray<float> Dug;
		Dug.Init(0.f, Plan.Edges.Num());
		for (int32 Index = 0; Index < Plan.PreDugEdges; ++Index)
		{
			Dug[Index] = 1.f;
		}
		const int32 NurseryEdge = Plan.PreDugEdges;
		if (Dug.IsValidIndex(NurseryEdge))
		{
			Dug[NurseryEdge] = 0.6f;
		}
		View->SetDug(Dug);
		View->SetDigFace(NurseryEdge, true);
		View->SetMound(12);

		// Five NPCs at sensible points along the open tunnels and chambers: the entrance shaft, a digger heading
		// up with a pellet, a forager heading down with food, a juvenile resting, one dancing.
		struct FNpcSpec
		{
			FVector2D UV;
			ECrabAnim Anim;
			ECarry Carry;
			float Scale;
		};
		const FNpcSpec Specs[] = {
			{FVector2D(0.f, -150.f), ECrabAnim::Scuttle, ECarry::None, 1.f},
			{FVector2D(-200.f, -694.f), ECrabAnim::Scuttle, ECarry::Pellet, 1.f},
			{FVector2D(-270.f, -348.f), ECrabAnim::Scuttle, ECarry::Food, 1.f},
			{FVector2D(370.f, -410.f), ECrabAnim::Idle, ECarry::None, 0.55f},
			{FVector2D(-420.f, -370.f), ECrabAnim::Dance, ECarry::None, 1.f},
		};
		// Facing the glass, as GAME.md says every colony crab does: forward toward the camera, -ViewDirection.
		const float FacingYaw = UCrabColonyViewComponent::ViewDirection().Rotation().Yaw + 180.f;
		for (const FNpcSpec& Spec : Specs)
		{
			if (ACrabColonyNpc* Npc = World->SpawnActor<ACrabColonyNpc>())
			{
				Npc->SetPose(View->PlanToWorld(Spec.UV), FacingYaw);
				Npc->SetAnim(Spec.Anim);
				Npc->SetCarrying(Spec.Carry);
				Npc->SetSizeScale(Spec.Scale);
			}
		}

		const FVector LookAt = View->PlanToWorld(FVector2D(0.f, -650.f));
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (ACrabPawn* Crab = Cast<ACrabPawn>(It->Get()->GetPawn()))
			{
				Crab->SetColonyCameraPreview(LookAt, UCrabColonyViewComponent::ViewDirection(), 2600.f);
			}
		}

		UE_LOG(LogCrabSim, Log, TEXT("CRABSIM_COLONY_PREVIEW built at burrow 0 (%.0f,%.0f,%.0f)"), BurrowLocation.X, BurrowLocation.Y, BurrowLocation.Z);
	}

	static FAutoConsoleCommandWithWorld CmdColonyPreview(
		TEXT("CrabSim.ColonyPreview"),
		TEXT("Builds a sample colony cutaway under burrow 0 (a mix of dug and undug tunnels, a dig face, a pellet"
			 " mound, five NPCs), hides the beach and any gull, and points the camera at it. For looking at the"
			 " look, not for play; run again for a fresh one on top of the last."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunColonyPreview));
}
