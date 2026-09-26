// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * The beach's shape as a pure function, so the mesh, the tide rules and the
 * tests all agree on where the ground is. The sea is on +X and the dunes on -X.
 * The camera looks along +X, so the sea is ahead of the crab and the dunes are
 * behind it. Units are uu (cm).
 */
namespace CrabTerrain
{
	constexpr float MinX = -4200.f;
	constexpr float MaxX = 9000.f;
	constexpr float HalfWidthY = 3200.f;

	/** Ground height at a world XY. */
	inline float Height(float X, float Y)
	{
		// A gentle slope down to the sea.
		float H = -0.06f * X - 40.f;

		// Dunes rise behind the beach, more ragged the higher they go.
		const float DuneT = FMath::Clamp((-2200.f - X) / 1400.f, 0.f, 1.f);
		H += FMath::SmoothStep(0.f, 1.f, DuneT) * (260.f + 140.f * FMath::PerlinNoise2D(FVector2D(X, Y) / 600.0));

		// A tidal creek winds across the flats and holds water after the tide drops.
		const float CreekCentreY = 420.f * FMath::Sin(X / 1100.f + 0.7f);
		const float CreekOffset = Y - CreekCentreY;
		const float CreekMask = FMath::SmoothStep(-2100.f, -1500.f, X) * (1.f - FMath::SmoothStep(4000.f, 5500.f, X));
		H -= 55.f * FMath::Exp(-(CreekOffset * CreekOffset) / (170.f * 170.f)) * CreekMask;

		// Broad undulation so the flats are not a ramp.
		H += 22.f * FMath::PerlinNoise2D(FVector2D(X, Y) / 750.0 + FVector2D(11.3, 4.7));

		// The sides rise into steep banks that keep the crab on the beach.
		H += FMath::SmoothStep(2500.f, 3200.f, FMath::Abs(Y)) * 900.f;
		return H;
	}

	/** Upward unit surface normal, from central differences. */
	inline FVector Normal(float X, float Y, float Step = 20.f)
	{
		const float DX = Height(X + Step, Y) - Height(X - Step, Y);
		const float DY = Height(X, Y + Step) - Height(X, Y - Step);
		return FVector(-DX, -DY, 2.f * Step).GetSafeNormal();
	}
}
