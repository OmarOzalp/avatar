// WebAssembly bindings for the 3D sandbox: one global FSandbox, flat exports for the browser client.
// Built freestanding (no libc, no libc++) by Tools/Sandbox3D/build_wasm.sh.

#include "BendingSandbox3D.h"
#include "SandboxScript.h"

using namespace BendingSim;
using namespace BendingSandbox3D;

#define SB_EXPORT(Name) extern "C" __attribute__((export_name(#Name)))

// Placement new without <new> (freestanding build).
inline void* operator new(decltype(sizeof(0)), void* Where) noexcept { return Where; }

namespace
{
	constexpr int PlayerFields = 50;
	constexpr int BodyStride = 30;
	constexpr int VolumeStride = 18;
	constexpr int WhipStride = 6;
	constexpr int PatchStride = 5;
	constexpr int PondStride = 6;
	constexpr int EventStride = 7;
	constexpr int TechniqueStride = 8;

	// Zero-initialized storage, constructed in place on first use: the sandbox is ~1.7 MB and must not land in the
	// module's data segment or on the 1 MB stack.
	alignas(FSandbox) unsigned char GStorage[sizeof(FSandbox)];
	FSandbox* GSandbox = nullptr;

	FSandbox& Sandbox()
	{
		if (!GSandbox)
		{
			GSandbox = new (GStorage) FSandbox();
			GSandbox->Init();
		}
		return *GSandbox;
	}

	FInput GInput;
	double GPlayer[PlayerFields];
	double GBodies[FSandbox::MaxBodies * BodyStride];
	double GVolumes[FSimWorld::MaxVolumes * VolumeStride];
	double GWhip[FWaterWhip::MaxPoints * WhipStride];
	double GPatches[FSimWorld::MaxMoisturePatches * PatchStride];
	double GPonds[FArenaLayout::MaxPonds * PondStride];
	double GEvents[FSandbox::MaxFrameEvents * EventStride];
	double GTerrainInfo[8];
	int GDirty[4];
	double GTechnique[TechniqueStride];
	double GStats[4];
}

SB_EXPORT(sb_init) void SbInit() { Sandbox(); }
SB_EXPORT(sb_reset) void SbReset() { Sandbox().Init(); }

/** Buttons: bit 0 LMB, 1 RMB, 2 Q, 3 E, 4 jump, 5 sprint. Stance: 0 = keep, else ETechniqueElement (1 Earth, 2 Water, 3 Fire, 4 Air). */
SB_EXPORT(sb_set_input) void SbSetInput(double MoveForward, double MoveRight, double CamX, double CamY, double CamZ,
	double DirX, double DirY, double DirZ, int Buttons, int Stance)
{
	GInput.MoveForward = MoveForward;
	GInput.MoveRight = MoveRight;
	GInput.CameraLocationCm = FVec3(CamX, CamY, CamZ);
	GInput.CameraForward = FVec3(DirX, DirY, DirZ);
	// Bits 0-3: LMB, RMB, Q, E; bit 4 jump, bit 5 sprint; bit 6: F (signature).
	for (int Slot = 0; Slot < static_cast<int>(ETechniqueSlot::Count); ++Slot)
	{
		GInput.bSlotHeld[Slot] = (Buttons >> (Slot < 4 ? Slot : Slot + 2)) & 1;
	}
	GInput.bJump = (Buttons >> 4) & 1;
	GInput.bSprint = (Buttons >> 5) & 1;
	GInput.StanceRequest = Stance;
}

SB_EXPORT(sb_advance) void SbAdvance(double FrameSeconds)
{
	Sandbox().Advance(GInput, FrameSeconds);
	GInput.StanceRequest = 0;
}

