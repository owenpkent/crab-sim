// SPDX-License-Identifier: Apache-2.0
#include "CrabHUD.h"
#include "CrabBeach.h"
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
	const float Scale = FMath::Clamp(Canvas->SizeY / 1080.f, 0.8f, 2.5f) * 1.25f;

	if (const ACrabBeach* Beach = Crab->GetBeach())
	{
		DrawTideGauge(*Crab, *Beach, Scale);
	}
	DrawGripBar(*Crab, Scale);
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
	DrawText(TEXT("Click: walk    Hold: follow    Right click: dash    Click the crab: dance    Click a burrow: dig in"),
		TextDim, 24.f * Scale, Canvas->SizeY - 30.f * Scale, Font, Scale * 0.7f);
}
