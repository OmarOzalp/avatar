#include "Sim/BendingSimReactions.h"

#include "Sim/BendingSimWorld.h"

namespace BendingSim
{
	void FReactionContext::EmitEvent(EReactionType Type, double MassKg, double EnergyJ) const
	{
		World.EmitPairEvent(*this, Type, MassKg, EnergyJ);
	}

	void FReactionContext::SpawnFreeVolume(const FVolume& Volume) const
	{
		World.QueueFreeVolume(Volume);
	}

	// ---------------------------------------------------------------------------------------------------- Heat exchange

	void ReactHeatExchange(const FHeatExchangeParams& Params, FReactionContext& Context)
	{
		FVolume& A = Context.A;
		FVolume& B = Context.B;
		const FContact& Contact = Context.Contact;
		const double DeltaSeconds = Context.DeltaSeconds;

		// Momentum: interpenetrating matter drags itself toward a common velocity (water smothers a fire jet).
		const double Coupling = (1.0 - KExp(-DeltaSeconds / KMax(Params.MomentumCouplingTimeS, 1e-4))) * Contact.OverlapFraction * Params.Strength;
		const FVec3 ImpulseOnA = MomentumCouplingImpulse(A, B, KMin(Coupling, 1.0));
		A.PendingImpulseKgCmS += ImpulseOnA;
		B.PendingImpulseKgCmS -= ImpulseOnA;

		const double DeltaT = A.TemperatureK - B.TemperatureK;
		if (KAbs(DeltaT) < 0.01)
		{
			return;
		}
		FVolume& Hot = DeltaT > 0.0 ? A : B;
		FVolume& Cold = DeltaT > 0.0 ? B : A;

		double HeatJ = Params.HeatTransferCoefficient * Contact.ExchangeAreaM2 * KAbs(DeltaT) * DeltaSeconds
			* Params.Strength * Context.Settings.HeatTransferScale;
		HeatJ = KMin(HeatJ, MaxHeatFlow(Hot, Cold));
		if (HeatJ <= 0.0)
		{
			return;
		}

		// Vapor leaves with the velocity of the liquid it boiled off, which keeps momentum exactly conserved.
		const FVec3 VaporVelocityCmS = Cold.VelocityCmS;

		double VaporizedKg = 0.0;
		double BulkHeatJ = HeatJ;
		if (Cold.Substance == ESubstance::Water && Hot.TemperatureK > Thermo::Constants::WaterBoilingPointK)
		{
			const double FlashHeatJ = HeatJ * Params.SurfaceFlashFraction;
			VaporizedKg += FlashVaporize(Cold, FlashHeatJ);
			BulkHeatJ -= FlashHeatJ;
		}
		const Thermo::FPhaseChangeResult ColdChange = AddHeat(Cold, BulkHeatJ);
		const Thermo::FPhaseChangeResult HotChange = AddHeat(Hot, -HeatJ);
		VaporizedKg += ColdChange.VaporizedKg;

		if (VaporizedKg > 0.0)
		{
			Context.EmitEvent(EReactionType::Evaporation, VaporizedKg, HeatJ);
			FVolume Steam = FVolume::MakeDefault(ESubstance::Steam, VaporizedKg);
			Steam.LocationCm = Contact.PointCm;
			Steam.VelocityCmS = VaporVelocityCmS;
			Context.SpawnFreeVolume(Steam);
		}
		if (ColdChange.MeltedKg > 0.0)
		{
			Context.EmitEvent(EReactionType::Melting, ColdChange.MeltedKg, HeatJ);
		}
		if (HotChange.CondensedKg > 0.0)
		{
			Context.EmitEvent(EReactionType::Condensation, HotChange.CondensedKg, HeatJ);
		}
	}

	// ---------------------------------------------------------------------------------------------------- Oxygenation

	void ReactOxygenation(const FOxygenationParams& Params, FReactionContext& Context)
	{
		FVolume& Air = Context.A;
		FVolume& Fire = Context.B;
		const FContact& Contact = Context.Contact;
		const double DeltaSeconds = Context.DeltaSeconds;

		// Air entering through the contact surface: normal component of the relative flow (normal points air -> fire).
		const FVec3 RelativeFlowMs = CmToM(Air.VelocityCmS - Fire.VelocityCmS);
		const double InflowSpeedMs = KMax(RelativeFlowMs.Dot(Contact.NormalAB), 0.0) + Params.MixingEntrainmentSpeedMs;

		double AirMassKg = Air.GetDensityKgM3() * Contact.ExchangeAreaM2 * InflowSpeedMs * DeltaSeconds * Params.Strength;
		AirMassKg = KMin(AirMassKg, Params.MaxEntrainmentPerFlameKgPerS * Fire.MassKg * DeltaSeconds);
		AirMassKg = KMin(AirMassKg, Air.MassKg * Contact.OverlapFraction);
		if (AirMassKg <= 0.0)
		{
			return;
		}

		// Entrained air becomes combustion gas: mass, momentum and sensible heat move with it...
		const double AirTemperatureK = Air.TemperatureK;
		const double AirSpecificHeat = Air.GetSpecificHeat();
		const FVec3 AirVelocityCmS = Air.VelocityCmS;
		Air.MassKg -= AirMassKg;
		EntrainMass(Fire, AirMassKg, AirTemperatureK, AirSpecificHeat, AirVelocityCmS);

		// ...and its oxygen burns, capped at the adiabatic flame temperature.
		const double CombustionHeatJ = AirMassKg * CombustionHeatPerKgAir * Params.CombustionEfficiency;
		AddHeat(Fire, CombustionHeatJ);
		Fire.TemperatureK = KMin(Fire.TemperatureK, Context.Settings.MaxFlameTemperatureK);

		Context.EmitEvent(EReactionType::Oxygenation, AirMassKg, CombustionHeatJ);
	}

