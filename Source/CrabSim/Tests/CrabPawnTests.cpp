// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabTestHelpers.h"
#include "CrabMovementMath.h"
#include "CrabSurvivalMath.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

using namespace UE::CrabSim::Tests;

namespace
{
	/** A beach, a crab standing on it at the start, and a world to tick. */
	struct FRig
	{
		FCrabTestWorld World;
		ACrabBeach* Beach = nullptr;
		ACrabPawn* Crab = nullptr;

		explicit FRig(float X = 0.f, float Y = 0.f)
		{
			if (World.IsReady())
			{
				Beach = SpawnBeach(World);
				if (Beach)
				{
					Crab = SpawnCrab(World, *Beach, X, Y);
				}
			}
		}

		bool IsValid() const { return World.IsReady() && Beach && Crab; }
	};

	float Yaw(const ACrabPawn& Crab) { return FRotator::NormalizeAxis(Crab.GetActorRotation().Yaw); }
	float Speed(const ACrabPawn& Crab) { return Crab.GetCharacterMovement()->Velocity.Size2D(); }
}

// --- Standing and walking ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnSettlesTest, "CrabSim.Pawn.SettlesOnTheGround", TestFlags)
bool FCrabPawnSettlesTest::RunTest(const FString& Parameters)
{
	FRig Rig(-700.f, 300.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.World.TickSeconds(1.5f);

	const float Half = Rig.Crab->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Where = Rig.Crab->GetActorLocation();
	TestNearlyEqual(TEXT("feet are on the terrain"), static_cast<float>(Where.Z - Half), Rig.Beach->GetGroundHeight(Where.X, Where.Y), 12.f);
	TestTrue(TEXT("standing on walkable ground"), Rig.Crab->GetCharacterMovement()->IsMovingOnGround());
	TestNearlyEqual(TEXT("not moving"), Speed(*Rig.Crab), 0.f, 1.f);
	TestNearlyEqual(TEXT("full grip"), Rig.Crab->GetGrip(), 1.f, 1e-4f);
	TestEqual(TEXT("not swept"), Rig.Crab->GetSweptCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnWalksSidewaysTest, "CrabSim.Pawn.ScuttlesSidewaysTowardATarget", TestFlags)
bool FCrabPawnWalksSidewaysTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	const FVector Start = Rig.Crab->GetActorLocation();
	Rig.Crab->SetMoveTarget(Start + FVector(0.f, 900.f, 0.f));
	TestTrue(TEXT("has a target"), Rig.Crab->HasMoveTarget());
	Rig.World.TickSeconds(1.0f);

	const FVector Now = Rig.Crab->GetActorLocation();
	TestTrue(TEXT("moved toward +Y"), Now.Y - Start.Y > 200.f);
	TestTrue(TEXT("without straying in X"), FMath::Abs(Now.X - Start.X) < 60.f);
	const float Yaw0 = Yaw(*Rig.Crab);
	TestTrue(*FString::Printf(TEXT("side-on: facing along X (yaw %.1f)"), Yaw0), FMath::Min(FMath::Abs(Yaw0), 180.f - FMath::Abs(Yaw0)) < 8.f);
	TestTrue(*FString::Printf(TEXT("at scuttle speed (%.0f)"), Speed(*Rig.Crab)), Speed(*Rig.Crab) > 350.f);
	TestEqual(TEXT("scuttle animation state"), Rig.Crab->GetAnimState(), ECrabAnim::Scuttle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnFacesSideOnBothWaysTest, "CrabSim.Pawn.TurnsAtMostNinetyDegreesToScuttle", TestFlags)
bool FCrabPawnFacesSideOnBothWaysTest::RunTest(const FString& Parameters)
{
	// Heading +X, the crab ends up facing +90 or -90, whichever side it was already nearer.
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->SetActorRotation(FRotator(0.f, 30.f, 0.f));
		Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(900.f, 0.f, 0.f));
		Rig.World.TickSeconds(0.8f);
		TestNearlyEqual(TEXT("from +30 it takes the +90 side"), Yaw(*Rig.Crab), 90.f, 6.f);
	}
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->SetActorRotation(FRotator(0.f, -30.f, 0.f));
		Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(900.f, 0.f, 0.f));
		Rig.World.TickSeconds(0.8f);
		TestNearlyEqual(TEXT("from -30 it takes the -90 side"), Yaw(*Rig.Crab), -90.f, 6.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnArrivesTest, "CrabSim.Pawn.ArrivesAndStops", TestFlags)
