// SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#include "CrabHudMath.h"

namespace UE::CrabSim::Tests::HudMath
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

using namespace UE::CrabSim::Tests::HudMath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudDigButtonSizeTest, "CrabSim.Hud.DigButtonIsBigAtEverySize", TestFlags)
bool FCrabHudDigButtonSizeTest::RunTest(const FString& Parameters)
{
	const FIntPoint Views[] = {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160), FIntPoint(800, 450)};
	for (const FIntPoint& View : Views)
	{
		const FBox2D Rect = CrabHud::DigButtonRect(View.X, View.Y);
		const FVector2D Size = Rect.GetSize();
		TestTrue(*FString::Printf(TEXT("at %dx%d the button is at least 120 px wide (%.0f)"), View.X, View.Y, Size.X), Size.X >= 120.f);
		TestTrue(*FString::Printf(TEXT("and 120 px tall (%.0f)"), Size.Y), Size.Y >= 120.f);
		TestNearlyEqual(TEXT("and square"), static_cast<float>(Size.X), static_cast<float>(Size.Y), 0.01f);
	}
	const FVector2D Small = CrabHud::DigButtonRect(1280, 720).GetSize();
	TestNearlyEqual(TEXT("at 1280x720 it is 140 px"), static_cast<float>(Small.X), 140.f, 0.01f);
	TestTrue(TEXT("it grows with the window"), CrabHud::DigButtonRect(3840, 2160).GetSize().X > Small.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudDigButtonPlaceTest, "CrabSim.Hud.DigButtonSitsBottomRightInsideTheView", TestFlags)
bool FCrabHudDigButtonPlaceTest::RunTest(const FString& Parameters)
{
	const FIntPoint Views[] = {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(3840, 2160)};
	for (const FIntPoint& View : Views)
	{
		const FBox2D Rect = CrabHud::DigButtonRect(View.X, View.Y);
		TestTrue(*FString::Printf(TEXT("at %dx%d it is inside the view"), View.X, View.Y),
			Rect.Min.X >= 0.f && Rect.Min.Y >= 0.f && Rect.Max.X <= View.X && Rect.Max.Y <= View.Y);
		TestTrue(TEXT("in the right third"), Rect.GetCenter().X > View.X * 0.66f);
		TestTrue(TEXT("in the bottom half"), Rect.GetCenter().Y > View.Y * 0.5f);
		TestTrue(TEXT("clear of the hint line at the very bottom"), Rect.Max.Y < View.Y - 20.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudDigButtonHitTest, "CrabSim.Hud.ClicksOnTheDigButtonAreForgiving", TestFlags)
bool FCrabHudDigButtonHitTest::RunTest(const FString& Parameters)
{
	const FBox2D Rect = CrabHud::DigButtonRect(1280, 720);
	TestTrue(TEXT("the centre hits"), CrabHud::HitsDigButton(1280, 720, Rect.GetCenter()));
	TestTrue(TEXT("each corner hits"), CrabHud::HitsDigButton(1280, 720, Rect.Min) && CrabHud::HitsDigButton(1280, 720, Rect.Max));
	TestTrue(TEXT("a few pixels outside still hits"), CrabHud::HitsDigButton(1280, 720, Rect.Min - FVector2D(6.f, 6.f)));
	TestFalse(TEXT("far outside does not"), CrabHud::HitsDigButton(1280, 720, Rect.Min - FVector2D(60.f, 60.f)));
	TestFalse(TEXT("the middle of the screen does not"), CrabHud::HitsDigButton(1280, 720, FVector2D(640.f, 360.f)));
	TestFalse(TEXT("the grip bar does not"), CrabHud::HitsDigButton(1280, 720, FVector2D(640.f, 650.f)));
	TestFalse(TEXT("the tide gauge does not"), CrabHud::HitsDigButton(1280, 720, FVector2D(45.f, 250.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudScaleTest, "CrabSim.Hud.ScaleFollowsTheWindowHeightWithinLimits", TestFlags)
bool FCrabHudScaleTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("720p is scale 1"), CrabHud::ScaleForHeight(720.f), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("1080p is scale 1.25"), CrabHud::ScaleForHeight(1080.f), 1.25f, 1e-4f);
	TestNearlyEqual(TEXT("tiny windows do not shrink it further"), CrabHud::ScaleForHeight(100.f), 1.f, 1e-4f);
	TestNearlyEqual(TEXT("huge windows are capped"), CrabHud::ScaleForHeight(100000.f), 2.5f * 1.25f, 1e-4f);
	return true;
}
