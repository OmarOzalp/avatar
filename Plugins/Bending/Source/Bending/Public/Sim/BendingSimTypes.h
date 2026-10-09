#pragma once

#include "Sim/BendingSimMath.h"
#include "Sim/BendingThermo.h"

/**
 * Core data of the engine-independent bending simulation: substances, elemental volumes, contacts, events
 * and settings. The Unreal plugin mirrors these as reflected types (EElementalSubstance,
 * FElementalVolumeState...) for Blueprint and the editor, and converts at the boundary.
 */
namespace BendingSim
{
	// ---------------------------------------------------------------- Substances

	/** Matter a bender moves. Values are mirrored by the reflected EElementalSubstance. */
	enum class ESubstance : unsigned char
	{
		None,
		Earth,
		Water,
		Ice,
		Steam,
		Fire,
		Air,

		Count
	};

	constexpr unsigned int SubstanceMask(ESubstance Substance) { return 1u << static_cast<unsigned int>(Substance); }
	inline constexpr unsigned int AllSubstancesMask = (1u << static_cast<unsigned int>(ESubstance::Count)) - 1u;

	/** Order-independent key for a substance pair. */
	constexpr unsigned short MakePairKey(ESubstance A, ESubstance B)
	{
		const unsigned short X = static_cast<unsigned short>(A);
		const unsigned short Y = static_cast<unsigned short>(B);
		return X < Y ? static_cast<unsigned short>((X << 8) | Y) : static_cast<unsigned short>((Y << 8) | X);
	}

	BENDINGSIM_API const char* GetSubstanceName(ESubstance Substance);

	/** Real-world reference properties. */
	struct FSubstanceProperties
	{
		/** Reference density (kg/m^3); gases with a derived radius use the ideal-gas law instead. */
		double DensityKgM3 = 1000.0;
		/** J/(kg*K). */
		double SpecificHeat = 1000.0;
		double DefaultTemperatureK = 288.15;
		/** Effective convective + radiative coefficient to the surrounding air (W/(m^2*K)). */
		double AmbientHeatTransferCoefficient = 10.0;
		double DragCoefficient = 0.47;
		/** J/(kg*K); > 0 only for gases. */
		double SpecificGasConstant = 0.0;
		/** Earth only: pore fraction, overridden per volume from the physical material. */
		double DefaultPorosity = 0.0;
		bool bIsGas = false;

		double GetDensityAt(double TemperatureK) const
		{
			return bIsGas ? IdealGasDensity(TemperatureK, SpecificGasConstant) : DensityKgM3;
		}
	};

	BENDINGSIM_API const FSubstanceProperties& GetSubstanceProperties(ESubstance Substance);

	// ---------------------------------------------------------------- Volumes

	enum class EShape : unsigned char
	{
		Sphere,
		/** Swept sphere along a segment: water-whip segments, fire streams, stone pillars. */
		Capsule
	};

	/**
	 * The simulated body of bent matter: what gameplay and reactions reason about. Matter is SI; shape and
	 * kinematics are engine units (cm, cm/s) because they feed engine APIs.
	 */
	struct BENDINGSIM_API FVolume
	{
		ESubstance Substance = ESubstance::None;
		double MassKg = 1.0;
		double TemperatureK = 288.15;
		/** Energy (J) banked toward the pending phase change: ice >= 0 toward melting, water <= 0 toward freezing. */
		double LatentHeatJ = 0.0;
		/** Specific heat for inert matter after mixing (air entrained into flame, water into soil). 0 = default. */
		double SpecificHeatOverride = 0.0;
		/** Earth: pore fraction. ~0.01 granite, 0.3-0.45 soil and sand. */
		double Porosity = 0.0;
		/** Earth: fraction of pore volume filled with water. */
		double Saturation = 0.0;
		double DragCoefficient = 0.47;

		EShape Shape = EShape::Sphere;
		double RadiusCm = 50.0;
		/** Capsule: half of the core segment, world space. */
		FVec3 CapsuleHalfAxisCm = FVec3(0.0, 0.0, 50.0);
		/** Spheres: radius follows mass and density each step, so gases expand with temperature (Charles's law). */
		bool bDeriveRadiusFromMass = false;
		/** Infinite mass in momentum exchange (anchored pillars, walls frozen to the ground). */
		bool bImmovable = false;

		FVec3 LocationCm;
		FVec3 VelocityCmS;
		/** Impulse (kg*cm/s) reactions accumulated this step; the world integrates it and hands it to the owner. */
		FVec3 PendingImpulseKgCmS;

