// SPDX-License-Identifier: Apache-2.0
#include "CrabHUD.h"
#include "CrabBeach.h"
#include "CrabDigMath.h"
#include "CrabHudMath.h"
#include "CrabPawn.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"

namespace
{
	const FLinearColor Panel = FLinearColor(0.f, 0.f, 0.f, 0.5f);
	const FLinearColor WaterFill = FLinearColor(0.1f, 0.55f, 0.85f, 0.9f);
	const FLinearColor Text = FLinearColor(1.f, 1.f, 1.f, 0.95f);
	const FLinearColor TextDim = FLinearColor(1.f, 1.f, 1.f, 0.6f);
	const FLinearColor Dry = FLinearColor(0.95f, 0.95f, 0.95f, 1.f);
	const FLinearColor Wet = FLinearColor(1.f, 0.3f, 0.2f, 1.f);
	const FLinearColor FoodLow = FLinearColor(0.85f, 0.45f, 0.1f, 1.f);
	const FLinearColor FoodHigh = FLinearColor(0.55f, 0.8f, 0.2f, 1.f);
	const FLinearColor DigReady = FLinearColor(0.85f, 0.6f, 0.2f, 0.95f);
	const FLinearColor DigBusy = FLinearColor(0.4f, 0.62f, 0.25f, 0.95f);
	const FLinearColor DigOff = FLinearColor(0.22f, 0.22f, 0.22f, 0.8f);
}

void ACrabHUD::DrawHUD()
{
	Super::DrawHUD();

	const ACrabPawn* Crab = Cast<ACrabPawn>(GetOwningPawn());
	if (!Crab || !Canvas)
	{
		return;
	}

	// Scale with the window, so the readout is as big to read at 4K as at 720p.
	const float Scale = CrabHud::ScaleForHeight(Canvas->SizeY);

	if (const ACrabBeach* Beach = Crab->GetBeach())
	{
		DrawTideGauge(*Crab, *Beach, Scale);
	}
	DrawGripBar(*Crab, Scale);
	DrawFoodBar(*Crab, Scale);
	DrawDigButton(*Crab, Scale);
	DrawMessage(*Crab, Scale);
	DrawHints(Scale);
}

void ACrabHUD::DrawTideGauge(const ACrabPawn& Crab, const ACrabBeach& Beach, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const float GX = 30.f * Scale;
	const float GY = Canvas->SizeY * 0.25f;
	const float GW = 28.f * Scale;
	const float GH = Canvas->SizeY * 0.4f;

	DrawRect(Panel, GX - 5.f * Scale, GY - 5.f * Scale, GW + 10.f * Scale, GH + 10.f * Scale);
	const float Fraction = Beach.GetTideFraction();
	DrawRect(WaterFill, GX, GY + GH * (1.f - Fraction), GW, GH * Fraction);

	// A marker for the ground under the crab: when the water is above it, the crab is wet.
	const FCrabTideSettings& Tide = Beach.GetTideSettings();
	const FVector Location = Crab.GetActorLocation();
	const float Ground = Beach.GetGroundHeight(Location.X, Location.Y);
	const float Range = FMath::Max(Tide.HighLevel - Tide.LowLevel, 1.f);
	const float MarkerFraction = FMath::Clamp((Ground - Tide.LowLevel) / Range, 0.f, 1.f);
	const bool bWet = Beach.GetTideLevel() > Ground;
	DrawRect(bWet ? Wet : Dry, GX - 12.f * Scale, GY + GH * (1.f - MarkerFraction) - 2.f * Scale, GW + 24.f * Scale, 4.f * Scale);

	DrawText(TEXT("HIGH"), TextDim, GX - 2.f * Scale, GY - 26.f * Scale, Font, Scale * 0.8f);
	DrawText(TEXT("LOW"), TextDim, GX + 2.f * Scale, GY + GH + 8.f * Scale, Font, Scale * 0.8f);

	const FString Label = FString::Printf(TEXT("%s in %ds"), Beach.IsTideRising() ? TEXT("Rising") : TEXT("Falling"), FMath::CeilToInt(Beach.GetSecondsToTurn()));
	DrawText(Label, Text, GX - 2.f * Scale, GY + GH + 32.f * Scale, Font, Scale * 0.8f);
}

void ACrabHUD::DrawGripBar(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const float Width = 300.f * Scale;
	const float Height = 16.f * Scale;
	const float X = (Canvas->SizeX - Width) * 0.5f;
	const float Y = Canvas->SizeY - 70.f * Scale;

	const float Grip = Crab.GetGrip();
	DrawRect(Panel, X - 4.f * Scale, Y - 4.f * Scale, Width + 8.f * Scale, Height + 8.f * Scale);
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.95f, 0.25f, 0.15f), FLinearColor(0.35f, 0.85f, 0.35f), FMath::Clamp(Grip * 1.4f, 0.f, 1.f));
	DrawRect(Fill, X, Y, Width * Grip, Height);
	DrawText(TEXT("GRIP"), Text, X, Y - 26.f * Scale, Font, Scale * 0.8f);
}