SB_EXPORT(sb_player) double* SbPlayer()
{
	const FSandbox& S = Sandbox();
	const FPlayer& P = S.Player;
	const FTechniqueInfo& Move = GetTechniqueInfo(P.Move);
	double* O = GPlayer;
	O[0] = P.LocationCm.X;
	O[1] = P.LocationCm.Y;
	O[2] = P.LocationCm.Z;
	O[3] = P.YawRad;
	O[4] = P.VelocityCmS.X;
	O[5] = P.VelocityCmS.Y;
	O[6] = P.VelocityCmS.Z;
	O[7] = P.bGrounded ? 1.0 : 0.0;
	O[8] = static_cast<double>(P.Stance);
	O[9] = static_cast<double>(P.Move);
	O[10] = static_cast<double>(P.Phase);
	O[11] = S.GetPhaseProgress();
	O[12] = static_cast<double>(P.HoldTechnique);
	O[13] = P.Chi;
	O[14] = P.MaxChi;
	O[15] = P.Stamina;
	O[16] = P.MaxStamina;
	O[17] = P.Traction;
	O[18] = P.AimPointCm.X;
	O[19] = P.AimPointCm.Y;
	O[20] = P.AimPointCm.Z;
	O[21] = P.bAimOnSurface ? 1.0 : 0.0;
	O[22] = P.HandCm.X;
	O[23] = P.HandCm.Y;
	O[24] = P.HandCm.Z;
	O[25] = P.StridePhase;
	O[26] = S.IsInCancelWindow() ? 1.0 : 0.0;
	O[27] = S.ThermalWorkJ;
	O[28] = S.KineticWorkJ;
	O[29] = P.ChiSpentTotal;
	O[30] = S.TimeS;
	O[31] = Move.StartupFrames;
	O[32] = Move.ActiveFrames;
	O[33] = Move.RecoveryFrames;
	O[34] = P.PhaseTimeS * 60.0;
	O[35] = static_cast<double>(P.BufferedMove);
	O[36] = S.Whip.IsActive() ? 1.0 : 0.0;
	O[37] = S.Whip.IsActive() ? S.Whip.GetMassKg(S.World) : 0.0;
	O[38] = S.Whip.IsActive() && S.Whip.IsAnyFrozen(S.World) ? 1.0 : 0.0;
	O[39] = static_cast<double>(S.Whip.GetState());
	// Tornado (age < 0: none), air scooter, jet dash.
	O[40] = S.Tornado.bActive ? S.Tornado.AgeS : -1.0;
	O[41] = S.Tornado.CenterCm.X;
	O[42] = S.Tornado.CenterCm.Y;
	O[43] = S.Tornado.CenterCm.Z;
	O[44] = S.Tuning.TornadoRadiusCm;
	O[45] = S.Tornado.FireKg;
	O[46] = P.bScooter ? 1.0 : 0.0;
	O[47] = P.DashTimeS;
	O[48] = S.Tornado.LifetimeS;
	O[49] = 0.0;
	return GPlayer;
}

