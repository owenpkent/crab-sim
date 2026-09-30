// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"

/**
 * Orbiting the camera round the crab: hold the right button and drag. Sideways travel turns the view round the
 * crab (yaw, no limit), up and down travel swings the camera lower or higher over it (pitch, clamped between near
 * the sand and near straight down). A right click that does not move is still a dash, fired when the button comes
 * up. What tells the two apart is how far the pointer moved, never how long the button was down, so there is no
 * timing window. Pure rules: the controller passes the button and the pointer in every frame, so a headless test
 * drives it exactly as the game does. See GAME.md, "Camera".
 */
namespace CrabOrbit
{
	/** The camera's pitch never goes higher than this, degrees (negative looks down): just short of straight down. */
	constexpr float MinPitch = -85.f;
	/** Nor lower than this: low over the sand, looking across the beach, with the crab still in view. */
	constexpr float MaxPitch = -10.f;

	inline float ClampPitch(float Pitch)
	{
		return FMath::Clamp(Pitch, MinPitch, MaxPitch);
	}

	/** All are also CVars (CrabSim.Orbit*), copied in each frame. */
	struct FTuning
	{
		/** How far the pointer must move from the press, px, before a right hold becomes an orbit. Under it, the
		 * release is a right click. Room for a hand that wobbles on the button. */
		float StartPixels = 10.f;
		/** Camera yaw per pixel of sideways pointer travel while orbiting, degrees. At 0.3 a drag across a 1280 px
		 * view is more than a full turn. Pointer right turns the view right. Negative turns the other way. */
		float DegreesPerPixel = 0.3f;
		/** Camera pitch per pixel of up and down travel, degrees. Pointer up brings the camera down toward the sand,
		 * looking across; pointer down lifts it over the crab, looking down. Negative swaps the two. */
		float PitchDegreesPerPixel = 0.3f;
	};

	/** What one frame of the right button did. */
	struct FStep
	{
		/** Add this to the camera's yaw, degrees. */
		float YawDelta = 0.f;
		/** Add this to the camera's pitch, degrees, then clamp it (ClampPitch). */
		float PitchDelta = 0.f;
		/** The button came up without orbiting: a right click, so dash. */
		bool bClick = false;
	};

	class FDrag
	{
	public:
		/** One frame: whether the right button is down, and where the pointer is. */
		FStep Update(bool bDown, const FVector2D& Pointer, const FTuning& Tuning)
		{
			FStep Step;
			if (!bDown)
			{
				Step.bClick = bHeld && !bOrbiting;
				bHeld = false;
				bOrbiting = false;
				return Step;
			}
			if (!bHeld)
			{
				bHeld = true;
				bOrbiting = false;
				Anchor = Pointer;
				Last = Pointer;
				return Step;
			}
			if (!bOrbiting && FVector2D::Distance(Pointer, Anchor) > Tuning.StartPixels)
			{
				// The travel so far counts too, so the view does not jump when the orbit starts.
				bOrbiting = true;
				Last = Anchor;
			}
			if (bOrbiting)
			{
				const FVector2D Moved = Pointer - Last;
				Step.YawDelta = static_cast<float>(Moved.X) * Tuning.DegreesPerPixel;
				Step.PitchDelta = static_cast<float>(-Moved.Y) * Tuning.PitchDegreesPerPixel;
			}
			Last = Pointer;
			return Step;
		}

		bool IsHeld() const { return bHeld; }
		bool IsOrbiting() const { return bOrbiting; }
		/** The pointer as of the last frame, for a frame where it is not known. */
		FVector2D GetLastPointer() const { return Last; }

	private:
		FVector2D Anchor = FVector2D::ZeroVector;
		FVector2D Last = FVector2D::ZeroVector;
		bool bHeld = false;
		bool bOrbiting = false;
	};

	/**
	 * A screen-relative direction (x right, y up the screen) as a world ground direction, for a camera at this yaw.
	 * Up the screen is the camera's forward and right is its right, so at yaw 0 up is world +X and right is +Y.
	 */
	inline FVector2D ScreenToWorld(const FVector2D& Screen, float CameraYaw)
	{
		const float Radians = FMath::DegreesToRadians(CameraYaw);
		const FVector2D Forward(FMath::Cos(Radians), FMath::Sin(Radians));
		const FVector2D Right(-Forward.Y, Forward.X);
		return Forward * Screen.Y + Right * Screen.X;
	}

	/** The inverse: a world ground offset in the camera's frame, x its right and y its forward. */
	inline FVector2D WorldToCamera(const FVector2D& World, float CameraYaw)
	{
		const float Radians = FMath::DegreesToRadians(CameraYaw);
		const FVector2D Forward(FMath::Cos(Radians), FMath::Sin(Radians));
		const FVector2D Right(-Forward.Y, Forward.X);
		return FVector2D(FVector2D::DotProduct(World, Right), FVector2D::DotProduct(World, Forward));
	}

	/** How much the ground is squashed up the screen at this pitch: 1 looking straight down, small looking across. */
	inline float GroundForeshortening(float CameraPitch)
	{
		return FMath::Max(FMath::Abs(FMath::Sin(FMath::DegreesToRadians(CameraPitch))), 0.1f);
	}
}
