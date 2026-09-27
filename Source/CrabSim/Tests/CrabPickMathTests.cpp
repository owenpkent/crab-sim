// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabPickMath.h"

namespace UE::CrabSim::Tests::PickMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/**
	 * A camera looking along +X over flat ground: +Y is right, +X is up the screen, foreshortened, and everything
	 * shrinks with PixelsPerUu (a far camera has a small one). Middle of a 1280x720 view is the origin.
	 */
	struct FFlatCamera
	{
		float PixelsPerUu = 0.3f;
		bool bSeesEverything = true;

		bool operator()(const FVector& World, FVector2D& OutPixel) const
		{
			OutPixel = FVector2D(640.f + World.Y * PixelsPerUu, 360.f - World.X * PixelsPerUu * 0.6f);
			return bSeesEverything;
		}
	};
}

using namespace UE::CrabSim::Tests::PickMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickMinimumTest, "CrabSim.Pick.AZoneIsNeverSmallerThanNinetyBySixtyPixels", TestFlags)
bool FCrabPickMinimumTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("90 px wide at 720p"), CrabPick::MinZoneWidth, 90.f, 1e-6f);
	TestNearlyEqual(TEXT("60 px tall"), CrabPick::MinZoneHeight, 60.f, 1e-6f);

	const FVector2D Tiny = CrabPick::ZoneHalfAxes(FVector2D(5.f, 2.f), 1.f);
	TestNearlyEqual(TEXT("a tiny projection gets half the width"), static_cast<float>(Tiny.X), 45.f, 1e-4f);
	TestNearlyEqual(TEXT("and half the height"), static_cast<float>(Tiny.Y), 30.f, 1e-4f);

	const FVector2D Big = CrabPick::ZoneHalfAxes(FVector2D(120.f, 80.f), 1.f);
	TestNearlyEqual(TEXT("a big projection keeps its own width: the larger wins"), static_cast<float>(Big.X), 120.f, 1e-4f);
	TestNearlyEqual(TEXT("and height"), static_cast<float>(Big.Y), 80.f, 1e-4f);

	const FVector2D Mixed = CrabPick::ZoneHalfAxes(FVector2D(120.f, 5.f), 1.f);
	TestTrue(TEXT("each axis takes its own larger: wide by the world, tall by the minimum"), Mixed.X == 120.f && Mixed.Y == 30.f);

	const FVector2D Scaled = CrabPick::ZoneHalfAxes(FVector2D::ZeroVector, 2.5f);
	TestNearlyEqual(TEXT("the minimum grows with the HUD scale"), static_cast<float>(Scaled.X), 112.5f, 1e-4f);
	TestNearlyEqual(TEXT("both ways"), static_cast<float>(Scaled.Y), 75.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickEllipseTest, "CrabSim.Pick.AZoneIsAnEllipseNotABox", TestFlags)
