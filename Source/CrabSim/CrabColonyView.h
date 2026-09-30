// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CrabColonyMath.h"
#include "CrabColonyView.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Draws the colony's cutaway: a side-on "ant farm" view of a CrabColony::FBlueprint, pale sand behind a strip of
 * darker damp sand and sky at the top, dark hollow tunnels and chambers dug into it. Placed by its owner: the
 * colony actor that runs the jobs and the player's trip down and up are someone else's job, this component is
 * only the look. The plane is the owner's own local XZ: a plan point (U, V) sits at local (U, 0, V), so U is
 * world +X at the plan's own scale and V is world +Z, and the colony actor's own position puts the plane at the
 * burrow's world location (see GAME.md, "Colony (building)").
 *
 * Art/unreal/README.md places the beach's sun "in front of and to the left of" the main camera's starting view
 * (yaw 0, looking along +X); this engine's left there is -Y (CrabGull tags its own left "L" at -Y off a +X-facing
 * heading), so a cutaway that faces the camera from the -Y side, looking back along +Y, catches that leftward
 * component of the sun. That is ViewDirection. It draws with the same lit engine shape material as the rest of
 * the game (ACrabBeach's own dark burrow holes are drawn the same way and read fine), rather than an unlit one:
 * the sun's exact azimuth past "to the left" is not pinned to a degree, but the ordinary lighting reads clearly
 * from this side on screen, so there was nothing to route around.
 */
UCLASS(ClassGroup = (Crab), meta = (BlueprintSpawnableComponent))
class CRABSIM_API UCrabColonyViewComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCrabColonyViewComponent();

	/** Unit vector the camera should look along to face the cutaway. See the class comment for why +Y. */
	static FVector ViewDirection() { return FVector(0.f, 1.f, 0.f); }

	/**
	 * Builds the backdrop, the surface strip, and a tunnel per edge and a disc per chamber, from Plan. Safe to
	 * call again for a new plan: it clears whatever an earlier Build (and SetMound/AddLoosePellet since) drew.
	 */
	void Build(const CrabColony::FBlueprint& InPlan);

	/**
	 * How much of each edge is dug, 0 to 1, indexed as Plan.Edges. A tunnel scales from its A end. Short of the
	 * point the tunnel reaches its B end's chamber (CrabColony::EdgeWork's geometric share of the edge's total
	 * work, i.e. the two nodes' distance divided by EdgeWork) the chamber shows nothing; past it the tunnel holds
	 * at full length and the rest of the fraction instead grows the chamber's disc from a hint of a hollow up to
	 * its full radius. Cheap: transforms and visibility only, no rebuild.
	 */
	void SetDug(const TArray<float>& FractionPerEdge);

	/** A small lighter blob of loose sand at Edge's current dug end (as last set by SetDug). Hidden if bActive is false or Edge is out of range. */
	void SetDigFace(int32 Edge, bool bActive);

	/** The pellet mound beside the entrance, on the surface strip: Pellets tiny sand balls, piling up, capped at a modest count. */
	void SetMound(int32 Pellets);

	/** A loose pellet lying on the tunnel floor at UV, for one the player sets down. Returns a handle for RemoveLoosePellet. */
	int32 AddLoosePellet(FVector2D UV);
	/** Takes back a pellet AddLoosePellet made. Fine to call with a handle already removed. */
	void RemoveLoosePellet(int32 Handle);

	FVector PlanToWorld(FVector2D UV) const { return GetComponentLocation() + FVector(UV.X, 0.f, UV.Y); }
	FVector2D WorldToPlan(FVector World) const
	{
		const FVector Local = World - GetComponentLocation();
		return FVector2D(Local.X, Local.Z);
	}

private:
	struct FEdgeVisual
	{
		TObjectPtr<UStaticMeshComponent> Tunnel = nullptr;
		/** This edge's A end, and the unit A->B direction, both in this component's local space. */
		FVector LocalA = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float GeometricLength = 0.f;
		/** Where in the 0-1 dig fraction the tunnel itself finishes: GeometricLength / CrabColony::EdgeWork. 1 when B holds no chamber. */
		float TunnelCompleteFraction = 1.f;
		/** Plan.Nodes index of a chamber at B, or INDEX_NONE. */
		int32 ChamberNode = INDEX_NONE;
		/** The tunnel's dug-to point, this component's local space, as last set by SetDug: where SetDigFace points. */
		FVector CurrentEnd = FVector::ZeroVector;
	};

	struct FChamberVisual
	{
		TObjectPtr<UStaticMeshComponent> Disc = nullptr;
		float FullRadius = 0.f;
	};

	void Reset();
	void BuildBackdrop();
	void BuildSurfaceStrip();
	void BuildMoundPellets();
	UStaticMeshComponent* AddTintedShape(UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color);

	CrabColony::FBlueprint Plan;
	TArray<FEdgeVisual> EdgeVisuals;
	TArray<FChamberVisual> ChamberVisuals;
	/** Plan.Nodes index -> ChamberVisuals index, INDEX_NONE for a node that holds no chamber. */
	TArray<int32> ChamberOfNode;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Backdrop;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DampSand;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SkyBand;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DigFaceMarker;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MoundPellets;
	int32 ShownMoundPellets = 0;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> LoosePellets;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicMaterial;
};
