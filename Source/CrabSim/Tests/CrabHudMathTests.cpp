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

namespace
{
	const FIntPoint MoltViews[] = {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160), FIntPoint(800, 450)};

	bool Overlaps(const FBox2D& A, const FBox2D& B)
	{
		return A.Intersect(B);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltButtonSizeTest, "CrabSim.Hud.TheMoltButtonIsTheSizeOfTheDigButton", TestFlags)
bool FCrabHudMoltButtonSizeTest::RunTest(const FString& Parameters)
{
	for (const FIntPoint& View : MoltViews)
	{
		const FVector2D Molt = CrabHud::MoltButtonRect(View.X, View.Y).GetSize();
		const FVector2D Dig = CrabHud::DigButtonRect(View.X, View.Y).GetSize();
		TestNearlyEqual(*FString::Printf(TEXT("at %dx%d as wide as the dig button"), View.X, View.Y), static_cast<float>(Molt.X), static_cast<float>(Dig.X), 0.01f);
		TestNearlyEqual(TEXT("and as tall"), static_cast<float>(Molt.Y), static_cast<float>(Dig.Y), 0.01f);
		TestTrue(TEXT("at least 120 px"), Molt.X >= 120.f && Molt.Y >= 120.f);
	}
	TestNearlyEqual(TEXT("140 px at 720p"), static_cast<float>(CrabHud::MoltButtonRect(1280, 720).GetSize().X), 140.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltButtonPlaceTest, "CrabSim.Hud.TheMoltButtonSitsAboveTheDigButtonAndOverlapsNothing", TestFlags)
bool FCrabHudMoltButtonPlaceTest::RunTest(const FString& Parameters)
{
	for (const FIntPoint& View : MoltViews)
	{
		const FBox2D Molt = CrabHud::MoltButtonRect(View.X, View.Y);
		const FBox2D Dig = CrabHud::DigButtonRect(View.X, View.Y);
		const float Margin = CrabHud::DigButtonHitMargin * CrabHud::ScaleForHeight(View.Y);
		TestTrue(*FString::Printf(TEXT("at %dx%d it is inside the view"), View.X, View.Y),
			Molt.Min.X >= 0.f && Molt.Min.Y >= 0.f && Molt.Max.X <= View.X && Molt.Max.Y <= View.Y);
		TestTrue(TEXT("above the dig button"), Molt.Max.Y < Dig.Min.Y);
		TestNearlyEqual(TEXT("lined up with it on the right"), static_cast<float>(Molt.Max.X), static_cast<float>(Dig.Max.X), 0.01f);
		TestFalse(TEXT("not touching the dig button"), Overlaps(Molt, Dig));
		TestFalse(TEXT("and their hit areas do not touch either"), Overlaps(Molt.ExpandBy(Margin), Dig.ExpandBy(Margin)));
		TestTrue(TEXT("room between them for the dig button's reason line"), Dig.Min.Y - Molt.Max.Y >= 40.f * CrabHud::ScaleForHeight(View.Y));
		TestTrue(TEXT("in the right third"), Molt.GetCenter().X > View.X * 0.66f);
		TestTrue(TEXT("with room above it for its own reason line"), Molt.Min.Y > 40.f * CrabHud::ScaleForHeight(View.Y));
		TestFalse(TEXT("clear of the food bar"), Overlaps(Molt.ExpandBy(Margin), CrabHud::FoodBarRect(View.X, View.Y).ExpandBy(12.f)));
		TestFalse(TEXT("clear of the grip bar"), Overlaps(Molt.ExpandBy(Margin), CrabHud::GripBarRect(View.X, View.Y).ExpandBy(12.f)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltButtonHitTest, "CrabSim.Hud.ClicksOnTheMoltButtonAreForgiving", TestFlags)
bool FCrabHudMoltButtonHitTest::RunTest(const FString& Parameters)
{
	const FBox2D Rect = CrabHud::MoltButtonRect(1280, 720);
	TestTrue(TEXT("the centre hits"), CrabHud::HitsMoltButton(1280, 720, Rect.GetCenter()));
	TestTrue(TEXT("each corner hits"), CrabHud::HitsMoltButton(1280, 720, Rect.Min) && CrabHud::HitsMoltButton(1280, 720, Rect.Max));
	TestTrue(TEXT("a few pixels outside still hits"), CrabHud::HitsMoltButton(1280, 720, Rect.Min - FVector2D(6.f, 6.f)));
	TestFalse(TEXT("far outside does not"), CrabHud::HitsMoltButton(1280, 720, Rect.Min - FVector2D(60.f, 60.f)));
	TestFalse(TEXT("the middle of the screen does not"), CrabHud::HitsMoltButton(1280, 720, FVector2D(640.f, 360.f)));
	TestFalse(TEXT("the dig button's middle is not the molt button"), CrabHud::HitsMoltButton(1280, 720, CrabHud::DigButtonRect(1280, 720).GetCenter()));
	TestFalse(TEXT("and the molt button's middle is not the dig button"), CrabHud::HitsDigButton(1280, 720, Rect.GetCenter()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudBarsTest, "CrabSim.Hud.TheBarsSitBottomMiddleSideBySide", TestFlags)
bool FCrabHudBarsTest::RunTest(const FString& Parameters)
{
	for (const FIntPoint& View : MoltViews)
	{
		const FBox2D Grip = CrabHud::GripBarRect(View.X, View.Y);
		const FBox2D Food = CrabHud::FoodBarRect(View.X, View.Y);
		TestNearlyEqual(*FString::Printf(TEXT("at %dx%d the grip bar is centred"), View.X, View.Y), static_cast<float>(Grip.GetCenter().X), View.X * 0.5f, 0.01f);
		TestTrue(TEXT("the food bar is on its right"), Food.Min.X > Grip.Max.X);
		TestNearlyEqual(TEXT("on the same row"), static_cast<float>(Food.Min.Y), static_cast<float>(Grip.Min.Y), 0.01f);
		TestTrue(TEXT("both are inside the view"), Grip.Min.X >= 0.f && Food.Max.X <= View.X && Food.Max.Y <= View.Y);
	}
	const FBox2D Grip = CrabHud::GripBarRect(1280, 720);
	TestNearlyEqual(TEXT("the grip bar is 300 px wide at 720p"), static_cast<float>(Grip.GetSize().X), 300.f, 0.01f);
	TestNearlyEqual(TEXT("the food bar 200"), static_cast<float>(CrabHud::FoodBarRect(1280, 720).GetSize().X), 200.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudMoltPipsTest, "CrabSim.Hud.ThreeMoltPipsSitAboveTheFoodBar", TestFlags)
bool FCrabHudMoltPipsTest::RunTest(const FString& Parameters)
{
	for (const FIntPoint& View : MoltViews)
	{
		const FBox2D Food = CrabHud::FoodBarRect(View.X, View.Y);
		FBox2D Previous;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FBox2D Pip = CrabHud::MoltPipRect(View.X, View.Y, Index);
			TestNearlyEqual(*FString::Printf(TEXT("at %dx%d pip %d is square"), View.X, View.Y, Index), static_cast<float>(Pip.GetSize().X), static_cast<float>(Pip.GetSize().Y), 0.01f);
			TestTrue(TEXT("above the food bar's label row"), Pip.Max.Y < Food.Min.Y - 20.f);
			TestTrue(TEXT("inside the view"), Pip.Min.X >= 0.f && Pip.Min.Y >= 0.f && Pip.Max.X <= View.X);
			TestTrue(TEXT("and inside the food bar's width"), Pip.Min.X >= Food.Min.X && Pip.Max.X <= Food.Max.X);
			if (Index > 0)
			{
				TestTrue(TEXT("each to the right of the last with a gap"), Pip.Min.X > Previous.Max.X);
			}
			Previous = Pip;
		}
		TestFalse(TEXT("clear of the molt button"), Overlaps(CrabHud::MoltPipRect(View.X, View.Y, 2), CrabHud::MoltButtonRect(View.X, View.Y)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudResultsPanelTest, "CrabSim.Hud.TheResultsPanelIsCentredWithABigNewRoundButton", TestFlags)
bool FCrabHudResultsPanelTest::RunTest(const FString& Parameters)
{
	for (const FIntPoint& View : MoltViews)
	{
		const FBox2D Panel = CrabHud::ResultsPanelRect(View.X, View.Y);
		const FBox2D Button = CrabHud::NewRoundButtonRect(View.X, View.Y);
		const float Scale = CrabHud::ScaleForHeight(View.Y);
		TestNearlyEqual(*FString::Printf(TEXT("at %dx%d the panel is centred across"), View.X, View.Y), static_cast<float>(Panel.GetCenter().X), View.X * 0.5f, 0.01f);
		TestNearlyEqual(TEXT("and down"), static_cast<float>(Panel.GetCenter().Y), View.Y * 0.5f, 0.01f);
		TestTrue(TEXT("inside the view"), Panel.Min.X >= 0.f && Panel.Min.Y >= 0.f && Panel.Max.X <= View.X && Panel.Max.Y <= View.Y);
		TestTrue(TEXT("the button is inside the panel"), Button.Min.X >= Panel.Min.X && Button.Max.X <= Panel.Max.X && Button.Min.Y >= Panel.Min.Y && Button.Max.Y <= Panel.Max.Y);
		TestNearlyEqual(TEXT("centred on it"), static_cast<float>(Button.GetCenter().X), static_cast<float>(Panel.GetCenter().X), 0.01f);
		TestTrue(TEXT("a big target: over 240 wide"), Button.GetSize().X >= 240.f * Scale && Button.GetSize().Y >= 80.f * Scale);
		TestTrue(TEXT("with room above it for the stat rows"), Button.Min.Y - Panel.Min.Y > 270.f * Scale);
		if (View.Y >= 720)
		{
			TestTrue(TEXT("and at a playable size the panel stays clear of the pips and the bars under it"), Panel.Max.Y < CrabHud::MoltPipRect(View.X, View.Y, 0).Min.Y - 10.f * Scale);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrabHudNewRoundHitTest, "CrabSim.Hud.ClicksOnTheNewRoundButtonAreForgiving", TestFlags)
bool FCrabHudNewRoundHitTest::RunTest(const FString& Parameters)
{
	const FBox2D Rect = CrabHud::NewRoundButtonRect(1280, 720);
	TestTrue(TEXT("the centre hits"), CrabHud::HitsNewRoundButton(1280, 720, Rect.GetCenter()));
	TestTrue(TEXT("each corner hits"), CrabHud::HitsNewRoundButton(1280, 720, Rect.Min) && CrabHud::HitsNewRoundButton(1280, 720, Rect.Max));
	TestTrue(TEXT("a few pixels outside still hits"), CrabHud::HitsNewRoundButton(1280, 720, Rect.Max + FVector2D(6.f, 6.f)));
	TestFalse(TEXT("far outside does not"), CrabHud::HitsNewRoundButton(1280, 720, Rect.Min - FVector2D(60.f, 60.f)));
	TestFalse(TEXT("the top of the panel does not"), CrabHud::HitsNewRoundButton(1280, 720, CrabHud::ResultsPanelRect(1280, 720).Min + FVector2D(20.f, 20.f)));
	TestFalse(TEXT("the dig button does not"), CrabHud::HitsNewRoundButton(1280, 720, CrabHud::DigButtonRect(1280, 720).GetCenter()));
	return true;
}