SB_EXPORT(sb_pack_bodies) int SbPackBodies()
{
	const FSandbox& S = Sandbox();
	int Count = 0;
	for (int Index = 0; Index < S.NumBodies; ++Index)
	{
		const FBody& B = S.Bodies[Index];
		if (!B.bAlive)
		{
			continue;
		}
		const FVolume* Volume = S.World.GetVolume(B.Volume);
		double* O = &GBodies[Count++ * BodyStride];
		O[0] = static_cast<double>(B.Kind);
		O[1] = static_cast<double>(B.Prop);
		O[2] = B.LocationCm.X;
		O[3] = B.LocationCm.Y;
		O[4] = B.LocationCm.Z;
		O[5] = B.Rotation[0];
		O[6] = B.Rotation[1];
		O[7] = B.Rotation[2];
		O[8] = B.Rotation[3];
		O[9] = B.RadiusCm;
		O[10] = B.HalfHeightCm;
		O[11] = B.MassKg;
		O[12] = Volume ? static_cast<double>(Volume->Substance) : 0.0;
		O[13] = Volume ? Volume->Saturation : 0.0;
		O[14] = B.Tilt[0];
		O[15] = B.Tilt[1];
		O[16] = B.bLit ? 1.0 : 0.0;
		O[17] = B.AnchorCm.X;
		O[18] = B.AnchorCm.Y;
		O[19] = B.AnchorCm.Z;
		O[20] = Index;
		// Burning props: 0 intact, 1 burning, 2 burnt away; how much is burnt; how close to catching; wetness.
		const FArenaPropSpec& Spec = GetArenaPropSpec(B.Prop);
		const bool bProp = B.Kind == EBodyKind::Prop;
		O[21] = !bProp ? 0.0 : (B.Burn.bBurntOut ? 2.0 : (B.Burn.bBurning ? 1.0 : 0.0));
		O[22] = bProp ? B.Burn.GetBurntFraction(Spec.Combustion) : 0.0;
		O[23] = bProp ? B.Burn.GetHeatFraction(Spec.Combustion) : 0.0;
		O[24] = B.Burn.WetS;
		O[25] = B.WaterKg;
		O[26] = B.YawRad;
		O[27] = static_cast<double>(B.Variant);
		// Dummies: health (0..1) and seconds left knocked out.
		O[28] = B.Health / FSandbox::DummyMaxHealth;
		O[29] = B.KnockoutS;
	}
	return Count;
}
SB_EXPORT(sb_bodies) double* SbBodies() { return GBodies; }

/** Every volume in the world. Owner: 0 free (steam), 1 body, 2 whip, 3 projectile. */
SB_EXPORT(sb_pack_volumes) int SbPackVolumes()
{
	const FSandbox& S = Sandbox();
	int Count = 0;
	for (int Slot = 0; Slot < S.World.GetSlotLimit(); ++Slot)
	{
		const FHandle Handle = S.World.GetHandleAt(Slot);
		const FVolume* V = S.World.GetVolume(Handle);
		if (!V || S.World.IsDepleted(Handle))
		{
			continue;
		}
		const int Projectile = S.FindProjectileByVolume(Handle);
		double Owner = 0.0;
		if (Projectile >= 0)
		{
			Owner = 3.0;
		}
		else if (S.IsWhipVolume(Handle))
		{
			Owner = 2.0;
		}
		else if (S.FindBodyByVolume(Handle))
		{
			Owner = 1.0;
		}
		double* O = &GVolumes[Count++ * VolumeStride];
		O[0] = Owner;
		O[1] = static_cast<double>(V->Substance);
		O[2] = V->LocationCm.X;
		O[3] = V->LocationCm.Y;
		O[4] = V->LocationCm.Z;
		O[5] = V->RadiusCm;
		O[6] = V->TemperatureK;
		O[7] = V->MassKg;
		O[8] = V->VelocityCmS.X;
		O[9] = V->VelocityCmS.Y;
		O[10] = V->VelocityCmS.Z;
		O[11] = V->Shape == EShape::Capsule ? V->CapsuleHalfAxisCm.X : 0.0;
		O[12] = V->Shape == EShape::Capsule ? V->CapsuleHalfAxisCm.Y : 0.0;
		O[13] = V->Shape == EShape::Capsule ? V->CapsuleHalfAxisCm.Z : 0.0;
		O[14] = Projectile >= 0 ? static_cast<double>(S.Projectiles[Projectile].Kind) : -1.0;
		O[15] = Projectile >= 0 && S.Projectiles[Projectile].bLanded ? 1.0 : 0.0;
		// The projectile's slot (stable while it lives) and age, so presentation can keep per-projectile effects.
		O[16] = Projectile >= 0 ? static_cast<double>(Projectile) : -1.0;
		O[17] = Projectile >= 0 ? S.Projectiles[Projectile].AgeS : 0.0;
	}
	return Count;
}
SB_EXPORT(sb_volumes) double* SbVolumes() { return GVolumes; }

