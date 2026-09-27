// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * How big a burrow or food patch is to click, on screen. A click zone keeps its world radius, and grows
 * to at least a minimum size in pixels, so a target far from the camera is still a big target. The zone
 * is an ellipse, not a box, so the ground between two targets still walks.
 */
namespace CrabPick
{
	/** The smallest a zone is on screen, px at scale 1 (a 720p window). The HUD scale grows it with the window. */
	constexpr float MinZoneWidth = 90.f;
	constexpr float MinZoneHeight = 60.f;

	/** Half the axes of a zone on screen, in px: the world radius as it appears there, or the minimum, whichever is larger on each axis. */
	inline FVector2D ZoneHalfAxes(const FVector2D& ProjectedHalfAxes, float Scale)
	{
		return FVector2D(FMath::Max(ProjectedHalfAxes.X, 0.5f * MinZoneWidth * Scale), FMath::Max(ProjectedHalfAxes.Y, 0.5f * MinZoneHeight * Scale));
	}

	/**
	 * How a pixel sits against an ellipse: 0 in the middle, 1 on the edge, more outside. Zero half axes count
	 * as a point.
	 */
	inline float EllipseDistance(const FVector2D& Pixel, const FVector2D& Centre, const FVector2D& HalfAxes)
	{
		const float DX = (Pixel.X - Centre.X) / FMath::Max(HalfAxes.X, KINDA_SMALL_NUMBER);
		const float DY = (Pixel.Y - Centre.Y) / FMath::Max(HalfAxes.Y, KINDA_SMALL_NUMBER);
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	/**
	 * Where a ground point and the circle of WorldRadius round it fall on screen: the point's pixel and how far
	 * the circle reaches from it, sideways and up and down. Project is a callable (World, OutPixel) that returns
	 * false for a point the camera cannot see. False here if the middle cannot be projected.
	 */
	template <typename ProjectFn>
	inline bool ProjectZone(const ProjectFn& Project, const FVector& Ground, float WorldRadius, FVector2D& OutCentre, FVector2D& OutProjectedHalfAxes)
	{
		OutProjectedHalfAxes = FVector2D::ZeroVector;
		if (!Project(Ground, OutCentre))
		{
			return false;
		}
		const FVector Offsets[] = {FVector(WorldRadius, 0.f, 0.f), FVector(0.f, WorldRadius, 0.f)};
		for (const FVector& Offset : Offsets)
		{
			FVector2D Reach;
			if (Project(Ground + Offset, Reach))
			{
				OutProjectedHalfAxes.X = FMath::Max(OutProjectedHalfAxes.X, static_cast<float>(FMath::Abs(Reach.X - OutCentre.X)));
				OutProjectedHalfAxes.Y = FMath::Max(OutProjectedHalfAxes.Y, static_cast<float>(FMath::Abs(Reach.Y - OutCentre.Y)));
			}
		}
		return true;
	}

	/**
	 * How a pixel sits against a target's click zone on screen: below 1 inside (the score to rank targets by),
	 * 1 on the edge. False, and OutScore untouched, if the target cannot be projected.
	 */
	template <typename ProjectFn>
	inline bool ZoneScore(const ProjectFn& Project, const FVector& Ground, float WorldRadius, float Scale, const FVector2D& Pixel, float& OutScore)
	{
		FVector2D Centre;
		FVector2D Projected;
		if (!ProjectZone(Project, Ground, WorldRadius, Centre, Projected))
		{
			return false;
		}
		OutScore = EllipseDistance(Pixel, Centre, ZoneHalfAxes(Projected, Scale));
		return true;
	}
}
