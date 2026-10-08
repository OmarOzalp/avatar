#pragma once

#include "CoreMinimal.h"
#include "Physics/ElementalSubstance.h"
#include "Sim/BendingSimTypes.h"
#include "ElementalVolumeTypes.generated.h"

UENUM(BlueprintType)
enum class EElementalVolumeShape : uint8
{
	Sphere,
	/** Swept sphere along a segment: water whip segments, fire streams, stone pillars. */
	Capsule
};

UENUM(BlueprintType)
enum class EElementalReactionType : uint8
{
	None,
	/** Liquid water boiled off (Fire + Water). Mass = steam produced. */
	Evaporation,
	/** Steam turned back to droplets. Mass = water condensed. */
	Condensation,
	/** Ice fully melted into water. */
	Melting,
	/** Water fully frozen into ice. */
	Freezing,
	/** Air fed a flame (Air + Fire). Mass = air consumed, Energy = combustion heat released. */
	Oxygenation,
	/** Soil absorbed water (Water + Earth). Mass = water absorbed. */
	Saturation,
	/** Soil crossed the mud threshold and lost cohesion/traction. */
	MudFormed,
	/** Aerodynamic or pressure force redirected a body (Air + X). Energy = kinetic energy imparted. */
	Deflection,
	/** Wind stripped loose material off earth. Mass = material removed. */
	Erosion,
	/** A flame dropped below its sustain temperature. */
	Extinguished
};

static_assert(static_cast<int>(EElementalReactionType::Extinguished) == static_cast<int>(BendingSim::EReactionType::Extinguished),
	"Reaction type mirror out of sync with BendingSim::EReactionType");
static_assert(static_cast<int>(EElementalVolumeShape::Capsule) == static_cast<int>(BendingSim::EShape::Capsule),
	"Shape mirror out of sync with BendingSim::EShape");

/** Stable reference to a volume in UBendingInteractionSubsystem. Serial guards against slot reuse. */
USTRUCT(BlueprintType)
struct BENDING_API FElementalVolumeHandle
{
	GENERATED_BODY()

	FElementalVolumeHandle() = default;
	FElementalVolumeHandle(int32 InIndex, uint32 InSerial) : Index(InIndex), Serial(InSerial) {}

	[[nodiscard]] bool IsSet() const { return Index != INDEX_NONE; }
	[[nodiscard]] int32 GetIndex() const { return Index; }
	[[nodiscard]] uint32 GetSerial() const { return Serial; }
	void Reset() { Index = INDEX_NONE; Serial = 0; }

	bool operator==(const FElementalVolumeHandle& Other) const { return Index == Other.Index && Serial == Other.Serial; }
	bool operator!=(const FElementalVolumeHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FElementalVolumeHandle& Handle)
	{
		return HashCombine(::GetTypeHash(Handle.Index), ::GetTypeHash(Handle.Serial));
	}

	[[nodiscard]] BendingSim::FHandle ToSim() const
	{
		BendingSim::FHandle Sim;
		Sim.Index = Index;
		Sim.Serial = Serial;
		return Sim;
	}

	[[nodiscard]] static FElementalVolumeHandle FromSim(const BendingSim::FHandle& Sim)
	{
		return Sim.IsSet() ? FElementalVolumeHandle(Sim.Index, Sim.Serial) : FElementalVolumeHandle();
	}

private:
	// Reflected so Blueprint equality and reflected containers compare handles by value.
	UPROPERTY()
	int32 Index = INDEX_NONE;

	UPROPERTY()
	uint32 Serial = 0;
};

/**
 * Reflected mirror of BendingSim::FVolume, the simulated body of bent matter, for Blueprint and the editor
 * (UElementalVolumeComponent::InitialState). The simulation itself runs on BendingSim::FVolume.
 * Niagara only renders this; it never decides outcomes.
 *
 * Matter quantities are SI. Shape and kinematics are engine units (cm, cm/s) because they feed engine APIs.
 */
USTRUCT(BlueprintType)
struct BENDING_API FElementalVolumeState
{
	GENERATED_BODY()

	// ---------------------------------------------------------------- Matter (SI)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matter")
	EElementalSubstance Substance = EElementalSubstance::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matter", meta = (ClampMin = 0.0, Units = "kg"))
	double MassKg = 1.0;