		/** Real-world defaults for a substance: temperature, drag, porosity, and a radius matching the mass. */
		static FVolume MakeDefault(ESubstance InSubstance, double InMassKg);

		const FSubstanceProperties& GetProperties() const { return GetSubstanceProperties(Substance); }
		bool IsGas() const { return GetProperties().bIsGas; }
		double GetSpecificHeat() const;
		double GetHeatCapacityJPerK() const { return MassKg * GetSpecificHeat(); }
		double GetGeometricVolumeM3() const;
		/** Ideal gas for free gases, mass / shape volume for owner-shaped (compressed) gas, reference otherwise. */
		double GetDensityKgM3() const;
		/** 0 for immovable or massless volumes. */
		double GetInverseMass() const { return (bImmovable || MassKg <= SmallNumber) ? 0.0 : 1.0 / MassKg; }
		double GetSurfaceAreaM2() const;
		/** Projected area facing a flow; capsules assume side-on. */
		double GetFrontalAreaM2() const;
		double GetKineticEnergyJ() const { return 0.5 * MassKg * CmToM(VelocityCmS).SizeSquared(); }
		FVec3 GetMomentumKgCmS() const { return VelocityCmS * MassKg; }
		FVec3 GetSegmentStartCm() const { return Shape == EShape::Capsule ? LocationCm - CapsuleHalfAxisCm : LocationCm; }
		FVec3 GetSegmentEndCm() const { return Shape == EShape::Capsule ? LocationCm + CapsuleHalfAxisCm : LocationCm; }
		double GetBoundingRadiusCm() const { return RadiusCm + (Shape == EShape::Capsule ? CapsuleHalfAxisCm.Size() : 0.0); }
		/** Recomputes RadiusCm from mass and density when bDeriveRadiusFromMass is set on a sphere. */
		void UpdateDerivedRadius();
	};

	// ---------------------------------------------------------------- Physics on volumes

	BENDINGSIM_API Thermo::FThermoMatter ToMatter(const FVolume& Volume);
	BENDINGSIM_API void ApplyMatter(FVolume& Volume, const Thermo::FThermoMatter& Matter);

	/** Adds heat (J; negative extracts), walking ice <-> water <-> steam; updates the substance on full transitions. */
	BENDINGSIM_API Thermo::FPhaseChangeResult AddHeat(FVolume& Volume, double HeatJ);
	/** Boils the contact layer of liquid water without warming the bulk. Returns kg vaporized. */
	BENDINGSIM_API double FlashVaporize(FVolume& Volume, double HeatJ);
	/** Largest heat flow Hot -> Cold this step that cannot invert the temperature gradient. */
	BENDINGSIM_API double MaxHeatFlow(const FVolume& Hot, const FVolume& Cold);
	/** Heat (J) to extract to freeze this water completely; 0 if not liquid water. */
	BENDINGSIM_API double HeatToFreeze(const FVolume& Volume);
	/** Heat (J) to add to melt this ice completely; 0 if not ice. */
	BENDINGSIM_API double HeatToMelt(const FVolume& Volume);
	/** Entrains inert mass: sensible heat conserved exactly, incoming momentum added as a pending impulse. */
	BENDINGSIM_API void EntrainMass(FVolume& Volume, double MassKg, double TemperatureK, double SpecificHeat, const FVec3& VelocityCmS);
	/** Newton cooling toward ambient over Dt, integrated exactly; phase plateaus use the conductive flux. */
	BENDINGSIM_API Thermo::FPhaseChangeResult ExchangeWithAmbient(FVolume& Volume, double AmbientTemperatureK, double DeltaSeconds);
	/** Quadratic drag (N) on a body seeing flow RelativeFlowMs: F = 1/2 * rho * Cd * A * |v| * v. */
	BENDINGSIM_API FVec3 DragForceN(const FVec3& RelativeFlowMs, double FluidDensityKgM3, double DragCoefficient, double FrontalAreaM2);
	/** Impulse on A (kg*cm/s) closing CouplingFraction of the velocity gap to B; apply the negative to B. */
	BENDINGSIM_API FVec3 MomentumCouplingImpulse(const FVolume& A, const FVolume& B, double CouplingFraction);
	/** Common velocity after a perfectly inelastic merge (cm/s). */
	BENDINGSIM_API FVec3 MixtureVelocityCmS(const FVolume& A, const FVolume& B);