bool FCrabPawnArrivesTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	const FVector Goal = Rig.Crab->GetActorLocation() + FVector(200.f, 350.f, 0.f);
	Rig.Crab->SetMoveTarget(Goal);
	Rig.World.TickSeconds(3.f);

	TestFalse(TEXT("target is cleared on arrival"), Rig.Crab->HasMoveTarget());
	TestTrue(TEXT("stopped near the goal"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Goal) < 60.f);
	TestTrue(TEXT("and is no longer moving"), Speed(*Rig.Crab) < 20.f);
	TestEqual(TEXT("back to idle"), Rig.Crab->GetAnimState(), ECrabAnim::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnClearTargetTest, "CrabSim.Pawn.ClearingTheTargetStopsTheCrab", TestFlags)
bool FCrabPawnClearTargetTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(0.f, 2000.f, 0.f));
	Rig.World.TickSeconds(0.5f);
	Rig.Crab->ClearMoveTarget();
	TestFalse(TEXT("no target"), Rig.Crab->HasMoveTarget());
	Rig.World.TickSeconds(1.f);
	TestTrue(TEXT("it comes to a stop"), Speed(*Rig.Crab) < 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnTargetKeepsGroundHeightTest, "CrabSim.Pawn.TargetHeightIsTheGroundNotTheClick", TestFlags)
bool FCrabPawnTargetKeepsGroundHeightTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	Rig.Crab->SetMoveTarget(FVector(400.f, 400.f, 99999.f));
	TestNearlyEqual(TEXT("target Z is the crab's feet, so a high click cannot lift the crab"), static_cast<float>(Rig.Crab->GetMoveTarget().Z), Rig.Crab->GetFeetZ(), 1e-3f);
	return true;
}

// --- Dash ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDashTest, "CrabSim.Pawn.DashBurstsThenCoolsDown", TestFlags)
bool FCrabPawnDashTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	const FVector Toward = Rig.Crab->GetActorLocation() + FVector(0.f, 600.f, 0.f);

	TestTrue(TEXT("first dash starts"), Rig.Crab->TryDash(Toward));
	TestTrue(TEXT("dashing"), Rig.Crab->IsDashing());
	Rig.World.TickN(1, 1.f / 60.f);
	TestTrue(*FString::Printf(TEXT("dash speed is a burst (%.0f)"), Speed(*Rig.Crab)), Speed(*Rig.Crab) > 800.f);
	TestEqual(TEXT("dash animation state"), Rig.Crab->GetAnimState(), ECrabAnim::Dash);

	TestFalse(TEXT("cannot dash again mid-dash"), Rig.Crab->TryDash(Toward));
	Rig.World.TickSeconds(0.45f);
	TestFalse(TEXT("the dash is over"), Rig.Crab->IsDashing());
	TestFalse(TEXT("still cooling down"), Rig.Crab->TryDash(Toward));
	TestTrue(TEXT("cooldown reported"), Rig.Crab->GetDashCooldownRemaining() > 0.3f);

	Rig.World.TickSeconds(1.0f);
	TestTrue(TEXT("dash is available again after the cooldown"), Rig.Crab->TryDash(Toward));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDashSurvivesTheFirstFrameTest, "CrabSim.Pawn.DashBurstIsNotBrakedAwayByTheOldSpeedCap", TestFlags)
bool FCrabPawnDashSurvivesTheFirstFrameTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	// Standing still, so the walking cap is at its lowest. The burst must still reach nearly full speed.
	Rig.Crab->GetCharacterMovement()->MaxWalkSpeed = 100.f;
	Rig.Crab->TryDash(Rig.Crab->GetActorLocation() + FVector(0.f, 600.f, 0.f));
	const float Full = Rig.Crab->SideSpeed * Rig.Crab->DashSpeedMultiplier;
	Rig.World.TickN(1, 1.f / 60.f);
	TestTrue(*FString::Printf(TEXT("speed %.0f is still close to the full burst %.0f"), Speed(*Rig.Crab), Full), Speed(*Rig.Crab) > 0.95f * Full);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDashDistanceTest, "CrabSim.Pawn.DashCoversRealGround", TestFlags)
bool FCrabPawnDashDistanceTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	const FVector Start = Rig.Crab->GetActorLocation();
	Rig.Crab->TryDash(Start + FVector(0.f, -600.f, 0.f));
	Rig.World.TickSeconds(0.4f);
	TestTrue(*FString::Printf(TEXT("moved toward -Y (%.0f)"), Start.Y - Rig.Crab->GetActorLocation().Y), Start.Y - Rig.Crab->GetActorLocation().Y > 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDashAtSelfTest, "CrabSim.Pawn.DashAtItsOwnPositionDoesNothing", TestFlags)
bool FCrabPawnDashAtSelfTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestFalse(TEXT("no direction, no dash"), Rig.Crab->TryDash(Rig.Crab->GetActorLocation()));
	TestFalse(TEXT("and no cooldown is spent"), Rig.Crab->GetDashCooldownRemaining() > 0.f);
	return true;
}

// --- Dance -------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDanceTest, "CrabSim.Pawn.DancesFacingTheCameraAndHoldsGround", TestFlags)
bool FCrabPawnDanceTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(0.f, 3000.f, 0.f));
	Rig.World.TickSeconds(0.3f);
	TestTrue(TEXT("dance starts"), Rig.Crab->StartDance());
	TestTrue(TEXT("dancing"), Rig.Crab->IsDancing());
	TestFalse(TEXT("dancing drops the walking target"), Rig.Crab->HasMoveTarget());

	Rig.World.TickSeconds(1.5f);
	TestNearlyEqual(TEXT("faces the camera"), FMath::Abs(Yaw(*Rig.Crab)), 180.f, 2.f);
	TestTrue(TEXT("holds its ground"), Speed(*Rig.Crab) < 15.f);
	TestEqual(TEXT("dance animation state"), Rig.Crab->GetAnimState(), ECrabAnim::Dance);

	const FVector Before = Rig.Crab->GetActorLocation();
	Rig.World.TickSeconds(1.f);
	TestTrue(TEXT("it dances on the spot"), FVector::Dist2D(Before, Rig.Crab->GetActorLocation()) < 6.f);

	Rig.Crab->StopDance();
	TestFalse(TEXT("dance stops"), Rig.Crab->IsDancing());
	Rig.World.TickSeconds(0.3f);
	TestEqual(TEXT("back to idle"), Rig.Crab->GetAnimState(), ECrabAnim::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnToggleDanceTest, "CrabSim.Pawn.ToggleDanceFlipsIt", TestFlags)
bool FCrabPawnToggleDanceTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("toggle on"), Rig.Crab->ToggleDance());
	TestTrue(TEXT("dancing"), Rig.Crab->IsDancing());
	TestFalse(TEXT("toggle off"), Rig.Crab->ToggleDance());
	TestFalse(TEXT("not dancing"), Rig.Crab->IsDancing());
	TestTrue(TEXT("starting twice is harmless"), Rig.Crab->StartDance() && Rig.Crab->StartDance());
	Rig.Crab->StopDance();
	Rig.Crab->StopDance();
	TestFalse(TEXT("stopping twice is harmless"), Rig.Crab->IsDancing());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDanceBlockedTest, "CrabSim.Pawn.CannotDanceInABurrowOrTheSurgeOrMidDash", TestFlags)
bool FCrabPawnDanceBlockedTest::RunTest(const FString& Parameters)
{
	// In a burrow.
	{
		FRig Rig(-2450.f, 350.f);
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		TestTrue(TEXT("dug in"), Rig.Crab->EnterBurrow(HighBurrow));
		TestFalse(TEXT("no dancing underground"), Rig.Crab->StartDance());
	}
	// In the surge.
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Beach->SetTideClock(HighTide);
		Rig.World.TickN(2, 1.f / 60.f);
		TestTrue(TEXT("in deep water"), Rig.Crab->GetWaterDepth() > CrabSurvival::SurgeDepth);
		TestFalse(TEXT("no dancing in the surge"), Rig.Crab->StartDance());
	}
	// Mid dash.
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->TryDash(Rig.Crab->GetActorLocation() + FVector(0.f, 500.f, 0.f));
		TestFalse(TEXT("no dancing mid dash"), Rig.Crab->StartDance());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDanceEndsTest, "CrabSim.Pawn.DanceEndsWhenTheTideArrivesOrTheCrabDashes", TestFlags)