bool FCrabPickEllipseTest::RunTest(const FString& Parameters)
{
	const FVector2D Centre(500.f, 300.f);
	const FVector2D Half(45.f, 30.f);
	TestNearlyEqual(TEXT("the middle is 0"), CrabPick::EllipseDistance(Centre, Centre, Half), 0.f, 1e-5f);
	TestNearlyEqual(TEXT("the left and right tips are on the edge"), CrabPick::EllipseDistance(Centre + FVector2D(45.f, 0.f), Centre, Half), 1.f, 1e-5f);
	TestNearlyEqual(TEXT("the top and bottom tips too"), CrabPick::EllipseDistance(Centre + FVector2D(0.f, -30.f), Centre, Half), 1.f, 1e-5f);
	TestTrue(TEXT("just inside a tip is inside"), CrabPick::EllipseDistance(Centre + FVector2D(44.f, 0.f), Centre, Half) < 1.f);
	TestTrue(TEXT("just outside a tip is outside"), CrabPick::EllipseDistance(Centre + FVector2D(46.f, 0.f), Centre, Half) > 1.f);
	TestTrue(TEXT("a corner of the 90x60 box is outside: it is not a box"), CrabPick::EllipseDistance(Centre + FVector2D(40.f, 26.f), Centre, Half) > 1.f);
	TestTrue(TEXT("a point that far along both axes is inside"), CrabPick::EllipseDistance(Centre + FVector2D(30.f, 18.f), Centre, Half) < 1.f);
	TestTrue(TEXT("a zero-size ellipse does not divide by zero"), FMath::IsFinite(CrabPick::EllipseDistance(Centre + FVector2D(3.f, 3.f), Centre, FVector2D::ZeroVector)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickProjectTest, "CrabSim.Pick.TheWorldRadiusIsProjectedOntoTheScreen", TestFlags)
bool FCrabPickProjectTest::RunTest(const FString& Parameters)
{
	const FFlatCamera Camera{0.3f, true};
	FVector2D Centre;
	FVector2D Half;
	TestTrue(TEXT("a visible point projects"), CrabPick::ProjectZone(Camera, FVector(-200.f, 100.f, 0.f), 100.f, Centre, Half));
	TestNearlyEqual(TEXT("its middle lands where the camera puts it (across)"), static_cast<float>(Centre.X), 640.f + 100.f * 0.3f, 1e-3f);
	TestNearlyEqual(TEXT("and up the screen"), static_cast<float>(Centre.Y), 360.f + 200.f * 0.3f * 0.6f, 1e-3f);
	TestNearlyEqual(TEXT("100 uu across is 30 px"), static_cast<float>(Half.X), 30.f, 1e-3f);
	TestNearlyEqual(TEXT("100 uu up the ground is foreshortened to 18 px"), static_cast<float>(Half.Y), 18.f, 1e-3f);

	const FFlatCamera Blind{0.3f, false};
	TestFalse(TEXT("a point the camera cannot see does not project"), CrabPick::ProjectZone(Blind, FVector::ZeroVector, 100.f, Centre, Half));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickRangeTest, "CrabSim.Pick.TheMinimumHoldsAtAnyCameraRange", TestFlags)
bool FCrabPickRangeTest::RunTest(const FString& Parameters)
{
	// A burrow's 100 uu and a patch's 200 uu, seen from close, from the start range and from far off.
	const float Ranges[] = {1.5f, 0.6f, 0.3f, 0.1f, 0.02f};
	for (const float PixelsPerUu : Ranges)
	{
		const FFlatCamera Camera{PixelsPerUu, true};
		for (const float WorldRadius : {100.f, 200.f})
		{
			FVector2D Centre;
			FVector2D Projected;
			CrabPick::ProjectZone(Camera, FVector(300.f, 200.f, 0.f), WorldRadius, Centre, Projected);
			const FVector2D Zone = CrabPick::ZoneHalfAxes(Projected, 1.f);
			const FString Where = FString::Printf(TEXT("%.2f px/uu, radius %.0f"), PixelsPerUu, WorldRadius);
			TestTrue(*(Where + TEXT(": at least 90 px wide")), Zone.X * 2.f >= 90.f - 1e-3f);
			TestTrue(*(Where + TEXT(": at least 60 px tall")), Zone.Y * 2.f >= 60.f - 1e-3f);
			TestTrue(*(Where + TEXT(": never smaller than the world radius shows")), Zone.X >= Projected.X - 1e-3f && Zone.Y >= Projected.Y - 1e-3f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabPickScoreTest, "CrabSim.Pick.ScoreRanksTargetsByHowDeepInTheZoneAClickIs", TestFlags)
bool FCrabPickScoreTest::RunTest(const FString& Parameters)
{
	const FFlatCamera Camera{0.1f, true};
	const FVector Burrow(0.f, 0.f, 0.f);
	float Score = -1.f;
	FVector2D Middle;
	Camera(Burrow, Middle);

	TestTrue(TEXT("a visible target scores"), CrabPick::ZoneScore(Camera, Burrow, 100.f, 1.f, Middle, Score));
	TestNearlyEqual(TEXT("a click on its middle scores 0"), Score, 0.f, 1e-5f);
	CrabPick::ZoneScore(Camera, Burrow, 100.f, 1.f, Middle + FVector2D(44.f, 0.f), Score);
	TestTrue(TEXT("44 px across is inside at 0.1 px/uu, where the world radius is 10 px"), Score < 1.f);
	CrabPick::ZoneScore(Camera, Burrow, 100.f, 1.f, Middle + FVector2D(46.f, 0.f), Score);
	TestTrue(TEXT("46 px across is outside"), Score > 1.f);
	CrabPick::ZoneScore(Camera, Burrow, 100.f, 1.f, Middle + FVector2D(0.f, 29.f), Score);
	TestTrue(TEXT("29 px down is inside"), Score < 1.f);
	CrabPick::ZoneScore(Camera, Burrow, 100.f, 1.f, Middle + FVector2D(0.f, 31.f), Score);
	TestTrue(TEXT("31 px down is outside"), Score > 1.f);

	float Untouched = 7.f;
	const FFlatCamera Blind{0.1f, false};
	TestFalse(TEXT("an unseen target does not score"), CrabPick::ZoneScore(Blind, Burrow, 100.f, 1.f, Middle, Untouched));
	TestNearlyEqual(TEXT("and leaves the score alone"), Untouched, 7.f, 1e-6f);
	return true;
}
