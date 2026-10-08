#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BendingSettings.generated.h"

class UElementalReactionSet;

/**
 * Project Settings > Game > Bending.
 *
 * Physical constants live in code (they are facts). What lives here are environment values and the
 * explicit gameplay multipliers layered on top of real physics, each named as such.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Bending"))
class BENDING_API UBendingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	[[nodiscard]] static const UBendingSettings& Get() { return *GetDefault<UBendingSettings>(); }

	virtual FName GetCategoryName() const override;

	// ---------------------------------------------------------------- Simulation

	/** Fixed rate of the cross-element interaction simulation. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 10, ClampMax = 240))
	float InteractionTickRateHz = 60.f;

	/** Cap on fixed steps per frame; the backlog is dropped beyond it (avoids the spiral of death on hitches). */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1, ClampMax = 16))
	int32 MaxSubstepsPerFrame = 4;

	/** Reaction rules. Empty uses the built-in set (evaporation, melting, oxygenation, saturation, aerodynamic drag). */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation")
	TSoftObjectPtr<UElementalReactionSet> ReactionSet;

	/** Continuous reaction events (steam production, erosion...) are aggregated and broadcast at this interval. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0.0, Units = "s"))
	float ReactionEventIntervalS = 0.1f;

	/** A volume whose mass drops below this is depleted. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0.0, Units = "kg"))
	float MinVolumeMassKg = 0.001f;

	/** Ownerless volumes spawned by reactions (steam clouds). Further spawns merge into the nearest one. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 1))
	int32 MaxFreeVolumes = 128;

	/** Spawned free volumes closer than this to an existing one of the same substance merge into it. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0.0, Units = "cm"))
	float FreeVolumeMergeRadiusCm = 120.f;

	/** Velocity relaxation time of free gas parcels against the still surrounding air. */
	UPROPERTY(Config, EditAnywhere, Category = "Simulation", meta = (ClampMin = 0.01, Units = "s"))
	float FreeGasDampingTimeS = 0.35f;

	// ---------------------------------------------------------------- Environment

	/** Ambient air temperature (K). 288.15 K = 15 C. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment", meta = (ClampMin = 1.0))
	float AmbientTemperatureK = 288.15f;

	/** Ambient air density (kg/m^3). Sea level: 1.225. */
	UPROPERTY(Config, EditAnywhere, Category = "Environment", meta = (ClampMin = 0.01))
	float AmbientAirDensityKgM3 = 1.225f;

	// ---------------------------------------------------------------- Thermal

	/** Gameplay multiplier on every reaction heat flow. 1 = physical. */
	UPROPERTY(Config, EditAnywhere, Category = "Thermal", meta = (ClampMin = 0.0))
	float HeatTransferScale = 1.f;

	/** Flame below this temperature (K) can no longer sustain combustion and is extinguished. */
	UPROPERTY(Config, EditAnywhere, Category = "Thermal", meta = (ClampMin = 300.0))
	float FireExtinguishTemperatureK = 700.f;

	/** Adiabatic flame temperature ceiling (K) for hydrocarbon-air combustion. */
	UPROPERTY(Config, EditAnywhere, Category = "Thermal", meta = (ClampMin = 800.0))
	float MaxFlameTemperatureK = 2300.f;

	// ---------------------------------------------------------------- Surfaces (mud)

	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 1))
	int32 MaxMoisturePatches = 64;

	/** Soil depth (m) that holds deposited water. Capacity = rho_water * porosity * depth * area. */
	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 0.01))
	float MoistureSoilDepthM = 0.15f;

	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 1.0, Units = "cm"))
	float MinMoisturePatchRadiusCm = 60.f;

	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 1.0, Units = "cm"))
	float MaxMoisturePatchRadiusCm = 800.f;

	/** Physical potential evaporation from wet soil (kg/(m^2*s)). ~5e-5 is a warm sunny day. */
	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 0.0))
	float DryingRateKgM2S = 5e-5f;

	/** Gameplay acceleration of natural drying. 1 = real time (mud would last hours). */
	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 0.0))
	float DryingTimeScale = 30.f;

	/** Saturation at which soil starts losing traction. */
	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MudOnsetSaturation = 0.35f;

	/** Friction of saturated mud relative to dry soil (mu_mud / mu_dry). */
	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MudFrictionRatio = 0.35f;

	/** Heat-transfer coefficient (W/(m^2*K)) from a flame resting on wet ground. */
	UPROPERTY(Config, EditAnywhere, Category = "Surfaces", meta = (ClampMin = 0.0))
	float FireGroundHeatTransferCoefficient = 60.f;

	// ---------------------------------------------------------------- Resources

	/** Mechanical work (J) one point of chi buys: accelerating and lifting mass. */
	UPROPERTY(Config, EditAnywhere, Category = "Resources", meta = (ClampMin = 1.0))
	float KineticJoulesPerChi = 5000.f;

	/** Heat (J) one point of chi buys. Phase changes cost megajoules, so this rate is much higher. */
	UPROPERTY(Config, EditAnywhere, Category = "Resources", meta = (ClampMin = 1.0))
	float ThermalJoulesPerChi = 100000.f;
};