bool FCrabPawnDanceEndsTest::RunTest(const FString& Parameters)
{
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->StartDance();
		Rig.World.TickSeconds(0.3f);
		TestTrue(TEXT("dancing on dry sand"), Rig.Crab->IsDancing());
		Rig.Beach->SetTideClock(HighTide);
		Rig.World.TickSeconds(0.2f);
		TestFalse(TEXT("the tide interrupts the dance"), Rig.Crab->IsDancing());
	}
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->StartDance();
		Rig.World.TickSeconds(0.3f);
		TestTrue(TEXT("a dash works from a dance"), Rig.Crab->TryDash(Rig.Crab->GetActorLocation() + FVector(0.f, 500.f, 0.f)));
		TestFalse(TEXT("and ends it"), Rig.Crab->IsDancing());
	}
	return true;
}

// --- Burrows -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnBurrowEnterExitTest, "CrabSim.Pawn.DigsInAndOut", TestFlags)
bool FCrabPawnBurrowEnterExitTest::RunTest(const FString& Parameters)
{
	FRig Rig(-2300.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	TestTrue(TEXT("digs in"), Rig.Crab->EnterBurrow(HighBurrow));
	TestTrue(TEXT("in a burrow"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("the right one"), Rig.Crab->GetCurrentBurrow(), HighBurrow);
	TestTrue(TEXT("moved onto the hole"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Rig.Beach->GetBurrows()[HighBurrow].Location) < 3.f);

	Rig.World.TickSeconds(1.f);
	TestTrue(*FString::Printf(TEXT("sank out of sight (%.2f)"), Rig.Crab->GetBurrowSink()), Rig.Crab->GetBurrowSink() > 0.95f);
	TestTrue(TEXT("perfectly still"), Speed(*Rig.Crab) < 1.f);
	TestNearlyEqual(TEXT("no water inside"), Rig.Crab->GetWaterDepth(), 0.f, 1e-3f);

	Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(0.f, 800.f, 0.f));
	TestFalse(TEXT("a crab in its burrow ignores walking orders"), Rig.Crab->HasMoveTarget());

	Rig.Crab->ExitBurrow();
	TestFalse(TEXT("out of the burrow"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("no burrow index"), Rig.Crab->GetCurrentBurrow(), static_cast<int32>(INDEX_NONE));
	Rig.World.TickSeconds(1.f);
	TestTrue(TEXT("rose back to the surface"), Rig.Crab->GetBurrowSink() < 0.05f);

	const FVector Out = Rig.Crab->GetActorLocation();
	Rig.Crab->SetMoveTarget(Out + FVector(0.f, 700.f, 0.f));
	Rig.World.TickSeconds(1.f);
	TestTrue(TEXT("and can walk again"), Rig.Crab->GetActorLocation().Y - Out.Y > 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnBurrowRejectsTest, "CrabSim.Pawn.RefusesBadOrFloodedBurrows", TestFlags)
bool FCrabPawnBurrowRejectsTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	TestFalse(TEXT("no such burrow"), Rig.Crab->EnterBurrow(42));
	TestFalse(TEXT("nor INDEX_NONE"), Rig.Crab->EnterBurrow(INDEX_NONE));
	TestFalse(TEXT("nothing happened"), Rig.Crab->IsInBurrow());

	Rig.Beach->SetTideClock(HighTide);
	TestTrue(TEXT("the low burrow is flooded"), Rig.Beach->IsBurrowFlooded(LowBurrow));
	TestFalse(TEXT("so the crab will not dig into it"), Rig.Crab->EnterBurrow(LowBurrow));
	TestTrue(TEXT("and says why"), Rig.Crab->GetMessage().Contains(TEXT("flooded")));
	TestFalse(TEXT("still not in a burrow"), Rig.Crab->IsInBurrow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnBurrowOnArrivalTest, "CrabSim.Pawn.DigsInOnArrivingAtABurrow", TestFlags)
bool FCrabPawnBurrowOnArrivalTest::RunTest(const FString& Parameters)
{
	FRig Rig(-700.f, -520.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	const int32 Target = 1;
	Rig.Crab->SetMoveTarget(Rig.Beach->GetBurrows()[Target].Location, Target);
	TestFalse(TEXT("not there yet"), Rig.Crab->IsInBurrow());
	Rig.World.TickSeconds(2.5f);
	TestTrue(TEXT("dug in on arrival"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("in the burrow it was sent to"), Rig.Crab->GetCurrentBurrow(), Target);
	TestFalse(TEXT("the walking target is spent"), Rig.Crab->HasMoveTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnFloodOutTest, "CrabSim.Pawn.FloodedOutOfABurrowByTheTide", TestFlags)
bool FCrabPawnFloodOutTest::RunTest(const FString& Parameters)
{
	FRig Rig(1900.f, -900.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);

	TestTrue(TEXT("dug into the lowest burrow at low tide"), Rig.Crab->EnterBurrow(LowBurrow));
	Rig.World.TickSeconds(1.f);
	TestTrue(TEXT("safe inside"), Rig.Crab->IsInBurrow());

	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickN(2, 1.f / 60.f);
	TestFalse(TEXT("forced out by the flood"), Rig.Crab->IsInBurrow());
	TestTrue(TEXT("with a message"), Rig.Crab->GetMessage().Contains(TEXT("Flooded")));
	TestTrue(TEXT("shown at full strength"), Rig.Crab->GetMessageAlpha() > 0.99f);

	// The surge advice that follows must not talk over the news.
	Rig.World.TickSeconds(0.5f);
	TestTrue(TEXT("still the flood message half a second on"), Rig.Crab->GetMessage().Contains(TEXT("Flooded")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnHighBurrowSafeTest, "CrabSim.Pawn.HighBurrowIsSafeAllTideLong", TestFlags)
bool FCrabPawnHighBurrowSafeTest::RunTest(const FString& Parameters)
{
	FRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestTrue(TEXT("dug in"), Rig.Crab->EnterBurrow(HighBurrow));
	Rig.Beach->SetTideClock(HighTide - 2.f);
	Rig.World.TickSeconds(4.f);
	TestTrue(TEXT("still in the high burrow at the top of the tide"), Rig.Crab->IsInBurrow());
	TestEqual(TEXT("never swept"), Rig.Crab->GetSweptCount(), 0);
	TestNearlyEqual(TEXT("grip intact"), Rig.Crab->GetGrip(), 1.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDashLeavesBurrowTest, "CrabSim.Pawn.DashDigsOutOfABurrow", TestFlags)
bool FCrabPawnDashLeavesBurrowTest::RunTest(const FString& Parameters)
{
	FRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	Rig.Crab->EnterBurrow(HighBurrow);
	Rig.World.TickSeconds(0.6f);
	TestTrue(TEXT("dash from underground"), Rig.Crab->TryDash(Rig.Crab->GetActorLocation() + FVector(0.f, 500.f, 0.f)));
	TestFalse(TEXT("out of the burrow"), Rig.Crab->IsInBurrow());
	Rig.World.TickSeconds(0.4f);
	TestTrue(TEXT("and away"), Speed(*Rig.Crab) > 0.f || Rig.Crab->GetActorLocation().Y > 350.f + 60.f);
	return true;
}

// --- The tide -------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnDryKeepsGripTest, "CrabSim.Pawn.DrySandCostsNothing", TestFlags)
bool FCrabPawnDryKeepsGripTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Rig.Beach->SetTideClock(LowTide);
	Rig.World.TickSeconds(3.f);
	TestNearlyEqual(TEXT("no water"), Rig.Crab->GetWaterDepth(), 0.f, 1e-3f);
	TestNearlyEqual(TEXT("grip is full"), Rig.Crab->GetGrip(), 1.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnSurgeDrainsGripTest, "CrabSim.Pawn.DeepWaterDrainsGripThenSweepsTheCrabAway", TestFlags)
bool FCrabPawnSurgeDrainsGripTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickSeconds(2.f);
	TestTrue(TEXT("the crab is in the surge"), Rig.Crab->GetWaterDepth() > CrabSurvival::SurgeDepth);
	TestTrue(*FString::Printf(TEXT("grip is falling (%.2f)"), Rig.Crab->GetGrip()), Rig.Crab->GetGrip() < 0.95f);
	TestTrue(TEXT("and it is told so"), Rig.Crab->GetMessage().Contains(TEXT("tide")));
	TestEqual(TEXT("not swept yet"), Rig.Crab->GetSweptCount(), 0);

	// Stay put long enough and the sea takes the crab.
	Rig.World.TickSeconds(14.f);
	TestTrue(*FString::Printf(TEXT("swept out (%d)"), Rig.Crab->GetSweptCount()), Rig.Crab->GetSweptCount() >= 1);

	// It washes up beside the highest burrow, on dry sand, with some grip back.
	const FCrabBurrow& Haven = Rig.Beach->GetBurrows()[Rig.Beach->FindSafestBurrow()];
	TestTrue(*FString::Printf(TEXT("washed up by the dunes (%.0f from the haven)"), FVector::Dist2D(Rig.Crab->GetActorLocation(), Haven.Location)),
		FVector::Dist2D(Rig.Crab->GetActorLocation(), Haven.Location) < 500.f);
	TestNearlyEqual(TEXT("out of the water"), Rig.Crab->GetWaterDepth(), 0.f, 1e-3f);
	TestTrue(TEXT("with grip left"), Rig.Crab->GetGrip() > 0.f);
	TestTrue(TEXT("and a message"), Rig.Crab->GetMessage().Contains(TEXT("Swept")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnGripRegainsTest, "CrabSim.Pawn.GripRecoversOnDrySand", TestFlags)
bool FCrabPawnGripRegainsTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	Rig.Beach->SetTideClock(HighTide);
	Rig.World.TickSeconds(3.f);
	const float Lowest = Rig.Crab->GetGrip();
	TestTrue(TEXT("grip dropped"), Lowest < 0.9f);

	// Teleport out of the water and let it recover.
	const float DuneX = -3300.f;
	Rig.Crab->SetActorLocation(FVector(DuneX, 0.f, Rig.Beach->GetGroundHeight(DuneX, 0.f) + 70.f), false, nullptr, ETeleportType::TeleportPhysics);
	Rig.World.TickSeconds(2.f);
	TestNearlyEqual(TEXT("dry"), Rig.Crab->GetWaterDepth(), 0.f, 1e-3f);
	TestTrue(*FString::Printf(TEXT("grip came back (%.2f to %.2f)"), Lowest, Rig.Crab->GetGrip()), Rig.Crab->GetGrip() > Lowest + 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnBurrowRestoresGripTest, "CrabSim.Pawn.GripRecoversFastestInABurrow", TestFlags)
bool FCrabPawnBurrowRestoresGripTest::RunTest(const FString& Parameters)
{
	// Same start for both: grip knocked down in the surge, then either out on the sand or dug in.
	auto RunOne = [](bool bDigIn) -> float
	{
		FRig Rig;
		if (!Rig.IsValid())
		{
			return -1.f;
		}
		Settle(Rig.World);
		Rig.Beach->SetTideClock(HighTide);
		Rig.World.TickSeconds(4.f);
		const float DuneX = -2450.f;
		Rig.Crab->SetActorLocation(FVector(DuneX, 350.f, Rig.Beach->GetGroundHeight(DuneX, 350.f) + 70.f), false, nullptr, ETeleportType::TeleportPhysics);
		Rig.World.TickSeconds(0.3f);
		if (bDigIn)
		{
			Rig.Crab->EnterBurrow(HighBurrow);
		}
		const float Before = Rig.Crab->GetGrip();
		Rig.World.TickSeconds(1.5f);
		return Rig.Crab->GetGrip() - Before;
	};

	const float InBurrow = RunOne(true);
	const float InOpen = RunOne(false);
	TestTrue(TEXT("both runs worked"), InBurrow >= 0.f && InOpen >= 0.f);
	TestTrue(*FString::Printf(TEXT("burrow recovery %.2f beats open-sand recovery %.2f"), InBurrow, InOpen), InBurrow > InOpen);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnWaterSlowsTest, "CrabSim.Pawn.WadingSlowsTheCrab", TestFlags)
bool FCrabPawnWaterSlowsTest::RunTest(const FString& Parameters)
{
	float DrySpeed = 0.f;
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(0.f, 2500.f, 0.f));
		Rig.World.TickSeconds(0.8f);
		DrySpeed = Rig.Crab->GetCharacterMovement()->MaxWalkSpeed;
	}
	float WetSpeed = 0.f;
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Beach->SetTideClock(HighTide);
		Rig.Crab->SetMoveTarget(Rig.Crab->GetActorLocation() + FVector(0.f, 2500.f, 0.f));
		Rig.World.TickSeconds(0.8f);
		WetSpeed = Rig.Crab->GetCharacterMovement()->MaxWalkSpeed;
	}
	TestTrue(*FString::Printf(TEXT("dry top speed %.0f is full scuttle"), DrySpeed), DrySpeed > 400.f);
	TestTrue(*FString::Printf(TEXT("wet top speed %.0f is well under it"), WetSpeed), WetSpeed < 0.75f * DrySpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnSurgeDirectionTest, "CrabSim.Pawn.RisingTideShovesUpTheBeachFallingTideDragsOut", TestFlags)
bool FCrabPawnSurgeDirectionTest::RunTest(const FString& Parameters)
{
	float RisingShift = 0.f;
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Beach->SetTideClock(RisingSurge);
		TestTrue(TEXT("the tide is rising"), Rig.Beach->IsTideRising());
		const float StartX = Rig.Crab->GetActorLocation().X;
		Rig.World.TickSeconds(1.5f);
		RisingShift = Rig.Crab->GetActorLocation().X - StartX;
	}
	float FallingShift = 0.f;
	{
		FRig Rig;
		if (!TestTrue(TEXT("rig"), Rig.IsValid()))
		{
			return false;
		}
		Settle(Rig.World);
		Rig.Beach->SetTideClock(FallingSurge);
		TestFalse(TEXT("the tide is falling"), Rig.Beach->IsTideRising());
		const float StartX = Rig.Crab->GetActorLocation().X;
		Rig.World.TickSeconds(1.5f);
		FallingShift = Rig.Crab->GetActorLocation().X - StartX;
	}
	TestTrue(*FString::Printf(TEXT("a rising tide pushes toward the dunes, -X (%.0f)"), RisingShift), RisingShift < -20.f);
	TestTrue(*FString::Printf(TEXT("a falling tide pulls toward the sea, +X (%.0f)"), FallingShift), FallingShift > 20.f);
	return true;
}

// --- Feedback ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnMessageFadesTest, "CrabSim.Pawn.MessagesFadeOut", TestFlags)
bool FCrabPawnMessageFadesTest::RunTest(const FString& Parameters)
{
	FRig Rig(-2450.f, 350.f);
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	Settle(Rig.World);
	TestNearlyEqual(TEXT("no message at first"), Rig.Crab->GetMessageAlpha(), 0.f, 1e-4f);

	Rig.Crab->EnterBurrow(HighBurrow);
	TestFalse(TEXT("there is a message"), Rig.Crab->GetMessage().IsEmpty());
	TestNearlyEqual(TEXT("fresh at full strength"), Rig.Crab->GetMessageAlpha(), 1.f, 1e-4f);

	Rig.World.TickSeconds(2.2f);
	const float Fading = Rig.Crab->GetMessageAlpha();
	TestTrue(*FString::Printf(TEXT("fading near the end (%.2f)"), Fading), Fading < 1.f);
	Rig.World.TickSeconds(1.5f);
	TestNearlyEqual(TEXT("gone"), Rig.Crab->GetMessageAlpha(), 0.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPawnFallbackVisualTest, "CrabSim.Pawn.FallsBackToShapesWithoutArt", TestFlags)
bool FCrabPawnFallbackVisualTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	if (!TestTrue(TEXT("rig"), Rig.IsValid()))
	{
		return false;
	}
	// This checks the no-art path, so it only means something while /Game/Crab/Meshes/SK_FiddlerCrab is absent.
	if (Rig.Crab->IsUsingSkeletalMesh())
	{
		AddInfo(TEXT("the skeletal crab is present, so the fallback path is not exercised here"));
		return true;
	}
	TestFalse(TEXT("using the shape-built crab"), Rig.Crab->IsUsingSkeletalMesh());
	Settle(Rig.World);
	TestEqual(TEXT("still idles"), Rig.Crab->GetAnimState(), ECrabAnim::Idle);
	return true;
}
