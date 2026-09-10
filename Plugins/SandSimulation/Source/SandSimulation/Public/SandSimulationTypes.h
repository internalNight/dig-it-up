#pragma once

#include "CoreMinimal.h"
#include "SandSimulationTypes.generated.h"

/**
 * Physical controls for one dry granular material.
 *
 * The solver uses SI units. These values are intentionally independent of the
 * visual material so changing albedo or normal detail cannot change mechanics.
 */
USTRUCT(BlueprintType)
struct SANDSIMULATION_API FSandMaterialParameters
{
    GENERATED_BODY()

    /** Loose bulk density. Sets particle mass and therefore tool/chassis loads. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "800.0", ClampMax = "2200.0"))
    float BulkDensityKgPerM3 = 1600.0f;

    /** Mohr-Coulomb friction angle. Higher values resist shear and form steeper piles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "5.0", ClampMax = "55.0", Units = "deg"))
    float InternalFrictionAngleDegrees = 30.0f;

    /** True dry sand is close to zero. Raising this produces damp/clumpy soil behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "0.0", ClampMax = "20000.0"))
    float CohesionPa = 0.0f;

    /** Plastic volume expansion during shear. Dense sand usually has a positive value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "deg"))
    float DilationAngleDegrees = 5.0f;

    /** Effective continuum stiffness. Higher values reduce elastic give but require smaller substeps. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "10000.0", ClampMax = "5000000.0"))
    float YoungsModulusPa = 250000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "0.05", ClampMax = "0.45"))
    float PoissonRatio = 0.2f;

    /** 0 is loose-packed and 1 is dense-packed; dense material initially resists cutting more strongly. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InitialRelativeCompaction = 0.45f;

    /** Strength gained under irreversible compression. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material", meta = (ClampMin = "0.0", ClampMax = "20.0"))
    float HardeningRate = 4.0f;

    /** Coulomb friction between sand and bucket, arm, chassis and container boundaries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Contact", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float ToolFrictionCoefficient = 0.5f;

    /** Collision energy return. Dry sand should remain nearly inelastic. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Contact", meta = (ClampMin = "0.0", ClampMax = "0.3"))
    float Restitution = 0.02f;

    /** Small numerical energy loss applied to unresolved grain-scale motion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Numerics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float VelocityDampingPerSecond = 0.15f;

    bool IsValid(FString* OutReason = nullptr) const;
    float GetLameMuPa() const;
    float GetLameLambdaPa() const;
    float GetDruckerPragerFrictionAlpha() const;
    float GetDruckerPragerCohesionInterceptPa() const;
    float GetDilationAlpha() const;
};

/** Spatial and temporal discretization. All dimensions are in solver-space metres. */
USTRUCT(BlueprintType)
struct SANDSIMULATION_API FSandSolverParameters
{
    GENERATED_BODY()

    /** Container interior. Z=0 is the rigid floor; XY is centred on the owning component. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Domain", meta = (ClampMin = "0.1", Units = "m"))
    FVector DomainSizeMeters = FVector(5.0, 5.0, 3.0);

    /** Initially filled depth. This material remains fully movable all the way to Z=0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Domain", meta = (ClampMin = "0.0", Units = "m"))
    float InitialSandDepthMeters = 2.0f;

    /** Baseline is 3.125 cm. A later 2.5 cm mode requires a separate performance gate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Resolution", meta = (ClampMin = "0.01", ClampMax = "0.1", Units = "m"))
    float CellSizeMeters = 0.03125f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Resolution", meta = (ClampMin = "1", ClampMax = "8"))
    int32 ParticlesPerCell = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Resolution", meta = (ClampMin = "4", ClampMax = "16"))
    int32 CellsPerBlockAxis = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Time", meta = (ClampMin = "15.0", ClampMax = "120.0", Units = "Hz"))
    float FixedSimulationHz = 30.0f;

    /** Safety cap for CFL/stiffness-driven internal subdivisions of one fixed tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Time", meta = (ClampMin = "1", ClampMax = "32"))
    int32 MaxInternalSubsteps = 12;

    bool IsValid(FString* OutReason = nullptr) const;
    FIntVector GetGridCellResolution() const;
    FIntVector GetGridNodeResolution() const;
    FIntVector GetInitialOccupiedCellResolution() const;
    int64 GetInitialParticleCount() const;
    FVector3f GetDomainMinimumMeters() const;
    float GetFixedDeltaSeconds() const;
};
