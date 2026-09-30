// SPDX-License-Identifier: Apache-2.0
#include "CrabHUD.h"
#include "CrabBeach.h"
#include "CrabDigMath.h"
#include "CrabGotoMath.h"
#include "CrabGull.h"
#include "CrabHudMath.h"
#include "CrabMoltMath.h"
#include "CrabPawn.h"
#include "CrabPlayerController.h"
#include "CrabStickMath.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

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
	const FLinearColor FoodReady = FLinearColor(0.55f, 0.8f, 0.25f, 0.95f);
	const FLinearColor BurrowReady = FLinearColor(0.74f, 0.62f, 0.9f, 0.95f);
	const FLinearColor HintInk = FLinearColor(1.f, 1.f, 1.f, 1.f);
	const FLinearColor HintPanel = FLinearColor(0.f, 0.f, 0.f, 0.8f);
	const FLinearColor PipEarned = FLinearColor(0.98f, 0.78f, 0.3f, 1.f);
	const FLinearColor PipEmpty = FLinearColor(0.1f, 0.1f, 0.1f, 0.7f);
	const FLinearColor Gold = FLinearColor(1.f, 0.85f, 0.35f, 1.f);
	const FLinearColor NewRoundFill = FLinearColor(0.55f, 0.8f, 0.2f, 0.98f);
	const FLinearColor ButtonInk = FLinearColor(0.05f, 0.04f, 0.02f, 1.f);
	const FLinearColor Border = FLinearColor(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor GullFill = FLinearColor(0.5f, 0.07f, 0.05f, 0.92f);
	const FLinearColor GullInk = FLinearColor(1.f, 0.95f, 0.85f, 1.f);
	const FLinearColor GullArrow = FLinearColor(1.f, 0.78f, 0.15f, 1.f);
	const FLinearColor GullArrowOutline = FLinearColor(0.f, 0.f, 0.f, 0.85f);
	const FLinearColor EatenAccent = FLinearColor(0.92f, 0.5f, 0.4f, 1.f);
	const FLinearColor OneStickOn = FLinearColor(0.55f, 0.8f, 0.25f, 0.95f);
	const FLinearColor OneStickOff = FLinearColor(0.4f, 0.4f, 0.4f, 0.9f);
	const FLinearColor StickItemFill = FLinearColor(0.14f, 0.14f, 0.16f, 0.88f);
	const FLinearColor StickCursorFill = FLinearColor(0.98f, 0.78f, 0.3f, 0.98f);
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
	TArray<FString> ShownReasons;
	if (!Crab->IsRoundOver())
	{
		ShownReasons.Add(DrawDigButton(*Crab, Scale));
		ShownReasons.Add(DrawMoltButton(*Crab, Scale));
		ShownReasons.Add(DrawFoodButton(*Crab, Scale));
		ShownReasons.Add(DrawBurrowButton(*Crab, Scale));
	}
	DrawMessage(*Crab, Scale, ShownReasons);
	const ACrabGull* Gull = nullptr;
	if (UWorld* World = GetWorld())
	{
		TActorIterator<ACrabGull> It(World);
		Gull = It ? *It : nullptr;
	}
	if (Gull && Gull->IsWarned() && !Crab->IsRoundOver())
	{
		DrawGullWarning(*Crab, *Gull, Scale);
	}
	DrawHints(*Crab, Gull && CrabGull::IsThreat(Gull->GetPhase()), Scale);
	if (Crab->IsRoundOver())
	{
		DrawResults(*Crab, Scale);
	}

	// Whatever the CVar says: the toggle is how a stick-only player turns this on in the first place.
	DrawOneStickToggle(Scale);
	const IConsoleVariable* OneStickCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.OneStick"));
	if (OneStickCVar && OneStickCVar->GetInt() != 0)
	{
		// Still drawn once the round is over: the menu falls back to just NEW ROUND (CrabStick::ActiveItemCount),
		// standing in for the results panel's own button so a stick-only player can start the next round.
		if (const ACrabPlayerController* PC = Cast<ACrabPlayerController>(PlayerOwner))
		{
			DrawOneStickPanel(*PC, Scale, Crab->IsRoundOver());
		}
	}
}

