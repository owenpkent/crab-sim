// SPDX-License-Identifier: Apache-2.0
#include "CrabHUD.h"
#include "CrabBeach.h"
#include "CrabDigMath.h"
#include "CrabHudMath.h"
#include "CrabMoltMath.h"
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
	const FLinearColor MoltReady = FLinearColor(0.4f, 0.72f, 0.88f, 0.95f);
	const FLinearColor MoltBusy = FLinearColor(0.24f, 0.46f, 0.66f, 0.95f);
	const FLinearColor PipEarned = FLinearColor(0.98f, 0.78f, 0.3f, 1.f);
	const FLinearColor PipEmpty = FLinearColor(0.1f, 0.1f, 0.1f, 0.7f);
	const FLinearColor Gold = FLinearColor(1.f, 0.85f, 0.35f, 1.f);
	const FLinearColor NewRoundFill = FLinearColor(0.55f, 0.8f, 0.2f, 0.98f);
	const FLinearColor ButtonInk = FLinearColor(0.05f, 0.04f, 0.02f, 1.f);
	const FLinearColor Border = FLinearColor(0.f, 0.f, 0.f, 0.6f);
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
	DrawMoltPips(*Crab, Scale);
	if (!Crab->IsRoundOver())
	{
		DrawDigButton(*Crab, Scale);
		DrawMoltButton(*Crab, Scale);
	}
	DrawMessage(*Crab, Scale);
	DrawHints(Scale);
	if (Crab->IsRoundOver())
	{
		DrawResults(*Crab, Scale);
	}
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
	const FBox2D Bar = CrabHud::GripBarRect(Canvas->SizeX, Canvas->SizeY);
	const float Width = Bar.GetSize().X;
	const float Height = Bar.GetSize().Y;
	const float X = Bar.Min.X;
	const float Y = Bar.Min.Y;

	const float Grip = Crab.GetGrip();
	DrawRect(Panel, X - 4.f * Scale, Y - 4.f * Scale, Width + 8.f * Scale, Height + 8.f * Scale);
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.95f, 0.25f, 0.15f), FLinearColor(0.35f, 0.85f, 0.35f), FMath::Clamp(Grip * 1.4f, 0.f, 1.f));
	DrawRect(Fill, X, Y, Width * Grip, Height);
	DrawText(TEXT("GRIP"), Text, X, Y - 26.f * Scale, Font, Scale * 0.8f);

	// A soft crab cannot hold more than half its grip: a mark where the cap is, and a tag with the time left.
	if (Crab.IsSoft())
	{
		DrawRect(Wet, X + Width * CrabMolt::Tuning::SoftGripCap - Scale, Y - 4.f * Scale, 2.f * Scale, Height + 8.f * Scale);
		const FString Tag = FString::Printf(TEXT("SOFT %ds"), FMath::CeilToInt(Crab.GetSoftRemaining()));
		float TagW = 0.f;
		float TagH = 0.f;
		GetTextSize(Tag, TagW, TagH, Font, Scale * 0.9f);
		DrawRect(Panel, X + 70.f * Scale, Y - 32.f * Scale, TagW + 16.f * Scale, TagH + 8.f * Scale);
		DrawText(Tag, Wet, X + 78.f * Scale, Y - 28.f * Scale, Font, Scale * 0.9f);
	}
}

