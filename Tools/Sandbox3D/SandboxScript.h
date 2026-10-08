#pragma once

#include "BendingSandbox3D.h"

/**
 * A scripted minute of play (walk, draw a whip, lash, freeze, thaw, blast, wall, rock, fire, air) driven through
 * the same input the browser sends. The native tests and the WebAssembly build both run it, and its digest must
 * match bit for bit.
 */
namespace BendingSandbox3D
{
	namespace Script
	{
		inline FInput LookAt(const FSandbox& S, const FVec3& TargetCm)
		{
			FInput Input;
			Input.CameraLocationCm = S.Player.LocationCm + FVec3(0.0, 0.0, 260.0);
			Input.CameraForward = (TargetCm - Input.CameraLocationCm).GetSafeNormal();
			return Input;
		}

		inline void Run(FSandbox& S, const FInput& Input, double Seconds)
		{
			const int Frames = static_cast<int>(Seconds * 60.0 + 0.5);
			for (int Frame = 0; Frame < Frames; ++Frame)
			{
				S.Advance(Input, 1.0 / 60.0);
			}
		}

		inline void Press(FSandbox& S, ETechniqueSlot Slot, const FVec3& TargetCm, double Seconds)
		{
			FInput Input = LookAt(S, TargetCm);
			Input.bSlotHeld[static_cast<int>(Slot)] = true;
			S.Advance(Input, 1.0 / 60.0);
			Input.bSlotHeld[static_cast<int>(Slot)] = false;
			Run(S, Input, Seconds);
		}

		inline void Hold(FSandbox& S, ETechniqueSlot Slot, const FVec3& TargetCm, double Seconds)
		{
			FInput Input = LookAt(S, TargetCm);
			Input.bSlotHeld[static_cast<int>(Slot)] = true;
			Run(S, Input, Seconds);
			Input.bSlotHeld[static_cast<int>(Slot)] = false;
			S.Advance(Input, 1.0 / 60.0);
		}

		inline void SetStance(FSandbox& S, ETechniqueElement Element)
		{
			FInput Input = LookAt(S, S.Player.LocationCm + FVec3(1000.0, 0.0, 0.0));
			Input.StanceRequest = static_cast<int>(Element);
			S.Advance(Input, 1.0 / 60.0);
		}

		inline void Teleport(FSandbox& S, double X, double Y, double YawDeg)
		{
			S.Player.LocationCm = FVec3(X, Y, S.Terrain.GetHeightAt(X, Y));
			S.Player.VelocityCmS = FVec3();
			S.Player.YawRad = YawDeg * Pi / 180.0;
			S.Player.bGrounded = true;
		}

		inline FVec3 Ground(const FSandbox& S, double X, double Y)
		{
			return FVec3(X, Y, S.Terrain.GetHeightAt(X, Y));
		}
	}

	inline void RunScriptedSession(FSandbox& S)
	{
		using namespace Script;
		const FVec3 Up(0.0, 0.0, 100.0);
		Teleport(S, 700.0, 450.0, 90.0);
		SetStance(S, ETechniqueElement::Water);
		Press(S, ETechniqueSlot::Primary, Ground(S, 700.0, -400.0), 1.2);
		FInput Walk = LookAt(S, Ground(S, 2000.0, -300.0));
		Walk.MoveForward = 1.0;
		Run(S, Walk, 2.0);
		Press(S, ETechniqueSlot::Primary, Ground(S, 2300.0, -250.0), 0.8);
		Press(S, ETechniqueSlot::Secondary, Ground(S, 2300.0, -250.0), 0.8);
		Press(S, ETechniqueSlot::Secondary, Ground(S, 2300.0, -250.0), 0.8);
		Press(S, ETechniqueSlot::Utility, Ground(S, 2300.0, 250.0), 1.0);
		SetStance(S, ETechniqueElement::Earth);
		Press(S, ETechniqueSlot::Secondary, Ground(S, 1800.0, 600.0), 1.0);
		Press(S, ETechniqueSlot::Primary, Ground(S, 2600.0, 0.0), 1.5);
		Hold(S, ETechniqueSlot::Special, Ground(S, 1500.0, -200.0), 1.0);
		SetStance(S, ETechniqueElement::Fire);
		Hold(S, ETechniqueSlot::Secondary, Ground(S, 2650.0, 0.0) + Up, 1.0);
		SetStance(S, ETechniqueElement::Air);
		Press(S, ETechniqueSlot::Primary, Ground(S, 3000.0, 350.0) + Up, 1.0);
		Hold(S, ETechniqueSlot::Secondary, Ground(S, 3000.0, -350.0) + Up, 1.0);
		Press(S, ETechniqueSlot::Utility, Ground(S, 3000.0, 0.0), 2.0);
	}

	/** Order-dependent sum over the player, bodies and terrain: equal digests mean bit-identical worlds in practice. */
	inline double SandboxDigest(const FSandbox& S)
	{
		double Sum = S.Player.LocationCm.X * 1.3 + S.Player.LocationCm.Y * 1.7 + S.Player.LocationCm.Z + S.Player.Chi * 11.0;
		for (int Index = 0; Index < S.NumBodies; ++Index)
		{
			if (S.Bodies[Index].bAlive)
			{
				Sum += (Index + 1) * (S.Bodies[Index].LocationCm.X + 2.0 * S.Bodies[Index].LocationCm.Y + 3.0 * S.Bodies[Index].LocationCm.Z);
			}
		}
		const double* Heights = S.Terrain.GetHeightData();
		for (int Index = 0; Index < S.Terrain.GetSamplesX() * S.Terrain.GetSamplesY(); Index += 13)
		{
			Sum += Heights[Index] * ((Index % 7) + 1);
		}
		return Sum;
	}
}