SB_EXPORT(sb_pack_whip) int SbPackWhip()
{
	const FSandbox& S = Sandbox();
	if (!S.Whip.IsActive())
	{
		return 0;
	}
	const int Count = S.Whip.GetNumPoints();
	for (int Index = 0; Index < Count; ++Index)
	{
		const FVec3& P = S.Whip.GetPoint(Index);
		const int Segment = Index < S.Whip.GetNumSegments() ? Index : S.Whip.GetNumSegments() - 1;
		const FVolume* V = S.World.GetVolume(S.Whip.GetSegmentHandle(Segment));
		double* O = &GWhip[Index * WhipStride];
		O[0] = P.X;
		O[1] = P.Y;
		O[2] = P.Z;
		O[3] = S.Whip.GetSegmentRadiusCm(Segment);
		O[4] = V && V->Substance == ESubstance::Ice ? 1.0 : 0.0;
		O[5] = S.Whip.GetPointVelocity(Index).Size();
	}
	return Count;
}
SB_EXPORT(sb_whip) double* SbWhip() { return GWhip; }

SB_EXPORT(sb_terrain_info) double* SbTerrainInfo()
{
	const FTerrain& T = Sandbox().Terrain;
	GTerrainInfo[0] = T.GetSamplesX();
	GTerrainInfo[1] = T.GetSamplesY();
	GTerrainInfo[2] = T.GetCellSizeCm();
	GTerrainInfo[3] = T.GetOriginCm().X;
	GTerrainInfo[4] = T.GetOriginCm().Y;
	GTerrainInfo[5] = T.GetBaseHeightCm();
	GTerrainInfo[6] = T.SoilPorosity;
	GTerrainInfo[7] = T.GetBedrockHeightCm();
	return GTerrainInfo;
}
SB_EXPORT(sb_terrain_heights) const double* SbTerrainHeights() { return Sandbox().Terrain.GetHeightData(); }
SB_EXPORT(sb_terrain_original) const double* SbTerrainOriginal() { return Sandbox().OriginalHeights; }
/** 1 when terrain changed since the last call; the rectangle (min x, min y, max x, max y) is in sb_dirty_rect. */
SB_EXPORT(sb_terrain_dirty) int SbTerrainDirty()
{
	return Sandbox().Terrain.ConsumeDirtyRegion(GDirty[0], GDirty[1], GDirty[2], GDirty[3]) ? 1 : 0;
}
SB_EXPORT(sb_dirty_rect) int* SbDirtyRect() { return GDirty; }

SB_EXPORT(sb_pack_patches) int SbPackPatches()
{
	const FSandbox& S = Sandbox();
	const int Count = S.World.GetNumMoisturePatches();
	for (int Index = 0; Index < Count; ++Index)
	{
		const FMoisturePatch& Patch = S.World.GetMoisturePatch(Index);
		double* O = &GPatches[Index * PatchStride];
		O[0] = Patch.CenterCm.X;
		O[1] = Patch.CenterCm.Y;
		O[2] = Patch.CenterCm.Z;
		O[3] = Patch.RadiusCm;
		O[4] = Patch.GetSaturation(S.World.Settings.MoistureSoilDepthM);
	}
	return Count;
}
SB_EXPORT(sb_patches) double* SbPatches() { return GPatches; }

SB_EXPORT(sb_pack_ponds) int SbPackPonds()
{
	const FSandbox& S = Sandbox();
	for (int Index = 0; Index < S.Layout.NumPonds; ++Index)
	{
		const FArenaPond& Pond = S.Layout.Ponds[Index];
		double* O = &GPonds[Index * PondStride];
		O[0] = Pond.CenterCm.X;
		O[1] = Pond.CenterCm.Y;
		O[2] = Pond.SurfaceHeightCm;
		O[3] = Pond.RadiusCm;
		O[4] = Pond.WaterKg;
		O[5] = Pond.WaterDepthCm;
	}
	return S.Layout.NumPonds;
}
SB_EXPORT(sb_ponds) double* SbPonds() { return GPonds; }

