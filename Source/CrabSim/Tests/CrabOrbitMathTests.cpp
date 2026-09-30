// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabHudMath.h"
#include "CrabOrbitMath.h"

namespace UE::CrabSim::Tests::OrbitMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** What a whole right press did: the yaw it added and whether it came up as a click. */
	struct FPress
	{
		float Yaw = 0.f;
		float Pitch = 0.f;
		bool bClick = false;
		bool bOrbited = false;
	};

	/** Presses at the first pointer, moves through the rest with the button down a frame each, then lets go at the last. */
	inline FPress RunPress(CrabOrbit::FDrag& Drag, TArrayView<const FVector2D> Pointers, const CrabOrbit::FTuning& Tuning = CrabOrbit::FTuning())
	{
		FPress Press;
		for (const FVector2D& Pointer : Pointers)
		{
			const CrabOrbit::FStep Step = Drag.Update(true, Pointer, Tuning);
			Press.Yaw += Step.YawDelta;
			Press.Pitch += Step.PitchDelta;
			Press.bClick |= Step.bClick;
			Press.bOrbited |= Drag.IsOrbiting();
		}
		const CrabOrbit::FStep Up = Drag.Update(false, Pointers.Last(), Tuning);
		Press.Yaw += Up.YawDelta;
		Press.bClick |= Up.bClick;
		return Press;
	}
}

using namespace UE::CrabSim::Tests::OrbitMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabOrbitClickDashesTest, "CrabSim.Orbit.ARightClickThatDoesNotMoveIsAClickNotAnOrbit", TestFlags)
bool FCrabOrbitClickDashesTest::RunTest(const FString& Parameters)
{
	CrabOrbit::FDrag Drag;
	const FVector2D At(500.0, 400.0);
	const FPress Still = RunPress(Drag, {At, At, At});
	TestTrue(TEXT("a still press comes up as a click"), Still.bClick);
	TestEqual(TEXT("and turns nothing"), Still.Yaw, 0.f);

	const FPress Wobble = RunPress(Drag, {At, At + FVector2D(4.0, -3.0), At + FVector2D(-6.0, 5.0), At + FVector2D(7.0, 0.0)});
	TestTrue(TEXT("a hand that wobbles under the start distance still clicks"), Wobble.bClick);
	TestEqual(TEXT("and still turns nothing"), Wobble.Yaw, 0.f);

	TArray<FVector2D> Long;
	Long.Init(At, 600);
	const FPress Held = RunPress(Drag, Long);
	TestTrue(TEXT("held still for ten seconds at 60 fps, it is still a click: no timing window"), Held.bClick);

	TestFalse(TEXT("no click while the button is up"), Drag.Update(false, At, CrabOrbit::FTuning()).bClick);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabOrbitDragTurnsTest, "CrabSim.Orbit.ARightDragTurnsTheViewByTheSidewaysTravelAndDoesNotClick", TestFlags)
bool FCrabOrbitDragTurnsTest::RunTest(const FString& Parameters)
{
	CrabOrbit::FDrag Drag;
	CrabOrbit::FTuning Tuning;
	const FVector2D At(500.0, 400.0);
	const FPress Right = RunPress(Drag, {At, At + FVector2D(5.0, 0.0), At + FVector2D(40.0, 3.0), At + FVector2D(300.0, -8.0)}, Tuning);
	TestTrue(TEXT("past the start distance it orbits"), Right.bOrbited);
	TestFalse(TEXT("and does not click when it comes up"), Right.bClick);
	TestNearlyEqual(TEXT("every pixel of sideways travel counts, the first few too: no jump"), Right.Yaw, 300.f * Tuning.DegreesPerPixel, 1e-3f);

	const FPress Back = RunPress(Drag, {At, At + FVector2D(-150.0, 0.0), At + FVector2D(-100.0, 0.0)}, Tuning);
	TestNearlyEqual(TEXT("left turns the other way, and coming back part way turns back"), Back.Yaw, -100.f * Tuning.DegreesPerPixel, 1e-3f);

	const FPress Up = RunPress(Drag, {At, At + FVector2D(0.0, -80.0)}, Tuning);
	TestTrue(TEXT("a drag straight up is still not a click"), Up.bOrbited && !Up.bClick);
	TestEqual(TEXT("but turns nothing: only sideways travel orbits"), Up.Yaw, 0.f);

	CrabOrbit::FTuning Slow;
	Slow.DegreesPerPixel = 0.1f;
	TestNearlyEqual(TEXT("the rate is tunable"), RunPress(Drag, {At, At + FVector2D(300.0, 0.0)}, Slow).Yaw, 30.f, 1e-3f);

	const FPress Next = RunPress(Drag, {At, At});
	TestTrue(TEXT("the next press starts fresh: a click again"), Next.bClick && !Next.bOrbited);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabOrbitPitchTest, "CrabSim.Orbit.UpAndDownTravelSwingsTheCameraLowerOrHigherWithinItsClamp", TestFlags)
