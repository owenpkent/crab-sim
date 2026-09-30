// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * The colony under the dune-foot burrow: its plan of tunnels and chambers, and the pure rules that run it. No
 * engine singletons: time and state are passed in, so headless tests drive it exactly as the colony actor does.
 * See GAME.md, "Colony".
 *
 * Positions are in the cutaway's plane: U across (world +X at the same scale), V up (0 is the surface at the
 * colony burrow's mouth, negative is below it). The colony actor puts the plane under the burrow: a point (U, V)
 * is at the burrow's world location plus (U, 0, V).
 */
namespace CrabColony
{
	enum class ENodeKind : uint8
	{
		/** The mouth, at the surface (V = 0). Up and out from here. */
		Entrance,
		/** Where tunnels meet. No room of its own. */
		Junction,
		/** A chamber that holds the colony's food. */
		Pantry,
		/** The chamber where new crabs hatch. */
		Nursery,
		/** A chamber to rest in. Each one raises how many crabs the colony can hold. */
		Rest,
	};

	struct FNode
	{
		ENodeKind Kind = ENodeKind::Junction;
		/** (U, V) in the cutaway plane, uu. */
		FVector2D Pos = FVector2D::ZeroVector;
		/** A chamber's hollow, uu. 0 for an entrance or a junction. */
		float Radius = 0.f;
	};