	// ---------------------------------------------------------------------------------------------------- Saturation

	void ReactSaturation(const FSaturationParams& Params, FReactionContext& Context)
	{
		FVolume& Water = Context.A;
		FVolume& Earth = Context.B;
		const FContact& Contact = Context.Contact;
		const double DeltaSeconds = Context.DeltaSeconds;

		// Momentum: an inelastic splash shoves the earth.
		const double Coupling = (1.0 - KExp(-DeltaSeconds / KMax(Params.MomentumCouplingTimeS, 1e-4))) * Contact.OverlapFraction * Params.Strength;
		const FVec3 ImpulseOnWater = MomentumCouplingImpulse(Water, Earth, KMin(Coupling, 1.0));
		Water.PendingImpulseKgCmS += ImpulseOnWater;
		Earth.PendingImpulseKgCmS -= ImpulseOnWater;

		const double PoreCapacityKg = WaterDensity * Earth.Porosity * Earth.GetGeometricVolumeM3();
		if (PoreCapacityKg <= SmallNumber || Earth.Saturation >= 1.0)
		{
			return;
		}

		const double ImpactSpeedMs = KMax(CmToM(Water.VelocityCmS - Earth.VelocityCmS).Dot(Contact.NormalAB), 0.0);
		const double AbsorptionRateKgS = WaterDensity * Contact.ExchangeAreaM2
			* (Params.InfiltrationRateMs + Params.ImpactAbsorptionFactor * ImpactSpeedMs) * Params.Strength;
		const double RoomKg = PoreCapacityKg * (1.0 - Earth.Saturation);
		const double AbsorbedKg = KMin(KMin(AbsorptionRateKgS * DeltaSeconds, RoomKg), Water.MassKg);
		if (AbsorbedKg <= 0.0)
		{
			return;
		}

		const bool bWasMud = Earth.Saturation >= Params.MudThresholdSaturation;
		const double WaterTemperatureK = Water.TemperatureK;
		const double WaterSpecificHeat = Water.GetSpecificHeat();
		const FVec3 WaterVelocityCmS = Water.VelocityCmS;

		// Absorbed water's mass, momentum and heat go into the earth: wet soil is heavier, with a higher heat capacity.
		Water.MassKg -= AbsorbedKg;
		EntrainMass(Earth, AbsorbedKg, WaterTemperatureK, WaterSpecificHeat, WaterVelocityCmS);
		Earth.Saturation = KMin(Earth.Saturation + AbsorbedKg / PoreCapacityKg, 1.0);

		Context.EmitEvent(EReactionType::Saturation, AbsorbedKg, 0.0);
		if (!bWasMud && Earth.Saturation >= Params.MudThresholdSaturation && Earth.Porosity >= Params.MinMudPorosity)
		{
			Context.EmitEvent(EReactionType::MudFormed, Earth.MassKg, 0.0);
		}
	}

	// ---------------------------------------------------------------------------------------------------- Aerodynamic drag