void ACrabHUD::DrawFoodBar(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	// Beside the grip bar, on its right.
	const FBox2D Bar = CrabHud::FoodBarRect(Canvas->SizeX, Canvas->SizeY);
	const float Width = Bar.GetSize().X;
	const float Height = Bar.GetSize().Y;
	const float X = Bar.Min.X;
	const float Y = Bar.Min.Y;

	const float Food = Crab.GetFood();
	DrawRect(Panel, X - 4.f * Scale, Y - 4.f * Scale, Width + 8.f * Scale, Height + 8.f * Scale);
	DrawRect(FMath::Lerp(FoodLow, FoodHigh, FMath::Clamp(Food, 0.f, 1.f)), X, Y, Width * Food, Height);
	// A tick where the store is enough for a burrow, and one where it is enough to begin a molt.
	DrawRect(Text, X + Width * CrabDig::FoodCost - Scale, Y - 4.f * Scale, 2.f * Scale, Height + 8.f * Scale);
	DrawRect(Text, X + Width * CrabMolt::Tuning::MinFood - Scale, Y - 4.f * Scale, 2.f * Scale, Height + 8.f * Scale);
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
	const FLinearColor Ink = (bDigging || bReady) ? ButtonInk : TextDim;
	const float Edge = 3.f * Scale;
	DrawRect(Border, Rect.Min.X - Edge, Rect.Min.Y - Edge, Size.X + 2.f * Edge, Size.Y + 2.f * Edge);
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

void ACrabHUD::DrawMoltButton(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const FBox2D Rect = CrabHud::MoltButtonRect(Canvas->SizeX, Canvas->SizeY);
	const FVector2D Size = Rect.GetSize();
	const CrabMolt::EResult Result = Crab.CheckMolt();
	const bool bMolting = Crab.IsMolting();
	const bool bReady = Result == CrabMolt::EResult::Ok;

	const FLinearColor Fill = bMolting ? MoltBusy : (bReady ? MoltReady : DigOff);
	const FLinearColor Ink = (bMolting || bReady) ? ButtonInk : TextDim;
	const float Edge = 3.f * Scale;
	DrawRect(Border, Rect.Min.X - Edge, Rect.Min.Y - Edge, Size.X + 2.f * Edge, Size.Y + 2.f * Edge);
	DrawRect(Fill, Rect.Min.X, Rect.Min.Y, Size.X, Size.Y);

	auto Centred = [&](const FString& Line, float TextScale, float Down, const FLinearColor& Colour)
	{
		float W = 0.f;
		float H = 0.f;
		GetTextSize(Line, W, H, Font, TextScale);
		DrawText(Line, Colour, Rect.Min.X + (Size.X - W) * 0.5f, Rect.Min.Y + Size.Y * Down, Font, TextScale);
	};

	if (bMolting)
	{
		// A ring round the button fills clockwise from the top as the molt goes on, with the seconds left inside.
		const FVector2D Middle = Rect.GetCenter();
		const float Radius = Size.X * 0.42f;
		const int32 Segments = 72;
		const int32 Filled = FMath::RoundToInt(Segments * Crab.GetMoltProgress());
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const float A0 = 2.f * PI * Segment / Segments;
			const float A1 = 2.f * PI * (Segment + 1) / Segments;
			DrawLine(Middle.X + Radius * FMath::Sin(A0), Middle.Y - Radius * FMath::Cos(A0),
				Middle.X + Radius * FMath::Sin(A1), Middle.Y - Radius * FMath::Cos(A1),
				Segment < Filled ? FLinearColor(0.98f, 0.95f, 0.6f, 1.f) : FLinearColor(0.f, 0.f, 0.f, 0.45f), 8.f * Scale);
		}
		const int32 SecondsLeft = FMath::CeilToInt(CrabMolt::Tuning::Duration * (1.f - Crab.GetMoltProgress()));
		Centred(FString::Printf(TEXT("%d"), SecondsLeft), Scale * 2.2f, 0.3f, Ink);
		Centred(TEXT("MOLTING"), Scale * 0.8f, 0.68f, Ink);
	}
	else
	{
		Centred(TEXT("MOLT"), Scale * 2.2f, 0.28f, Ink);
		Centred(FString::Printf(TEXT("%.0f%% food"), CrabMolt::Tuning::FoodCost * 100.f), Scale * 0.8f, 0.68f, Ink);
	}

	// Why it is greyed out, above the button and lined up with its right edge.
	FString Reason = bMolting ? FString(TEXT("Stay put: leaving cancels")) : FString(CrabMolt::ReasonText(Result));
	if (!Reason.IsEmpty())
	{
		float ReasonW = 0.f;
		float ReasonH = 0.f;
		GetTextSize(Reason, ReasonW, ReasonH, Font, Scale * 0.9f);
		DrawRect(Panel, Rect.Max.X - ReasonW - 8.f * Scale, Rect.Min.Y - 40.f * Scale, ReasonW + 16.f * Scale, ReasonH + 8.f * Scale);
		DrawText(Reason, Text, Rect.Max.X - ReasonW, Rect.Min.Y - 36.f * Scale, Font, Scale * 0.9f);
	}
}