/** Events of the last frame: reactions plus splashes (type Saturation, discrete). */
SB_EXPORT(sb_pack_events) int SbPackEvents()
{
	const FSandbox& S = Sandbox();
	for (int Index = 0; Index < S.NumFrameEvents; ++Index)
	{
		const FSandboxEvent& E = S.FrameEvents[Index];
		double* O = &GEvents[Index * EventStride];
		// Presentation effects are numbered from 100, after the simulation's reaction types.
		O[0] = E.Effect != ESandboxEffect::None ? 100.0 + static_cast<double>(E.Effect) : static_cast<double>(E.Type);
		O[1] = E.LocationCm.X;
		O[2] = E.LocationCm.Y;
		O[3] = E.LocationCm.Z;
		O[4] = E.MassKg;
		O[5] = E.EnergyJ;
		O[6] = E.bDiscrete ? 1.0 : 0.0;
	}
	return S.NumFrameEvents;
}
SB_EXPORT(sb_events) double* SbEvents() { return GEvents; }

SB_EXPORT(sb_message_count) int SbMessageCount() { return Sandbox().NumMessages; }
SB_EXPORT(sb_message_serial) int SbMessageSerial() { return Sandbox().MessageSerial; }
SB_EXPORT(sb_message_text) const char* SbMessageText(int Index) { return Sandbox().Messages[Index].Text; }
SB_EXPORT(sb_message_time) double SbMessageTime(int Index) { return Sandbox().Messages[Index].TimeS; }

SB_EXPORT(sb_technique_count) int SbTechniqueCount() { return static_cast<int>(ETechnique::Count); }
SB_EXPORT(sb_technique_name) const char* SbTechniqueName(int Id) { return GetTechniqueInfo(static_cast<ETechnique>(Id)).Name; }
SB_EXPORT(sb_technique_description) const char* SbTechniqueDescription(int Id) { return GetTechniqueInfo(static_cast<ETechnique>(Id)).Description; }
/** element, slot, startup, active, recovery, chi, stamina, hold. */
SB_EXPORT(sb_technique_info) double* SbTechniqueInfo(int Id)
{
	const FTechniqueInfo& Info = GetTechniqueInfo(static_cast<ETechnique>(Id));
	GTechnique[0] = static_cast<double>(Info.Element);
	GTechnique[1] = static_cast<double>(Info.Slot);
	GTechnique[2] = Info.StartupFrames;
	GTechnique[3] = Info.ActiveFrames;
	GTechnique[4] = Info.RecoveryFrames;
	GTechnique[5] = Info.ChiCost;
	GTechnique[6] = Info.StaminaCost;
	GTechnique[7] = Info.bHold ? 1.0 : 0.0;
	return GTechnique;
}
SB_EXPORT(sb_find_technique) int SbFindTechnique(int Element, int Slot)
{
	return static_cast<int>(FindTechnique(static_cast<ETechniqueElement>(Element), static_cast<ETechniqueSlot>(Slot)));
}

SB_EXPORT(sb_stats) double* SbStats()
{
	const FSandbox& S = Sandbox();
	GStats[0] = S.World.GetNumVolumes();
	GStats[1] = S.World.GetStats().ContactsLastStep;
	GStats[2] = static_cast<double>(S.World.GetStats().Steps);
	GStats[3] = S.World.GetNumMoisturePatches();
	return GStats;
}

/** Tests: runs the canonical scripted session on a fresh training ground and returns its digest (compare with native). */
SB_EXPORT(sb_run_scripted_session) double SbRunScriptedSession()
{
	FSandbox& S = Sandbox();
	S.Init();
	RunScriptedSession(S);
	return SandboxDigest(S);
}
