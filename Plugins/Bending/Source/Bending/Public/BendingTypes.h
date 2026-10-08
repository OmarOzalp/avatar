#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.generated.h"

/** Bending discipline. The matter being moved is EElementalSubstance. */
UENUM(BlueprintType)
enum class EBendingElement : uint8
{
	None,
	Earth,
	Water,
	Fire,
	Air
};

/** Martial-arts frame phases of a move. */
UENUM(BlueprintType)
enum class EBendingPhase : uint8
{
	None,
	Startup,
	Active,
	Recovery
};

/** Which chi conversion rate applies when a bender pays for physical work. */
UENUM(BlueprintType)
enum class EBendingEnergyKind : uint8
{
	/** Accelerating mass: 1/2 m v^2, lifting: m g h. */
	Kinetic,
	/** Adding or extracting heat: freezing water, superheating flame. */
	Thermal
};

/**
 * Fighting-game frame data for a move, authored at a fixed 60 Hz regardless of render or physics rate.
 * With bFitAnimationToFrameData, the montage is re-timed per phase so each phase lasts exactly this long.
 */
USTRUCT(BlueprintType)
struct BENDING_API FBendingFrameData
{
	GENERATED_BODY()

	static constexpr double FramesPerSecond = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Data", meta = (ClampMin = 1, UIMin = 1))
	int32 StartupFrames = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Data", meta = (ClampMin = 1, UIMin = 1))
	int32 ActiveFrames = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Data", meta = (ClampMin = 0, UIMin = 0))
	int32 RecoveryFrames = 20;

	/** First recovery frame from which a buffered move may cancel this one. >= RecoveryFrames means never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Data", meta = (ClampMin = 0, UIMin = 0))
	int32 RecoveryCancelFrame = 14;

	/** A confirmed hit makes the move cancellable from that moment on (hit-confirm combos). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Data")
	bool bCancelOnHit = true;

	/** Re-time each montage segment marked by a Bending Phase notify so the phase lasts exactly its frame count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Data")
	bool bFitAnimationToFrameData = true;

	[[nodiscard]] int32 GetPhaseFrames(EBendingPhase Phase) const
	{
		switch (Phase)
		{
		case EBendingPhase::Startup:  return StartupFrames;
		case EBendingPhase::Active:   return ActiveFrames;
		case EBendingPhase::Recovery: return RecoveryFrames;
		default:                      return 0;
		}
	}

	[[nodiscard]] double GetPhaseSeconds(EBendingPhase Phase) const
	{
		return GetPhaseFrames(Phase) / FramesPerSecond;
	}

	[[nodiscard]] int32 GetTotalFrames() const
	{
		return StartupFrames + ActiveFrames + RecoveryFrames;
	}

	[[nodiscard]] static int32 SecondsToFrames(double Seconds)
	{
		return FMath::FloorToInt32(Seconds * FramesPerSecond + UE_KINDA_SMALL_NUMBER);
	}
};