	void ReactAeroDrag(const FAeroDragParams& Params, FReactionContext& Context)
	{
		FVolume& Air = Context.A;
		FVolume& Body = Context.B;
		const FContact& Contact = Context.Contact;
		const double DeltaSeconds = Context.DeltaSeconds;

		const double FullFrontalAreaM2 = Body.GetFrontalAreaM2();
		const double DragAreaM2 = FullFrontalAreaM2 * Contact.ImmersionB;
		if (FullFrontalAreaM2 <= 0.0 || Contact.ImmersionB <= 0.0)
		{
			return;
		}

		const FVec3 FlowMs = CmToM(Air.VelocityCmS - Body.VelocityCmS);
		const double AirDensity = Air.GetDensityKgM3();

		// A uniform ball of compressed air passing over a body exerts no net pressure force (entry and exit cancel);
		// momentum moves through drag. Compression matters through density.
		const FVec3 ForceN = DragForceN(FlowMs, AirDensity, Body.DragCoefficient, DragAreaM2);
		FVec3 ImpulseNs = ForceN * (DeltaSeconds * Params.Strength);

		// Explicit drag must never push a light body past the wind: cap at a full inelastic merge.
		const double MaxImpulseNs = ImpulseEngineToSI(MomentumCouplingImpulse(Body, Air, 1.0)).Size();
		const double ImpulseMagnitude = ImpulseNs.Size();
		if (ImpulseMagnitude > MaxImpulseNs && ImpulseMagnitude > 0.0)
		{
			ImpulseNs *= MaxImpulseNs / ImpulseMagnitude;
		}

		const FVec3 ImpulseKgCmS = ImpulseSIToEngine(ImpulseNs);
		Body.PendingImpulseKgCmS += ImpulseKgCmS;
		Air.PendingImpulseKgCmS -= ImpulseKgCmS;

		if (!Body.bImmovable && Body.MassKg > SmallNumber && !ImpulseNs.IsNearlyZero())
		{
			// Kinetic energy handed to the body: what separates a nudge from a deflection.
			const FVec3 VelocityMs = CmToM(Body.VelocityCmS);
			const FVec3 NewVelocityMs = VelocityMs + ImpulseNs / Body.MassKg;
			const double WorkJ = 0.5 * Body.MassKg * (NewVelocityMs.SizeSquared() - VelocityMs.SizeSquared());
			Context.EmitEvent(EReactionType::Deflection, 0.0, KAbs(WorkJ));
		}

		if (Body.Substance == ESubstance::Earth && Body.Porosity >= Params.ErodiblePorosity && Body.Saturation <= Params.MaxErodibleSaturation)
		{
			const double DynamicPressurePa = DynamicPressure(AirDensity, FlowMs.Size());
			if (DynamicPressurePa > Params.CriticalErosionPressurePa)
			{
				const double ErodedKg = KMin(
					Params.ErosionCoefficient * (DynamicPressurePa - Params.CriticalErosionPressurePa) * DragAreaM2 * DeltaSeconds * Params.Strength,
					Body.MassKg);
				Body.MassKg -= ErodedKg;
				Context.EmitEvent(EReactionType::Erosion, ErodedKg, 0.0);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------- Registration

	void HeatExchangeReactionFunction(const void* UserData, FReactionContext& Context)
	{
		ReactHeatExchange(*static_cast<const FHeatExchangeParams*>(UserData), Context);
	}

	void OxygenationReactionFunction(const void* UserData, FReactionContext& Context)
	{
		ReactOxygenation(*static_cast<const FOxygenationParams*>(UserData), Context);
	}

	void SaturationReactionFunction(const void* UserData, FReactionContext& Context)
	{
		ReactSaturation(*static_cast<const FSaturationParams*>(UserData), Context);
	}

	void AeroDragReactionFunction(const void* UserData, FReactionContext& Context)
	{
		ReactAeroDrag(*static_cast<const FAeroDragParams*>(UserData), Context);
	}

	void AddBuiltInReactions(FSimWorld& World, const FDefaultReactionParams& Params)
	{
		auto Add = [&World](ESubstance A, ESubstance B, FReactionFunction Function, const void* UserData)
		{
			FReactionEntry Entry;
			Entry.SubstanceA = A;
			Entry.SubstanceB = B;
			Entry.Function = Function;
			Entry.UserData = UserData;
			World.AddReaction(Entry);
		};

		// Evaporation, melting, and flame against rock.
		Add(ESubstance::Fire, ESubstance::Water, &HeatExchangeReactionFunction, &Params.HeatExchange);
		Add(ESubstance::Fire, ESubstance::Ice, &HeatExchangeReactionFunction, &Params.FlameOnSolid);
		Add(ESubstance::Steam, ESubstance::Ice, &HeatExchangeReactionFunction, &Params.HeatExchange);
		Add(ESubstance::Fire, ESubstance::Earth, &HeatExchangeReactionFunction, &Params.FlameOnSolid);
		// Oxygenation.
		Add(ESubstance::Air, ESubstance::Fire, &OxygenationReactionFunction, &Params.Oxygenation);
		// Mud.
		Add(ESubstance::Water, ESubstance::Earth, &SaturationReactionFunction, &Params.Saturation);
		// Deflection and erosion; wind also pushes water, ice and steam clouds.
		Add(ESubstance::Air, ESubstance::Earth, &AeroDragReactionFunction, &Params.AeroDrag);
		Add(ESubstance::Air, ESubstance::Water, &AeroDragReactionFunction, &Params.AeroDrag);
		Add(ESubstance::Air, ESubstance::Ice, &AeroDragReactionFunction, &Params.AeroDrag);
		Add(ESubstance::Air, ESubstance::Steam, &AeroDragReactionFunction, &Params.AeroDrag);
	}
}