bool FCrabOrbitPitchTest::RunTest(const FString& Parameters)
{
	CrabOrbit::FDrag Drag;
	CrabOrbit::FTuning Tuning;
	const FVector2D At(640.0, 400.0);
	const FPress Up = RunPress(Drag, {At, At + FVector2D(0.0, -30.0), At + FVector2D(0.0, -100.0)}, Tuning);
	TestNearlyEqual(TEXT("pointer up raises the pitch toward level: the camera comes down, looking across"), Up.Pitch, 100.f * Tuning.PitchDegreesPerPixel, 1e-3f);
	TestEqual(TEXT("and turns no yaw"), Up.Yaw, 0.f);

	const FPress Diagonal = RunPress(Drag, {At, At + FVector2D(200.0, 100.0)}, Tuning);
	TestNearlyEqual(TEXT("a diagonal drag turns both: right turns the view right"), Diagonal.Yaw, 200.f * Tuning.DegreesPerPixel, 1e-3f);
	TestNearlyEqual(TEXT("and down lifts the camera, looking down"), Diagonal.Pitch, -100.f * Tuning.PitchDegreesPerPixel, 1e-3f);

	CrabOrbit::FTuning Inverted = Tuning;
	Inverted.PitchDegreesPerPixel = -Tuning.PitchDegreesPerPixel;
	TestNearlyEqual(TEXT("a negative pitch rate swaps up and down"), RunPress(Drag, {At, At + FVector2D(0.0, -100.0)}, Inverted).Pitch, -100.f * Tuning.PitchDegreesPerPixel, 1e-3f);

	TestEqual(TEXT("the clamp stops short of straight down"), CrabOrbit::ClampPitch(-120.f), CrabOrbit::MinPitch);
	TestEqual(TEXT("and above the sand"), CrabOrbit::ClampPitch(20.f), CrabOrbit::MaxPitch);
	TestEqual(TEXT("and leaves the default pitch alone"), CrabOrbit::ClampPitch(-38.f), -38.f);
	TestTrue(TEXT("the clamp looks down at both ends"), CrabOrbit::MinPitch > -90.f && CrabOrbit::MaxPitch < 0.f);

	TestNearlyEqual(TEXT("the default pitch squashes the ground like the fixed camera did"), CrabOrbit::GroundForeshortening(-38.f), CrabHud::GroundForeshortening, 0.01f);
	TestTrue(TEXT("looking straight down it is not squashed"), CrabOrbit::GroundForeshortening(-85.f) > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabOrbitScreenToWorldTest, "CrabSim.Orbit.UpTheScreenIsTheCameraForwardAtAnyYaw", TestFlags)
bool FCrabOrbitScreenToWorldTest::RunTest(const FString& Parameters)
{
	const FVector2D Up(0.0, 1.0);
	const FVector2D Right(1.0, 0.0);
	TestTrue(TEXT("at yaw 0, up is +X, toward the sea"), CrabOrbit::ScreenToWorld(Up, 0.f).Equals(FVector2D(1.0, 0.0), 1e-4));
	TestTrue(TEXT("and right is +Y"), CrabOrbit::ScreenToWorld(Right, 0.f).Equals(FVector2D(0.0, 1.0), 1e-4));
	TestTrue(TEXT("orbited to yaw 90, up is +Y"), CrabOrbit::ScreenToWorld(Up, 90.f).Equals(FVector2D(0.0, 1.0), 1e-4));
	TestTrue(TEXT("and right is -X"), CrabOrbit::ScreenToWorld(Right, 90.f).Equals(FVector2D(-1.0, 0.0), 1e-4));
	TestTrue(TEXT("at yaw 180, up is toward the dunes"), CrabOrbit::ScreenToWorld(Up, 180.f).Equals(FVector2D(-1.0, 0.0), 1e-4));

	const FVector2D Stick(0.6, -0.3);
	for (float Yaw : {0.f, 37.f, -120.f, 179.f})
	{
		TestTrue(*FString::Printf(TEXT("WorldToCamera undoes ScreenToWorld at yaw %.0f"), Yaw),
			CrabOrbit::WorldToCamera(CrabOrbit::ScreenToWorld(Stick, Yaw), Yaw).Equals(Stick, 1e-4));
		TestNearlyEqual(TEXT("and keeps the stick's length"), static_cast<float>(CrabOrbit::ScreenToWorld(Stick, Yaw).Size()), static_cast<float>(Stick.Size()), 1e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabOrbitGullArrowTest, "CrabSim.Orbit.TheGullArrowTurnsWithTheCamera", TestFlags)
bool FCrabOrbitGullArrowTest::RunTest(const FString& Parameters)
{
	const FVector2D Crab(100.0, -50.0);
	TestTrue(TEXT("orbited to yaw 90, a gull at +Y is up the screen"),
		CrabHud::GullArrowDirection(Crab, Crab + FVector2D(0.0, 900.0), 90.f).Equals(FVector2D(0.0, -1.0), 1e-3));
	TestTrue(TEXT("and one toward the sea (+X) is on the left"),
		CrabHud::GullArrowDirection(Crab, Crab + FVector2D(900.0, 0.0), 90.f).Equals(FVector2D(-1.0, 0.0), 1e-3));
	TestTrue(TEXT("at yaw 180 the sea is behind the camera: down"),
		CrabHud::GullArrowDirection(Crab, Crab + FVector2D(900.0, 0.0), 180.f).Equals(FVector2D(0.0, 1.0), 1e-3));
	return true;
}