void ACrabHUD::DrawTideGauge(const ACrabPawn& Crab, const ACrabBeach& Beach, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const FBox2D Bar = CrabHud::TideGaugeBarRect(Canvas->SizeX, Canvas->SizeY);
	const float GX = Bar.Min.X;
	const float GY = Bar.Min.Y;
	const float GW = Bar.GetSize().X;
	const float GH = Bar.GetSize().Y;

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

FString ACrabHUD::DrawDigButton(const ACrabPawn& Crab, float Scale)
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
		DrawReasonLine(Rect, Reason, Scale);
	}
	return Reason;
}

FString ACrabHUD::DrawMoltButton(const ACrabPawn& Crab, float Scale)
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
	const FString Reason = bMolting ? FString(TEXT("Stay put: leaving cancels")) : FString(CrabMolt::ReasonText(Result));
	if (!Reason.IsEmpty())
	{
		DrawReasonLine(Rect, Reason, Scale);
	}
	return Reason;
}

void ACrabHUD::DrawReasonLine(const FBox2D& Rect, const FString& Reason, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	float ReasonW = 0.f;
	float ReasonH = 0.f;
	GetTextSize(Reason, ReasonW, ReasonH, Font, Scale * 0.9f);
	DrawRect(Panel, Rect.Max.X - ReasonW - 8.f * Scale, Rect.Min.Y - 40.f * Scale, ReasonW + 16.f * Scale, ReasonH + 8.f * Scale);
	DrawText(Reason, Text, Rect.Max.X - ReasonW, Rect.Min.Y - 36.f * Scale, Font, Scale * 0.9f);
}

FString ACrabHUD::DrawFoodButton(const ACrabPawn& Crab, float Scale)
{
	const FString Reason = CrabGoto::ReasonText(Crab.CheckGoToFood());
	DrawGotoButton(CrabHud::FoodButtonRect(Canvas->SizeX, Canvas->SizeY), TEXT("FOOD"), TEXT("best patch"), FoodReady, Reason, Scale);
	return Reason;
}

FString ACrabHUD::DrawBurrowButton(const ACrabPawn& Crab, float Scale)
{
	const FString Reason = CrabGoto::ReasonText(Crab.CheckGoToBurrow());
	DrawGotoButton(CrabHud::BurrowButtonRect(Canvas->SizeX, Canvas->SizeY), TEXT("BURROW"), TEXT("safest burrow"), BurrowReady, Reason, Scale);
	return Reason;
}