void ACrabHUD::DrawMoltPips(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	for (int32 Index = 0; Index < CrabMolt::Tuning::MoltsToWin; ++Index)
	{
		const FBox2D Pip = CrabHud::MoltPipRect(Canvas->SizeX, Canvas->SizeY, Index);
		const FVector2D Size = Pip.GetSize();
		const float Edge = 3.f * Scale;
		DrawRect(Border, Pip.Min.X - Edge, Pip.Min.Y - Edge, Size.X + 2.f * Edge, Size.Y + 2.f * Edge);
		DrawRect(Index < Crab.GetMolts() ? PipEarned : PipEmpty, Pip.Min.X, Pip.Min.Y, Size.X, Size.Y);
	}
	const FBox2D Last = CrabHud::MoltPipRect(Canvas->SizeX, Canvas->SizeY, CrabMolt::Tuning::MoltsToWin - 1);
	DrawText(TEXT("MOLTS"), Text, Last.Max.X + 14.f * Scale, Last.Min.Y + 3.f * Scale, Font, Scale * 0.8f);
}

void ACrabHUD::DrawResults(const ACrabPawn& Crab, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const float ViewW = Canvas->SizeX;
	const float ViewH = Canvas->SizeY;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, ViewW, ViewH);

	const FBox2D PanelRect = CrabHud::ResultsPanelRect(ViewW, ViewH);
	const FVector2D PanelSize = PanelRect.GetSize();
	const float Edge = 4.f * Scale;
	DrawRect(Gold, PanelRect.Min.X - Edge, PanelRect.Min.Y - Edge, PanelSize.X + 2.f * Edge, PanelSize.Y + 2.f * Edge);
	DrawRect(FLinearColor(0.04f, 0.06f, 0.08f, 0.94f), PanelRect.Min.X, PanelRect.Min.Y, PanelSize.X, PanelSize.Y);

	float TitleW = 0.f;
	float TitleH = 0.f;
	GetTextSize(TEXT("Fully grown!"), TitleW, TitleH, Font, Scale * 2.6f);
	DrawText(TEXT("Fully grown!"), Gold, PanelRect.Min.X + (PanelSize.X - TitleW) * 0.5f, PanelRect.Min.Y + 22.f * Scale, Font, Scale * 2.6f);

	struct FRow { FString Label; FString Value; FLinearColor Colour; };
	TArray<FRow> Rows;
	Rows.Add({TEXT("Time"), CrabMolt::TimeText(Crab.GetRoundSeconds()), Text});
	Rows.Add({TEXT("Best time"), CrabMolt::TimeText(Crab.GetBestSeconds()) + (Crab.IsNewBest() ? TEXT("  NEW BEST") : TEXT("")), Crab.IsNewBest() ? Gold : Text});
	Rows.Add({TEXT("Molts"), FString::Printf(TEXT("%d"), Crab.GetMolts()), Text});
	Rows.Add({TEXT("Burrows dug"), FString::Printf(TEXT("%d"), Crab.GetRoundDug()), Text});
	Rows.Add({TEXT("Food eaten"), FString::Printf(TEXT("%.1f"), Crab.GetRoundFoodEaten()), Text});
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const float Y = PanelRect.Min.Y + (104.f + 36.f * Index) * Scale;
		float ValueW = 0.f;
		float ValueH = 0.f;
		GetTextSize(Rows[Index].Value, ValueW, ValueH, Font, Scale * 1.3f);
		DrawText(Rows[Index].Label, TextDim, PanelRect.Min.X + 60.f * Scale, Y, Font, Scale * 1.3f);
		DrawText(Rows[Index].Value, Rows[Index].Colour, PanelRect.Max.X - 60.f * Scale - ValueW, Y, Font, Scale * 1.3f);
	}

	const FBox2D Button = CrabHud::NewRoundButtonRect(ViewW, ViewH);
	const FVector2D ButtonSize = Button.GetSize();
	DrawRect(Border, Button.Min.X - Edge, Button.Min.Y - Edge, ButtonSize.X + 2.f * Edge, ButtonSize.Y + 2.f * Edge);
	DrawRect(NewRoundFill, Button.Min.X, Button.Min.Y, ButtonSize.X, ButtonSize.Y);
	float LabelW = 0.f;
	float LabelH = 0.f;
	GetTextSize(TEXT("NEW ROUND"), LabelW, LabelH, Font, Scale * 2.f);
	DrawText(TEXT("NEW ROUND"), ButtonInk, Button.Min.X + (ButtonSize.X - LabelW) * 0.5f, Button.Min.Y + (ButtonSize.Y - LabelH) * 0.5f, Font, Scale * 2.f);
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
	DrawText(TEXT("Click: walk    Hold: follow    Right click: dash    Click the crab: dance    Click a burrow: dig in    Click green mud: eat    DIG: new burrow    MOLT: in a burrow"),
		TextDim, 24.f * Scale, Canvas->SizeY - 30.f * Scale, Font, Scale * 0.7f);
}