void ACrabHUD::DrawFoodBar(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	// Beside the grip bar, on its right.
	const float GripRight = (Canvas->SizeX + 300.f * Scale) * 0.5f;
	const float Width = 200.f * Scale;
	const float Height = 16.f * Scale;
	const float X = GripRight + 36.f * Scale;
	const float Y = Canvas->SizeY - 70.f * Scale;

	const float Food = Crab.GetFood();
	DrawRect(Panel, X - 4.f * Scale, Y - 4.f * Scale, Width + 8.f * Scale, Height + 8.f * Scale);
	DrawRect(FMath::Lerp(FoodLow, FoodHigh, FMath::Clamp(Food, 0.f, 1.f)), X, Y, Width * Food, Height);
	// A tick where the store is enough for a burrow.
	DrawRect(Text, X + Width * CrabDig::FoodCost - Scale, Y - 4.f * Scale, 2.f * Scale, Height + 8.f * Scale);
	DrawText(TEXT("FOOD"), Text, X, Y - 26.f * Scale, Font, Scale * 0.8f);
}

void ACrabHUD::DrawDigButton(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const FBox2D Rect = CrabHud::DigButtonRect(Canvas->SizeX, Canvas->SizeY);
	const FVector2D Size = Rect.GetSize();
	const CrabDig::EResult Result = Crab.CheckDig();
	const bool bDigging = Crab.IsDigging();
	const bool bReady = Result == CrabDig::EResult::Ok;

	const FLinearColor Fill = bDigging ? DigBusy : (bReady ? DigReady : DigOff);
	const FLinearColor Ink = (bDigging || bReady) ? FLinearColor(0.05f, 0.04f, 0.02f, 1.f) : TextDim;
	const float Border = 3.f * Scale;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), Rect.Min.X - Border, Rect.Min.Y - Border, Size.X + 2.f * Border, Size.Y + 2.f * Border);
	DrawRect(Fill, Rect.Min.X, Rect.Min.Y, Size.X, Size.Y);

	const FString Label = bDigging ? TEXT("DIGGING") : TEXT("DIG");
	const float LabelScale = bDigging ? Scale * 1.15f : Scale * 2.2f;
	float LabelW = 0.f;
	float LabelH = 0.f;
	GetTextSize(Label, LabelW, LabelH, Font, LabelScale);
	DrawText(Label, Ink, Rect.Min.X + (Size.X - LabelW) * 0.5f, Rect.Min.Y + Size.Y * 0.28f, Font, LabelScale);

	const FString Cost = FString::Printf(TEXT("%.0f%% food"), CrabDig::FoodCost * 100.f);
	float CostW = 0.f;
	float CostH = 0.f;
	GetTextSize(Cost, CostW, CostH, Font, Scale * 0.8f);
	DrawText(Cost, Ink, Rect.Min.X + (Size.X - CostW) * 0.5f, Rect.Min.Y + Size.Y * 0.68f, Font, Scale * 0.8f);

	// Progress bar along the foot of the button while the crab digs.
	if (bDigging)
	{
		const float BarHeight = 12.f * Scale;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Rect.Min.X, Rect.Max.Y - BarHeight, Size.X, BarHeight);
		DrawRect(FLinearColor(0.95f, 0.9f, 0.5f, 1.f), Rect.Min.X, Rect.Max.Y - BarHeight, Size.X * Crab.GetDigProgress(), BarHeight);
	}

	// Why it is greyed out, above the button and lined up with its right edge.
	FString Reason = bDigging ? FString(TEXT("Hold still: any walk cancels")) : FString(CrabDig::ReasonText(Result));
	if (!Reason.IsEmpty())
	{
		Reason[0] = FChar::ToUpper(Reason[0]);
		float ReasonW = 0.f;
		float ReasonH = 0.f;
		GetTextSize(Reason, ReasonW, ReasonH, Font, Scale * 0.9f);
		DrawRect(Panel, Rect.Max.X - ReasonW - 8.f * Scale, Rect.Min.Y - 40.f * Scale, ReasonW + 16.f * Scale, ReasonH + 8.f * Scale);
		DrawText(Reason, Text, Rect.Max.X - ReasonW, Rect.Min.Y - 36.f * Scale, Font, Scale * 0.9f);
	}
}

void ACrabHUD::DrawMessage(const ACrabPawn& Crab, float Scale)
{
	const float Alpha = Crab.GetMessageAlpha();
	if (Alpha <= 0.f || Crab.GetMessage().IsEmpty())
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Crab.GetMessage(), Width, Height, Font, Scale * 1.3f);
	const float X = (Canvas->SizeX - Width) * 0.5f;
	const float Y = Canvas->SizeY * 0.12f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha), X - 14.f * Scale, Y - 8.f * Scale, Width + 28.f * Scale, Height + 16.f * Scale);
	DrawText(Crab.GetMessage(), FLinearColor(1.f, 1.f, 1.f, Alpha), X, Y, Font, Scale * 1.3f);
}

void ACrabHUD::DrawHints(float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	DrawText(TEXT("Click: walk    Hold: follow    Right click: dash    Click the crab: dance    Click a burrow: dig in    Click green mud: eat    DIG button: new burrow"),
		TextDim, 24.f * Scale, Canvas->SizeY - 30.f * Scale, Font, Scale * 0.7f);
}