void ACrabHUD::DrawGotoButton(const FBox2D& Rect, const TCHAR* Label, const TCHAR* Hint, const FLinearColor& ReadyColour, const FString& Reason, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const FVector2D Size = Rect.GetSize();
	const bool bReady = Reason.IsEmpty();
	const FLinearColor Ink = bReady ? ButtonInk : TextDim;
	const float Edge = 3.f * Scale;
	DrawRect(Border, Rect.Min.X - Edge, Rect.Min.Y - Edge, Size.X + 2.f * Edge, Size.Y + 2.f * Edge);
	DrawRect(bReady ? ReadyColour : DigOff, Rect.Min.X, Rect.Min.Y, Size.X, Size.Y);

	auto Centred = [&](const FString& Line, float TextScale, float Down)
	{
		float W = 0.f;
		float H = 0.f;
		GetTextSize(Line, W, H, Font, TextScale);
		// Shrink a line that would not fit, so a long word never runs off the button.
		const float Fit = FMath::Min(1.f, (Size.X - 12.f * Scale) / FMath::Max(W, 1.f));
		GetTextSize(Line, W, H, Font, TextScale * Fit);
		DrawText(Line, Ink, Rect.Min.X + (Size.X - W) * 0.5f, Rect.Min.Y + Size.Y * Down, Font, TextScale * Fit);
	};
	Centred(Label, Scale * 2.f, 0.2f);
	Centred(Hint, Scale * 0.8f, 0.68f);

	if (!bReady)
	{
		DrawReasonLine(Rect, Reason, Scale);
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
	const bool bEaten = Crab.IsEaten();
	const FLinearColor& Accent = bEaten ? EatenAccent : Gold;
	DrawRect(Accent, PanelRect.Min.X - Edge, PanelRect.Min.Y - Edge, PanelSize.X + 2.f * Edge, PanelSize.Y + 2.f * Edge);
	DrawRect(FLinearColor(0.04f, 0.06f, 0.08f, 0.94f), PanelRect.Min.X, PanelRect.Min.Y, PanelSize.X, PanelSize.Y);

	const FString Title = bEaten ? TEXT("Eaten by a gull") : TEXT("Fully grown!");
	float TitleW = 0.f;
	float TitleH = 0.f;
	GetTextSize(Title, TitleW, TitleH, Font, Scale * 2.6f);
	DrawText(Title, Accent, PanelRect.Min.X + (PanelSize.X - TitleW) * 0.5f, PanelRect.Min.Y + 22.f * Scale, Font, Scale * 2.6f);
	if (bEaten)
	{
		float NoteW = 0.f;
		float NoteH = 0.f;
		GetTextSize(TEXT("The gull is unbothered."), NoteW, NoteH, Font, Scale);
		DrawText(TEXT("The gull is unbothered."), TextDim, PanelRect.Min.X + (PanelSize.X - NoteW) * 0.5f, PanelRect.Min.Y + 76.f * Scale, Font, Scale);
	}

	struct FRow { FString Label; FString Value; FLinearColor Colour; };
	TArray<FRow> Rows;
	Rows.Add({TEXT("Time"), CrabMolt::TimeText(Crab.GetRoundSeconds()), Text});
	Rows.Add({TEXT("Best time"), CrabMolt::BestText(Crab.GetBestSeconds()) + (Crab.IsNewBest() ? TEXT("  NEW BEST") : TEXT("")), Crab.IsNewBest() ? Gold : Text});
	Rows.Add({bEaten ? TEXT("Molts so far") : TEXT("Molts"), FString::Printf(TEXT("%d"), Crab.GetMolts()), Text});
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

void ACrabHUD::DrawMessage(const ACrabPawn& Crab, float Scale, const TArray<FString>& ShownReasons)
{
	const float Alpha = Crab.GetMessageAlpha();
	if (Alpha <= 0.f || Crab.GetMessage().IsEmpty())
	{
		return;
	}
	// A refusal that a button's reason line already says is not said twice.
	for (const FString& Reason : ShownReasons)
	{
		if (CrabHud::EchoesReason(Crab.GetMessage(), Reason))
		{
			return;
		}
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

void ACrabHUD::DrawHints(const ACrabPawn& Crab, bool bGullDown, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const IConsoleVariable* OneStickCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.OneStick"));
	const bool bOneStick = OneStickCVar && OneStickCVar->GetInt() != 0;
	const CrabHud::FHintText Hint = CrabHud::HintText(Crab.GetRoundSeconds(), Crab.IsInBurrow(), Crab.IsMolting(), Crab.IsRoundOver(), bGullDown, bOneStick);
	if (Hint.First.IsEmpty())
	{
		return;
	}
	TArray<FString> Lines;
	Lines.Add(Hint.First);
	if (!Hint.Second.IsEmpty())
	{
		Lines.Add(Hint.Second);
	}

	// Big and white on a dark panel, never under HintMinPixels tall whatever the font is, and shrunk (to that floor at
	// most) only if a line would run into the grip bar.
	float SampleW = 0.f;
	float SampleH = 0.f;
	GetTextSize(TEXT("Ag"), SampleW, SampleH, Font, 1.f);
	const float Floor = CrabHud::HintMinPixels * Scale / FMath::Max(SampleH, 1.f);
	float TextScale = FMath::Max(Scale * 1.5f, Floor);
	const FBox2D Room = CrabHud::HintPanelRect(Canvas->SizeX, Canvas->SizeY);
	const float Pad = 12.f * Scale;
	float Widest = 0.f;
	float LineH = 0.f;
	for (const FString& Line : Lines)
	{
		float W = 0.f;
		GetTextSize(Line, W, LineH, Font, TextScale);
		Widest = FMath::Max(Widest, W);
	}
	if (Widest > Room.GetSize().X - 2.f * Pad)
	{
		TextScale = FMath::Max(TextScale * (Room.GetSize().X - 2.f * Pad) / Widest, Floor);
		Widest = 0.f;
		for (const FString& Line : Lines)
		{
			float W = 0.f;
			GetTextSize(Line, W, LineH, Font, TextScale);
			Widest = FMath::Max(Widest, W);
		}
	}

	const float Gap = 4.f * Scale;
	const float PanelH = Lines.Num() * LineH + (Lines.Num() - 1) * Gap + 2.f * Pad * 0.6f;
	const float PanelW = Widest + 2.f * Pad;
	const float Top = Room.Max.Y - PanelH;
	DrawRect(HintPanel, Room.Min.X, Top, PanelW, PanelH);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawText(Lines[Index], HintInk, Room.Min.X + Pad, Top + Pad * 0.6f + Index * (LineH + Gap), Font, TextScale);
	}
}

void ACrabHUD::DrawGullWarning(const ACrabPawn& Crab, const ACrabGull& Gull, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const float ViewW = Canvas->SizeX;
	const float ViewH = Canvas->SizeY;

	// The banner: "Gull!" and what it is doing, top middle.
	const FBox2D Banner = CrabHud::GullBannerRect(ViewW, ViewH);
	const FVector2D BannerSize = Banner.GetSize();
	const float Edge = 3.f * Scale;
	DrawRect(Border, Banner.Min.X - Edge, Banner.Min.Y - Edge, BannerSize.X + 2.f * Edge, BannerSize.Y + 2.f * Edge);
	DrawRect(GullFill, Banner.Min.X, Banner.Min.Y, BannerSize.X, BannerSize.Y);
	const FString Line = CrabGull::BannerText(Gull.GetPhase());
	float TitleW = 0.f;
	float TitleH = 0.f;
	float LineW = 0.f;
	float LineH = 0.f;
	GetTextSize(TEXT("Gull!"), TitleW, TitleH, Font, Scale * 2.f);
	GetTextSize(Line, LineW, LineH, Font, Scale * 1.05f);
	const float Top = Banner.Min.Y + (BannerSize.Y - TitleH - LineH) * 0.5f;
	DrawText(TEXT("Gull!"), GullInk, Banner.Min.X + (BannerSize.X - TitleW) * 0.5f, Top, Font, Scale * 2.f);
	DrawText(Line, GullInk, Banner.Min.X + (BannerSize.X - LineW) * 0.5f, Top + TitleH, Font, Scale * 1.05f);

	// The arrow: at the edge of the view, toward the gull, with how far it is under it. Not a button.
	const FVector Where = Crab.GetActorLocation();
	FVector2D Direction = CrabHud::GullArrowDirection(FVector2D(Where.X, Where.Y), Gull.GetGroundLocation(), Crab.GetCameraYaw(),
		CrabOrbit::GroundForeshortening(Crab.GetCameraPitch()));
	// A gull that is on screen is kept clear of, and the arrow points at it from where it sits.
	const FVector Seen = Project(Gull.GetActorLocation() + FVector(0.f, 0.f, 60.f));
	const bool bOnScreen = Seen.Z > 0.f && Seen.X > 0.f && Seen.X < ViewW && Seen.Y > 0.f && Seen.Y < ViewH;
	TArray<FBox2D> Extra;
	if (bOnScreen)
	{
		const FVector2D Half(120.f * Scale, 100.f * Scale);
		Extra.Add(FBox2D(FVector2D(Seen.X, Seen.Y) - Half, FVector2D(Seen.X, Seen.Y) + Half));
	}
	const FBox2D Box = CrabHud::GullArrowBox(ViewW, ViewH, Direction, Extra);
	if (bOnScreen)
	{
		FVector2D Toward = FVector2D(Seen.X, Seen.Y) - Box.GetCenter();
		if (Toward.Normalize())
		{
			Direction = Toward;
		}
	}
	const FVector2D Sideways(-Direction.Y, Direction.X);
	const FVector2D Middle = Box.GetCenter() - FVector2D(0.f, 8.f * Scale);
	const FVector2D Tip = Middle + Direction * (34.f * Scale);
	const FVector2D Tail = Middle - Direction * (30.f * Scale);
	const FVector2D WingA = Tip - Direction * (28.f * Scale) + Sideways * (26.f * Scale);
	const FVector2D WingB = Tip - Direction * (28.f * Scale) - Sideways * (26.f * Scale);
	auto Stroke = [this](const FVector2D& From, const FVector2D& To, const FLinearColor& Colour, float Width)
	{
		DrawLine(From.X, From.Y, To.X, To.Y, Colour, Width);
	};
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FLinearColor& Colour = Pass == 0 ? GullArrowOutline : GullArrow;
		const float Width = (Pass == 0 ? 24.f : 14.f) * Scale;
		Stroke(Tail, Tip, Colour, Width);
		Stroke(Tip, WingA, Colour, Width);
		Stroke(Tip, WingB, Colour, Width);
	}
	const FString Distance = CrabGull::DistanceText(Gull.GetCrabDistance());
	float DistanceW = 0.f;
	float DistanceH = 0.f;
	GetTextSize(Distance, DistanceW, DistanceH, Font, Scale * 1.1f);
	const FVector2D TextAt(Box.GetCenter().X - DistanceW * 0.5f, Box.GetCenter().Y + 32.f * Scale);
	DrawRect(Panel, TextAt.X - 6.f * Scale, TextAt.Y - 2.f * Scale, DistanceW + 12.f * Scale, DistanceH + 4.f * Scale);
	DrawText(Distance, Text, TextAt.X, TextAt.Y, Font, Scale * 1.1f);
}

void ACrabHUD::DrawOneStickToggle(float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("CrabSim.OneStick"));
	const bool bOn = CVar && CVar->GetInt() != 0;
	const FBox2D Rect = CrabHud::OneStickButtonRect(Canvas->SizeX, Canvas->SizeY);
	const FVector2D Size = Rect.GetSize();
	const float Edge = 3.f * Scale;
	DrawRect(Border, Rect.Min.X - Edge, Rect.Min.Y - Edge, Size.X + 2.f * Edge, Size.Y + 2.f * Edge);
	DrawRect(bOn ? OneStickOn : OneStickOff, Rect.Min.X, Rect.Min.Y, Size.X, Size.Y);

	const FString Label = bOn ? TEXT("ONE STICK: ON") : TEXT("ONE STICK: OFF");
	float LabelW = 0.f;
	float LabelH = 0.f;
	GetTextSize(Label, LabelW, LabelH, Font, Scale * 0.95f);
	DrawText(Label, bOn ? ButtonInk : Text, Rect.Min.X + (Size.X - LabelW) * 0.5f, Rect.Min.Y + (Size.Y - LabelH) * 0.5f, Font, Scale * 0.95f);
}

void ACrabHUD::DrawOneStickPanel(const ACrabPlayerController& PC, float Scale, bool bRoundOver)
{
	UFont* Font = GEngine->GetMediumFont();
	const float ViewW = Canvas->SizeX;
	const float ViewH = Canvas->SizeY;
	const int32 ItemCount = CrabStick::ActiveItemCount(bRoundOver);
	const CrabStick::EMode Mode = PC.GetOneStickMode();

	// The cursor and its highlight only mean anything in MENU: STEER hides the column so it does not
	// sit in the way of the view the player is driving by. The round ending always forces MENU (see
	// FMenuState), so NEW ROUND is never hidden behind STEER.
	if (Mode == CrabStick::EMode::Menu)
	{
		const CrabStick::EMenuItem Cursor = PC.GetOneStickCursor();
		for (int32 Index = 0; Index < ItemCount; ++Index)
		{
			const CrabStick::EMenuItem Item = CrabStick::ActiveItemAt(bRoundOver, Index);
			const FBox2D Rect = CrabHud::StickMenuItemRect(ViewW, ViewH, Index);
			const FVector2D Size = Rect.GetSize();
			const bool bHighlighted = Item == Cursor;
			const float Edge = 3.f * Scale;
			DrawRect(Border, Rect.Min.X - Edge, Rect.Min.Y - Edge, Size.X + 2.f * Edge, Size.Y + 2.f * Edge);
			DrawRect(bHighlighted ? StickCursorFill : StickItemFill, Rect.Min.X, Rect.Min.Y, Size.X, Size.Y);

			const FString Label = CrabStick::ItemLabel(Item);
			float LabelW = 0.f;
			float LabelH = 0.f;
			GetTextSize(Label, LabelW, LabelH, Font, Scale * 1.4f);
			DrawText(Label, bHighlighted ? ButtonInk : Text, Rect.Min.X + 18.f * Scale, Rect.Min.Y + (Size.Y - LabelH) * 0.5f, Font, Scale * 1.4f);
		}
	}

	const FBox2D Banner = CrabHud::StickModeBannerRect(ViewW, ViewH, ItemCount);
	const FVector2D BannerSize = Banner.GetSize();
	DrawRect(Panel, Banner.Min.X, Banner.Min.Y, BannerSize.X, BannerSize.Y);
	// Two lines at a readable size: one line had to shrink to fit the column's width.
	const FString BannerFirst = bRoundOver ? TEXT("Left/right or click:")
		: (Mode == CrabStick::EMode::Menu ? TEXT("Up/down: move. Left/right: pick.") : TEXT("STEER"));
	const FString BannerSecond = bRoundOver ? TEXT("NEW ROUND")
		: (Mode == CrabStick::EMode::Menu ? TEXT("Triple-tap: steer.") : TEXT("Triple-tap: menu."));
	float FirstW = 0.f;
	float FirstH = 0.f;
	float BannerScale = Scale * 1.05f;
	GetTextSize(BannerFirst, FirstW, FirstH, Font, BannerScale);
	BannerScale *= FMath::Min(1.f, (BannerSize.X - 16.f * Scale) / FMath::Max(FirstW, 1.f));
	GetTextSize(BannerFirst, FirstW, FirstH, Font, BannerScale);
	const float LineTop = Banner.Min.Y + (BannerSize.Y - 2.f * FirstH) * 0.5f;
	DrawText(BannerFirst, Text, Banner.Min.X + 8.f * Scale, LineTop, Font, BannerScale);
	DrawText(BannerSecond, Text, Banner.Min.X + 8.f * Scale, LineTop + FirstH, Font, BannerScale);

	// The chain pips: how many taps have built up toward the third, which always fires a triple tap.
	const int32 ChainCount = PC.GetOneStickChainCount();
	FBox2D LastPip = Banner;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FBox2D Pip = CrabHud::StickChainPipRect(ViewW, ViewH, ItemCount, Index);
		const FVector2D PipSize = Pip.GetSize();
		const float Edge = 2.f * Scale;
		DrawRect(Border, Pip.Min.X - Edge, Pip.Min.Y - Edge, PipSize.X + 2.f * Edge, PipSize.Y + 2.f * Edge);
		DrawRect(Index < ChainCount ? PipEarned : PipEmpty, Pip.Min.X, Pip.Min.Y, PipSize.X, PipSize.Y);
		LastPip = Pip;
	}
	const FString PipLabel = FString::Printf(TEXT("%d of 3 taps"), ChainCount);
	float PipLabelW = 0.f;
	float PipLabelH = 0.f;
	GetTextSize(PipLabel, PipLabelW, PipLabelH, Font, Scale * 1.15f);
	DrawText(PipLabel, Text, LastPip.Max.X + 14.f * Scale, LastPip.Min.Y + (LastPip.GetSize().Y - PipLabelH) * 0.5f, Font, Scale * 1.15f);
}