	// ---------------------------------------------------------------- Contacts

	/** Narrow-phase result for one overlapping pair, oriented from A to B. */
	struct FContact
	{
		/** Midpoint of the overlap (cm). */
		FVec3 PointCm;
		/** Unit normal from A toward B. */
		FVec3 NormalAB = FVec3(0.0, 0.0, 1.0);
		double PenetrationCm = 0.0;
		/** Surface of the smaller volume inside the larger (m^2): what exchanges heat and mass. */
		double ExchangeAreaM2 = 0.0;
		/** Fraction of A's / B's surface inside the other (0..1). */
		double ImmersionA = 0.0;
		double ImmersionB = 0.0;
		/** Penetration relative to the smaller diameter (0..1). */
		double OverlapFraction = 0.0;

		FContact Flipped() const
		{
			FContact Out = *this;
			Out.NormalAB = -NormalAB;
			Out.ImmersionA = ImmersionB;
			Out.ImmersionB = ImmersionA;
			return Out;
		}
	};

	/** Sphere/capsule overlap via closest points between core segments. False when separated. */
	BENDINGSIM_API bool ComputeContact(const FVolume& A, const FVolume& B, FContact& OutContact);

	// ---------------------------------------------------------------- Handles, events, settings

	/** Stable reference to a volume. Serial guards against slot reuse. */
	struct FHandle
	{
		int Index = -1;
		unsigned int Serial = 0;

		constexpr bool IsSet() const { return Index >= 0; }
		constexpr bool operator==(const FHandle& Other) const { return Index == Other.Index && Serial == Other.Serial; }
		constexpr bool operator!=(const FHandle& Other) const { return !(*this == Other); }
	};

	/** Values are mirrored by the reflected EElementalReactionType. */
	enum class EReactionType : unsigned char
	{
		None,
		Evaporation,
		Condensation,
		Melting,
		Freezing,
		Oxygenation,
		Saturation,
		MudFormed,
		Deflection,
		Erosion,
		Extinguished,

		Count
	};

	BENDINGSIM_API const char* GetReactionTypeName(EReactionType Type);

	struct FReactionEvent
	{
		EReactionType Type = EReactionType::None;
		ESubstance SubstanceA = ESubstance::None;
		ESubstance SubstanceB = ESubstance::None;
		FHandle VolumeA;
		FHandle VolumeB;
		/** Mass-weighted location over the aggregation window (cm). */
		FVec3 LocationCm;
		FVec3 Normal = FVec3(0.0, 0.0, 1.0);
		/** Mass converted or moved over the window (kg). */
		double MassKg = 0.0;
		/** Energy exchanged over the window (J). */
		double EnergyJ = 0.0;
		/** MassKg / window: bind to Niagara spawn rates. */
		double MassRateKgS = 0.0;
		/** EnergyJ / window. */
		double PowerW = 0.0;
		double WindowSeconds = 0.0;
		/** One-off transitions (melting, extinguished) rather than an aggregated continuous reaction. */
		bool bDiscrete = false;
	};

	/** Environment and simulation knobs. Physical constants live in code; these are environment and gameplay scales. */
	struct FSimSettings
	{
		double TickRateHz = 60.0;
		int MaxSubstepsPerFrame = 4;
		double ReactionEventIntervalS = 0.1;
		double MinVolumeMassKg = 0.001;
		int MaxFreeVolumes = 128;
		double FreeVolumeMergeRadiusCm = 120.0;
		double FreeGasDampingTimeS = 0.35;

		double AmbientTemperatureK = 288.15;
		double AmbientAirDensityKgM3 = AirDensitySeaLevel;
		/** Gravity along Z (cm/s^2). */
		double GravityZCmS2 = -980.665;

		double HeatTransferScale = 1.0;
		double FireExtinguishTemperatureK = 700.0;
		double MaxFlameTemperatureK = 2300.0;

		int MaxMoisturePatches = 64;
		double MoistureSoilDepthM = 0.05;
		double MinMoisturePatchRadiusCm = 60.0;
		double MaxMoisturePatchRadiusCm = 800.0;
		/** Physical potential evaporation from wet soil (kg/(m^2*s)). */
		double DryingRateKgM2S = 5e-5;
		/** Gameplay acceleration of natural drying. 1 = real time. */
		double DryingTimeScale = 30.0;
		double MudOnsetSaturation = 0.35;
		double MudFrictionRatio = 0.35;
		double FireGroundHeatTransferCoefficient = 60.0;
	};
}