	/** A tunnel. It is dug from A (the colony side, already open when digging starts) toward B. */
	struct FEdge
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
		/** How wide the tunnel is drawn, uu. A crab fits with room to spare. */
		float Width = 90.f;
	};

	/**
	 * The whole plan. Edges [0, PreDugEdges) are open from the start; the rest are dug in the order they are listed,
	 * each one's A end already open by the time it is reached.
	 */
	struct FBlueprint
	{
		TArray<FNode> Nodes;
		TArray<FEdge> Edges;
		int32 PreDugEdges = 0;
	};

	/**
	 * The colony every round starts from: an entrance shaft, a pantry and a rest chamber off the first junction and a
	 * second junction below it, all open; then the nursery, a second rest chamber, a deeper junction, a third rest
	 * chamber and a second pantry, dug in that order.
	 */
	inline FBlueprint DefaultBlueprint()
	{
		FBlueprint Plan;
		auto Node = [&Plan](ENodeKind Kind, float U, float V, float Radius = 0.f)
		{
			FNode N;
			N.Kind = Kind;
			N.Pos = FVector2D(U, V);
			N.Radius = Radius;
			return Plan.Nodes.Add(N);
		};
		auto Edge = [&Plan](int32 A, int32 B)
		{
			FEdge E;
			E.A = A;
			E.B = B;
			return Plan.Edges.Add(E);
		};

		const int32 Entrance = Node(ENodeKind::Entrance, 0.f, 0.f);
		const int32 Upper = Node(ENodeKind::Junction, 0.f, -300.f);
		const int32 Pantry = Node(ENodeKind::Pantry, -450.f, -380.f, 130.f);
		const int32 Rest = Node(ENodeKind::Rest, 400.f, -420.f, 120.f);
		const int32 Lower = Node(ENodeKind::Junction, 0.f, -650.f);
		const int32 Nursery = Node(ENodeKind::Nursery, -500.f, -760.f, 140.f);
		const int32 Rest2 = Node(ENodeKind::Rest, 450.f, -820.f, 120.f);
		const int32 Deep = Node(ENodeKind::Junction, 100.f, -1000.f);
		const int32 Rest3 = Node(ENodeKind::Rest, -250.f, -1150.f, 120.f);
		const int32 Pantry2 = Node(ENodeKind::Pantry, 500.f, -1180.f, 130.f);

		// Open from the start.
		Edge(Entrance, Upper);
		Edge(Upper, Pantry);
		Edge(Upper, Rest);
		Edge(Upper, Lower);
		Plan.PreDugEdges = Plan.Edges.Num();

		// Dug in this order.
		Edge(Lower, Nursery);
		Edge(Lower, Rest2);
		Edge(Lower, Deep);
		Edge(Deep, Rest3);
		Edge(Deep, Pantry2);
		return Plan;
	}

	inline bool IsChamber(ENodeKind Kind)
	{
		return Kind == ENodeKind::Pantry || Kind == ENodeKind::Nursery || Kind == ENodeKind::Rest;
	}

	/** How much digging an edge takes, uu: its length, plus the chamber's width when it ends in one. */
	inline float EdgeWork(const FBlueprint& Plan, int32 EdgeIndex)
	{
		const FEdge& E = Plan.Edges[EdgeIndex];
		const FNode& B = Plan.Nodes[E.B];
		const float Length = static_cast<float>(FVector2D::Distance(Plan.Nodes[E.A].Pos, B.Pos));
		return Length + (IsChamber(B.Kind) ? B.Radius * 2.f : 0.f);
	}

	// --- Rules below: paths, the nearest point on the tunnels, digging, job choice, hatching. ---

	/**
	 * Every number that paces the colony: how fast crabs walk and dig, how much a chamber holds, and the costs
	 * and timers around foraging and hatching. One struct so a CVar or a test can retune the whole colony at
	 * once, the same way the other Tuning structs in this module do.
	 */
	struct FTuning
	{
		/** A colony NPC's walk speed in the tunnels, uu/s. */
		float NpcWalkSpeed = 220.f;
		/** A colony NPC's walk speed up on the surface while foraging, uu/s: faster than underground, open ground with no tunnel to hug. */
		float NpcSurfaceWalkSpeed = 300.f;
		/** How fast an NPC digger works a tunnel face, uu/s. */
		float NpcDigRate = 12.f;
		/** How fast the player digs, uu/s: faster than an NPC, so helping down here is worth doing. */
		float PlayerDigRate = 30.f;
		/** Dug sand it takes to roll one sand pellet, uu. */
		float PelletUu = 40.f;
		/** Food an open pantry holds. StoreCapacity is this times how many pantries are open. */
		float PantryCapacity = 2.f;
		/** Food in store at the start of a round. */
		float StartStore = 0.6f;
		/** Crabs living in the colony at the start of a round. */
		int32 StartPopulation = 4;
		/** Crabs the colony can hold with no rest chamber open at all. */
		int32 BasePopulation = 3;
		/** More crabs the cap allows for every open rest chamber. */
		int32 PerRestChamber = 2;
		/** Food a hatch costs, spent from Store the moment it completes. */
		float HatchCost = 0.4f;
		/** Seconds of hatch progress needed once CanHatch holds, before a new crab hatches. */
		float HatchSeconds = 20.f;
		/** Forage when Store is below this fraction of StoreCapacity. */
		float ForageBelow = 0.6f;
		/** Colony NPCs allowed to be digging at once. */
		int32 MaxDiggers = 3;
		/** Seconds a forager spends sifting a patch before heading back down with what it found. */
		float ForageSiftSeconds = 8.f;
		/** Richness a forager takes from a patch per second of sifting: a whole trip brings ForageSiftSeconds worth home. */
		float ForageTakePerSecond = 0.02f;
		/** Food eaten from a pantry per second while EAT is held. */
		float PlayerEatPerSecond = 0.1f;
		/** A crab on the surface flees for the burrow once the water is at most this many seconds off, sooner still if a gull is down. */
		float FleeWaterSeconds = 25.f;
		/** A rest lasts at least this many seconds... */
		float RestMin = 8.f;
		/** ...and at most this many, the actual length drawn at random (RestSeconds) from the crab's own seed. */
		float RestMax = 18.f;
		/** How small a just-hatched crab is drawn, as a scale of full size. */
		float JuvenileScale = 0.55f;
		/** Seconds a hatched crab takes to grow from JuvenileScale up to full size. */
		float GrowSeconds = 90.f;
	};

	namespace Detail
	{
		/** The corridor's own straight-line length, A to B, uu: EdgeWork less the chamber's hollowing bonus. */
		inline float EdgeLength(const FBlueprint& Plan, int32 EdgeIndex)
		{
			const FEdge& E = Plan.Edges[EdgeIndex];
			return static_cast<float>(FVector2D::Distance(Plan.Nodes[E.A].Pos, Plan.Nodes[E.B].Pos));
		}

		/** The closest point to P on the segment [A, B], and how far along it that point is (0 at A, |B-A| at B). */
		inline FVector2D ClosestPointOnSegment(const FVector2D& A, const FVector2D& B, const FVector2D& P, float& OutAlong)
		{
			const FVector2D AB = B - A;
			const float LenSq = static_cast<float>(AB.SizeSquared());
			if (LenSq <= KINDA_SMALL_NUMBER)
			{
				OutAlong = 0.f;
				return A;
			}
			const float T = FMath::Clamp(static_cast<float>((P - A) | AB) / LenSq, 0.f, 1.f);
			OutAlong = T * FMath::Sqrt(LenSq);
			return A + AB * T;
		}
	}

	/** Dug state: how much of each edge (index-aligned with Plan.Edges) has been dug, uu. Open once Dug >= EdgeWork. */
	struct FDigState
	{
		TArray<float> Dug;
	};

	/** The dig state a fresh blueprint starts from: the pre-dug edges already complete, everything else untouched. */
	inline FDigState InitialDigState(const FBlueprint& Plan)
	{
		FDigState Dig;
		Dig.Dug.SetNumZeroed(Plan.Edges.Num());
		for (int32 Index = 0; Index < Plan.PreDugEdges; ++Index)
		{
			Dig.Dug[Index] = EdgeWork(Plan, Index);
		}
		return Dig;
	}

	inline bool IsEdgeOpen(const FBlueprint& Plan, const FDigState& Dig, int32 EdgeIndex)
	{
		return Dig.Dug[EdgeIndex] >= EdgeWork(Plan, EdgeIndex);
	}

	/** A node is open once any edge touching it is open, or it is node 0, the entrance: always open, even before its own shaft is dug. */
	inline bool IsNodeOpen(const FBlueprint& Plan, const FDigState& Dig, int32 NodeIndex)
	{
		if (NodeIndex == 0)
		{
			return true;
		}
		for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
		{
			const FEdge& E = Plan.Edges[Index];
			if ((E.A == NodeIndex || E.B == NodeIndex) && IsEdgeOpen(Plan, Dig, Index))
			{
				return true;
			}
		}
		return false;
	}

	/** The first edge in plan order that is not yet open: what is being dug right now. INDEX_NONE once everything is. */
	inline int32 ActiveDigEdge(const FBlueprint& Plan, const FDigState& Dig)
	{
		for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
		{
			if (!IsEdgeOpen(Plan, Dig, Index))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** How far into an edge's work digging has got, 0 to 1. */
	inline float DigFraction(const FBlueprint& Plan, const FDigState& Dig, int32 EdgeIndex)
	{
		const float Work = EdgeWork(Plan, EdgeIndex);
		return Work > KINDA_SMALL_NUMBER ? FMath::Clamp(Dig.Dug[EdgeIndex] / Work, 0.f, 1.f) : 1.f;
	}

	/**
	 * Where the tunnel's face currently is: along the straight line from A to B by however much of the corridor
	 * is dug, stopping dead at B once the corridor itself is through (any further digging is the chamber at B
	 * hollowing out, which does not move the face any closer to the glass).
	 */
	inline FVector2D DigFacePos(const FBlueprint& Plan, const FDigState& Dig, int32 EdgeIndex)
	{
		const FEdge& E = Plan.Edges[EdgeIndex];
		const FVector2D A = Plan.Nodes[E.A].Pos;
		const FVector2D B = Plan.Nodes[E.B].Pos;
		const float Length = Detail::EdgeLength(Plan, EdgeIndex);
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return B;
		}
		const float Alpha = FMath::Clamp(Dig.Dug[EdgeIndex] / Length, 0.f, 1.f);
		return FMath::Lerp(A, B, Alpha);
	}

	/**
	 * A place on the open network: which edge it belongs to, how far along that edge's corridor it sits (0 at A,
	 * up to the corridor's length at B), and its actual position. Inside an open chamber, Pos can be any point
	 * in the room (chambers are free space, walked anywhere inside) while Edge and Along still name the corridor
	 * that leads into it, Along sitting at that corridor's far end: the room is treated as hanging off that end.
	 */
	struct FSpot
	{
		int32 Edge = INDEX_NONE;
		float Along = 0.f;
		FVector2D Pos = FVector2D::ZeroVector;
	};

	/**
	 * The reachable point nearest to Point. Inside an open chamber's circle, Point is itself walkable, so it is
	 * the spot outright, on the edge that opens into that chamber. Otherwise the nearest point on any edge's dug
	 * segment: from A up to however far digging has reached on that edge, so a click past an unfinished face
	 * lands on the face and never beyond it, and a click near a chamber that is not open yet lands on the tunnel
	 * short of it the same way.
	 */
	inline FSpot NearestSpot(const FBlueprint& Plan, const FDigState& Dig, const FVector2D& Point)
	{
		for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
		{
			const FEdge& E = Plan.Edges[Index];
			const FNode& NodeB = Plan.Nodes[E.B];
			if (IsChamber(NodeB.Kind) && IsEdgeOpen(Plan, Dig, Index) && FVector2D::Distance(Point, NodeB.Pos) <= NodeB.Radius)
			{
				return FSpot{Index, Detail::EdgeLength(Plan, Index), Point};
			}
		}

		int32 BestEdge = INDEX_NONE;
		float BestAlong = 0.f;
		FVector2D BestPos = FVector2D::ZeroVector;
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
		{
			const FEdge& E = Plan.Edges[Index];
			const FVector2D A = Plan.Nodes[E.A].Pos;
			const FVector2D B = Plan.Nodes[E.B].Pos;
			const float Length = Detail::EdgeLength(Plan, Index);
			const float SegLen = FMath::Clamp(Dig.Dug[Index], 0.f, Length);
			const FVector2D Dir = Length > KINDA_SMALL_NUMBER ? (B - A) / Length : FVector2D::ZeroVector;
			const FVector2D SegEnd = A + Dir * SegLen;
			float Along = 0.f;
			const FVector2D Candidate = Detail::ClosestPointOnSegment(A, SegEnd, Point, Along);
			const float DistSq = static_cast<float>(FVector2D::DistSquared(Point, Candidate));
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestEdge = Index;
				BestAlong = Along;
				BestPos = Candidate;
			}
		}
		return FSpot{BestEdge, BestAlong, BestPos};
	}

	/** The spot standing exactly at a node: on the first edge touching it, at whichever end of that edge the node is. */
	inline FSpot SpotAtNode(const FBlueprint& Plan, int32 NodeIndex)
	{
		const FVector2D Pos = Plan.Nodes[NodeIndex].Pos;
		for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
		{
			if (Plan.Edges[Index].A == NodeIndex)
			{
				return FSpot{Index, 0.f, Pos};
			}
		}
		for (int32 Index = 0; Index < Plan.Edges.Num(); ++Index)
		{
			if (Plan.Edges[Index].B == NodeIndex)
			{
				return FSpot{Index, Detail::EdgeLength(Plan, Index), Pos};
			}
		}
		return FSpot{INDEX_NONE, 0.f, Pos};
	}

	namespace Detail
	{
		/** A node a spot can reach on its own, without any help from FindPath's own Dijkstra step, and the distance to it. */
		struct FSpotEnd
		{
			int32 Node = INDEX_NONE;
			float Distance = 0.f;
		};

		/**
		 * The one or two nodes a spot can walk to directly: always the A end (Along, plus however far Pos sits
		 * off the corridor's line, which is 0 unless this is a chamber's free space); the B end too, the same
		 * way, but only when the edge is fully open (otherwise there is no corridor past the spot at all).
		 */
		inline void SpotEnds(const FBlueprint& Plan, const FDigState& Dig, const FSpot& Spot, TArray<FSpotEnd, TInlineAllocator<2>>& OutEnds)
		{
			OutEnds.Reset();
			const FEdge& E = Plan.Edges[Spot.Edge];
			const FVector2D A = Plan.Nodes[E.A].Pos;
			const FVector2D B = Plan.Nodes[E.B].Pos;
			const float Length = EdgeLength(Plan, Spot.Edge);
			const float Along = FMath::Clamp(Spot.Along, 0.f, Length);
			const FVector2D OnLine = Length > KINDA_SMALL_NUMBER ? FMath::Lerp(A, B, Along / Length) : A;
			const float OffLine = static_cast<float>(FVector2D::Distance(OnLine, Spot.Pos));
			OutEnds.Add(FSpotEnd{E.A, Along + OffLine});
			if (IsEdgeOpen(Plan, Dig, Spot.Edge))
			{
				OutEnds.Add(FSpotEnd{E.B, (Length - Along) + OffLine});
			}
		}
	}

	/**
	 * The shortest walk along open tunnels from From to To: Dijkstra over the node graph (small, a dozen nodes
	 * at most), each open edge weighted by its own corridor length. From and To join that graph by however far
	 * each sits from the ends of its own edge (SpotEnds). When both are on the same edge the node graph is
	 * skipped outright and the waypoints are just the two points: routing via a node would only walk the same
	 * corridor twice over. False (and OutWaypoints left empty) when no open path joins them.
	 */
	inline bool FindPath(const FBlueprint& Plan, const FDigState& Dig, const FSpot& From, const FSpot& To, TArray<FVector2D>& OutWaypoints)
	{
		OutWaypoints.Reset();
		if (From.Edge == To.Edge)
		{
			OutWaypoints.Add(From.Pos);
			if (!OutWaypoints.Last().Equals(To.Pos))
			{
				OutWaypoints.Add(To.Pos);
			}
			return true;
		}

		TArray<Detail::FSpotEnd, TInlineAllocator<2>> FromEnds;
		TArray<Detail::FSpotEnd, TInlineAllocator<2>> ToEnds;
		Detail::SpotEnds(Plan, Dig, From, FromEnds);
		Detail::SpotEnds(Plan, Dig, To, ToEnds);

		const int32 NumNodes = Plan.Nodes.Num();
		TArray<float> BestDist;
		TArray<int32> Prev;
		TArray<bool> Visited;
		BestDist.Init(TNumericLimits<float>::Max(), NumNodes);
		Prev.Init(INDEX_NONE, NumNodes);
		Visited.Init(false, NumNodes);

		for (const Detail::FSpotEnd& End : FromEnds)
		{
			BestDist[End.Node] = FMath::Min(BestDist[End.Node], End.Distance);
		}

		for (int32 Iteration = 0; Iteration < NumNodes; ++Iteration)
		{
			int32 Node = INDEX_NONE;
			float Best = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < NumNodes; ++Index)
			{
				if (!Visited[Index] && BestDist[Index] < Best)
				{
					Best = BestDist[Index];
					Node = Index;
				}
			}
			if (Node == INDEX_NONE)
			{
				break;
			}
			Visited[Node] = true;
			for (int32 EdgeIndex = 0; EdgeIndex < Plan.Edges.Num(); ++EdgeIndex)
			{
				if (!IsEdgeOpen(Plan, Dig, EdgeIndex))
				{
					continue;
				}
				const FEdge& E = Plan.Edges[EdgeIndex];
				int32 Other = INDEX_NONE;
				if (E.A == Node)
				{
					Other = E.B;
				}
				else if (E.B == Node)
				{
					Other = E.A;
				}
				else
				{
					continue;
				}
				const float NewDist = BestDist[Node] + Detail::EdgeLength(Plan, EdgeIndex);
				if (NewDist < BestDist[Other])
				{
					BestDist[Other] = NewDist;
					Prev[Other] = Node;
				}
			}
		}

		int32 BestToNode = INDEX_NONE;
		float BestTotal = TNumericLimits<float>::Max();
		for (const Detail::FSpotEnd& End : ToEnds)
		{
			if (BestDist[End.Node] < TNumericLimits<float>::Max())
			{
				const float Total = BestDist[End.Node] + End.Distance;
				if (Total < BestTotal)
				{
					BestTotal = Total;
					BestToNode = End.Node;
				}
			}
		}
		if (BestToNode == INDEX_NONE)
		{
			return false;
		}

		TArray<int32> NodePath;
		for (int32 Node = BestToNode; Node != INDEX_NONE; Node = Prev[Node])
		{
			NodePath.Insert(Node, 0);
		}

		OutWaypoints.Add(From.Pos);
		for (int32 Node : NodePath)
		{
			const FVector2D& Pos = Plan.Nodes[Node].Pos;
			if (!OutWaypoints.Last().Equals(Pos))
			{
				OutWaypoints.Add(Pos);
			}
		}
		if (!OutWaypoints.Last().Equals(To.Pos))
		{
			OutWaypoints.Add(To.Pos);
		}
		return true;
	}

	/** The total length of a waypoint path, uu. */
	inline float PathLength(TArrayView<const FVector2D> Waypoints)
	{
		float Length = 0.f;
		for (int32 Index = 1; Index < Waypoints.Num(); ++Index)
		{
			Length += static_cast<float>(FVector2D::Distance(Waypoints[Index - 1], Waypoints[Index]));
		}
		return Length;
	}

	/**
	 * Advances Pos along Waypoints by Distance uu, a mover's speed*dt each tick. Crosses as many waypoints as
	 * Distance reaches in one call. InOutWaypoint is the next waypoint being walked toward; it reaches
	 * Waypoints.Num() on arrival, and calling this again once there just returns Pos unchanged. Never overshoots
	 * the last waypoint.
	 */
	inline FVector2D StepAlong(const TArray<FVector2D>& Waypoints, FVector2D Pos, float Distance, int32& InOutWaypoint)
	{
		Distance = FMath::Max(Distance, 0.f);
		while (Distance > 0.f && InOutWaypoint < Waypoints.Num())
		{
			const FVector2D Target = Waypoints[InOutWaypoint];
			const float Remaining = static_cast<float>(FVector2D::Distance(Pos, Target));
			if (Remaining <= Distance)
			{
				Pos = Target;
				Distance -= Remaining;
				++InOutWaypoint;
			}
			else
			{
				FVector2D Dir = Target - Pos;
				Dir.Normalize();
				Pos = Pos + Dir * Distance;
				Distance = 0.f;
			}
		}
		return Pos;
	}

	/**
	 * The colony's living state: what has been dug, what is in store, who lives there and how close the next
	 * hatch is. The mound of pellets and the crabs themselves are either derived from this or owned by the
	 * colony actor.
	 */
	struct FColonyState
	{
		FDigState Dig;
		/** Food in the pantries, one pooled number regardless of which pantry it sits in. */
		float Store = 0.f;
		int32 Population = 0;
		/** Seconds of progress toward the next hatch while CanHatch holds; reset to 0 the moment it does not. */
		float HatchProgress = 0.f;
		/** Whole sand pellets waiting at the mound. */
		int32 PelletsOnMound = 0;
		/** Dug sand since the last whole pellet, uu, carried over to the next PelletsFor call. */
		float PelletCarry = 0.f;
	};

	/** Food the colony can hold: PantryCapacity for every open pantry. */
	inline float StoreCapacity(const FBlueprint& Plan, const FDigState& Dig, const FTuning& Tuning)
	{
		int32 OpenPantries = 0;
		for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
		{
			if (Plan.Nodes[Index].Kind == ENodeKind::Pantry && IsNodeOpen(Plan, Dig, Index))
			{
				++OpenPantries;
			}
		}
		return Tuning.PantryCapacity * OpenPantries;
	}

	/** Crabs the colony can hold: BasePopulation plus PerRestChamber for every open rest chamber. */
	inline int32 PopulationCap(const FBlueprint& Plan, const FDigState& Dig, const FTuning& Tuning)
	{
		int32 OpenRestChambers = 0;
		for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
		{
			if (Plan.Nodes[Index].Kind == ENodeKind::Rest && IsNodeOpen(Plan, Dig, Index))
			{
				++OpenRestChambers;
			}
		}
		return Tuning.BasePopulation + Tuning.PerRestChamber * OpenRestChambers;
	}

	/** The nursery is open, there is enough food in store to spend, and there is room for one more crab. */
	inline bool CanHatch(const FBlueprint& Plan, const FColonyState& State, const FTuning& Tuning)
	{
		bool bNurseryOpen = false;
		for (int32 Index = 0; Index < Plan.Nodes.Num(); ++Index)
		{
			if (Plan.Nodes[Index].Kind == ENodeKind::Nursery && IsNodeOpen(Plan, State.Dig, Index))
			{
				bNurseryOpen = true;
				break;
			}
		}
		return bNurseryOpen && State.Store >= Tuning.HatchCost && State.Population < PopulationCap(Plan, State.Dig, Tuning);
	}

	/**
	 * Advances HatchProgress while CanHatch holds; it resets to 0 the moment CanHatch does not (a closed
	 * nursery, an empty store or a full colony all pause it rather than banking progress toward later). Returns
	 * true, and pays HatchCost from Store and adds one to Population, on the step HatchSeconds is reached.
	 */
	inline bool StepHatch(FColonyState& State, const FBlueprint& Plan, float DeltaSeconds, const FTuning& Tuning)
	{
		if (!CanHatch(Plan, State, Tuning))
		{
			State.HatchProgress = 0.f;
			return false;
		}
		State.HatchProgress += FMath::Max(DeltaSeconds, 0.f);
		if (State.HatchProgress >= Tuning.HatchSeconds)
		{
			State.HatchProgress = 0.f;
			State.Store -= Tuning.HatchCost;
			++State.Population;
			return true;
		}
		return false;
	}

	/** Digs an edge by up to Amount uu, clamped to what is left of its work. Returns how much actually happened: 0 once it is already open. */
	inline float DigAt(FDigState& Dig, const FBlueprint& Plan, int32 EdgeIndex, float Amount)
	{
		const float Work = EdgeWork(Plan, EdgeIndex);
		const float Before = Dig.Dug[EdgeIndex];
		if (Before >= Work)
		{
			return 0.f;
		}
		const float After = FMath::Min(Before + FMath::Max(Amount, 0.f), Work);
		Dig.Dug[EdgeIndex] = After;
		return After - Before;
	}

	/** Turns dug sand into whole pellets of PelletUu each, carrying the leftover in InOutCarry for next time. */
	inline int32 PelletsFor(float DugUu, float& InOutCarry, const FTuning& Tuning)
	{
		InOutCarry += FMath::Max(DugUu, 0.f);
		const int32 Pellets = Tuning.PelletUu > KINDA_SMALL_NUMBER ? FMath::FloorToInt(InOutCarry / Tuning.PelletUu) : 0;
		InOutCarry -= Pellets * Tuning.PelletUu;
		return Pellets;
	}

	/** A colony NPC's job, picked whenever the last one finishes. */
	enum class EJob : uint8
	{
		Rest,
		Dig,
		Haul,
		Forage,
		ReturnHome,
		Flee,
	};

	/** Where a colony crab is while it works a job: down in the tunnels, or up on the surface (foraging, or fleeing back down). */
	enum class EPlace : uint8
	{
		Underground,
		Surface,
	};

	/** What ChooseJob needs to know about the colony right now. */
	struct FJobContext
	{
		/** Store as a fraction of StoreCapacity: 0 empty, 1 full (or past it, if Store ever exceeds capacity). */
		float StoreFraction = 0.f;
		/** Any tunnel in the plan is still undug. */
		bool bDigLeft = false;
		/** Low enough tide, a food patch reachable, and no gull down: the surface is safe and worth the trip. */
		bool bForageOpen = false;
		int32 Foragers = 0;
		int32 Diggers = 0;
		int32 Population = 0;
	};

	/**
	 * A colony NPC's next job, chosen deterministically from the colony's state: forage first, while the store
	 * is low, the surface is safe, and foraging is not already at its share of the population; else dig, while
	 * any of the plan is left and digging is not already at MaxDiggers; else rest. The three conditions are
	 * checked in that fixed order and the first one that holds wins, so there is never a tie between them for
	 * Seed to break: two NPCs handed the same FJobContext always agree. Seed is threaded through only so the
	 * caller has one ready to hand to RestSeconds once Rest is the answer.
	 */
	inline EJob ChooseJob(const FJobContext& Context, uint32 Seed, const FTuning& Tuning)
	{
		(void)Seed;
		const int32 MaxForagers = FMath::Max(1, Context.Population / 3);
		if (Context.bForageOpen && Context.StoreFraction < Tuning.ForageBelow && Context.Foragers < MaxForagers)
		{
			return EJob::Forage;
		}
		if (Context.bDigLeft && Context.Diggers < Tuning.MaxDiggers)
		{
			return EJob::Dig;
		}
		return EJob::Rest;
	}

	/** How long a rest lasts, drawn from [RestMin, RestMax] by Seed: the one place a job's own randomness lives. */
	inline float RestSeconds(uint32 Seed, const FTuning& Tuning)
	{
		FRandomStream Random(static_cast<int32>(Seed));
		return Random.FRandRange(Tuning.RestMin, Tuning.RestMax);
	}

	/** A crab up on the surface should flee for the burrow: the gull is down, or the water is at most this close. */
	inline bool ShouldFlee(float SecondsUntilWater, bool bGullDown, const FTuning& Tuning)
	{
		return bGullDown || SecondsUntilWater <= Tuning.FleeWaterSeconds;
	}
}