	/** Kelvin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matter", meta = (ClampMin = 1.0))
	double TemperatureK = 288.15;

	/** Energy (J) banked toward the pending phase change: ice >= 0 toward melting, water <= 0 toward freezing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Matter")
	double LatentHeatJ = 0.0;

	/** Specific heat (J/(kg*K)) for inert matter after mixing (air entrained into flame). 0 = substance default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Matter")
	double SpecificHeatOverride = 0.0;

	/** Pore fraction for Earth: ~0.01 granite, 0.3-0.45 soil and sand. Pores fill with water and become mud. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matter", meta = (ClampMin = 0.0, ClampMax = 0.9))
	float Porosity = 0.f;

	/** Fraction of pore volume filled with water (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matter", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float Saturation = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Matter", meta = (ClampMin = 0.0))
	float DragCoefficient = 0.47f;

	// ---------------------------------------------------------------- Shape & kinematics (engine units)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	EElementalVolumeShape Shape = EElementalVolumeShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = 0.0, Units = "cm"))
	float RadiusCm = 50.f;

	/** Capsule only: half of the core segment. Component space on UElementalVolumeComponent, world space in the simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EElementalVolumeShape::Capsule", Units = "cm"))
	FVector CapsuleHalfAxisCm = FVector(0.0, 0.0, 50.0);

	/** Spheres only: radius follows mass and density every step, so gases expand with temperature (Charles's law). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	bool bDeriveRadiusFromMass = false;

	/** Infinite mass in momentum exchange (terrain-anchored pillars, ice walls frozen to the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	bool bImmovable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinematics", meta = (Units = "cm"))
	FVector LocationCm = FVector::ZeroVector;

	/** cm/s. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinematics")
	FVector VelocityCmS = FVector::ZeroVector;

	/** Real-world defaults for a substance: temperature, drag, porosity, and a radius matching the mass. */
	[[nodiscard]] static FElementalVolumeState MakeDefault(EElementalSubstance InSubstance, double InMassKg);

	[[nodiscard]] BendingSim::FVolume ToSim() const;
	[[nodiscard]] static FElementalVolumeState FromSim(const BendingSim::FVolume& Volume);
};

/** What a heat transfer did to a volume's phase. */
USTRUCT(BlueprintType)
struct BENDING_API FElementalPhaseChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Phase Change")
	double MeltedKg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Phase Change")
	double FrozenKg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Phase Change")
	double VaporizedKg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Phase Change")
	double CondensedKg = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Phase Change")
	bool bSubstanceChanged = false;

	[[nodiscard]] static FElementalPhaseChange FromSim(const BendingSim::Thermo::FPhaseChangeResult& Result)
	{
		FElementalPhaseChange Change;
		Change.MeltedKg = Result.MeltedKg;
		Change.FrozenKg = Result.FrozenKg;
		Change.VaporizedKg = Result.VaporizedKg;
		Change.CondensedKg = Result.CondensedKg;
		Change.bSubstanceChanged = Result.bPhaseChanged;
		return Change;
	}
};

/** Broadcast by UBendingInteractionSubsystem; continuous reactions arrive aggregated with rates. */
USTRUCT(BlueprintType)
struct BENDING_API FElementalReactionEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	EElementalReactionType Type = EElementalReactionType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	EElementalSubstance SubstanceA = EElementalSubstance::None;

	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	EElementalSubstance SubstanceB = EElementalSubstance::None;

	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	FElementalVolumeHandle VolumeA;

	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	FElementalVolumeHandle VolumeB;

	/** Mass-weighted location of the reaction over the aggregation window (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	FVector LocationCm = FVector::ZeroVector;

	/** Contact normal A -> B. */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	FVector Normal = FVector::UpVector;

	/** Mass converted or moved over the window (kg). */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	double MassKg = 0.0;

	/** Energy exchanged over the window (J). */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	double EnergyJ = 0.0;

	/** MassKg / window: bind straight to Niagara spawn rates. */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	double MassRateKgS = 0.0;

	/** EnergyJ / window (W). */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	double PowerW = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	float WindowSeconds = 0.f;

	/** One-off transition (melting, freezing, extinguished) rather than an aggregated continuous reaction. */
	UPROPERTY(BlueprintReadOnly, Category = "Reaction")
	bool bDiscrete = false;

	[[nodiscard]] static FElementalReactionEvent FromSim(const BendingSim::FReactionEvent& Event);
};
