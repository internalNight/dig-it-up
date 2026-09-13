#include "SandSimulationTypes.h"

namespace
{
bool FailValidation(FString* OutReason, const TCHAR* Reason)
{
    if (OutReason != nullptr)
    {
        *OutReason = Reason;
    }
    return false;
}

float MohrCoulombAngleToDruckerPragerAlpha(const float AngleDegrees)
{
    const float SinAngle = FMath::Sin(FMath::DegreesToRadians(AngleDegrees));
    return (2.0f * SinAngle) / (FMath::Sqrt(3.0f) * (3.0f - SinAngle));
}
}

bool FSandMaterialParameters::IsValid(FString* OutReason) const
{
    for(float V : {BulkDensityKgPerM3,InternalFrictionAngleDegrees,CohesionPa,DilationAngleDegrees,
        YoungsModulusPa,PoissonRatio,InitialRelativeCompaction,HardeningRate,ToolFrictionCoefficient,Restitution,VelocityDampingPerSecond})
        if(!FMath::IsFinite(V)) return FailValidation(OutReason,TEXT("Material values must be finite."));
    if (BulkDensityKgPerM3 <= 0.0f)
    {
        return FailValidation(OutReason, TEXT("Bulk density must be positive."));
    }
    if (InternalFrictionAngleDegrees <= 0.0f || InternalFrictionAngleDegrees >= 60.0f)
    {
        return FailValidation(OutReason, TEXT("Internal friction angle must lie between 0 and 60 degrees."));
    }
    if (CohesionPa < 0.0f)
    {
        return FailValidation(OutReason, TEXT("Cohesion cannot be negative."));
    }
    if (DilationAngleDegrees < 0.0f || DilationAngleDegrees > InternalFrictionAngleDegrees)
    {
        return FailValidation(OutReason, TEXT("Dilation angle must lie between zero and the friction angle."));
    }
    if (YoungsModulusPa <= 0.0f || PoissonRatio <= 0.0f || PoissonRatio >= 0.5f)
    {
        return FailValidation(OutReason, TEXT("Elastic parameters must produce a finite, positive stiffness."));
    }
    if (InitialRelativeCompaction < 0.0f || InitialRelativeCompaction > 1.0f || HardeningRate < 0.0f)
    {
        return FailValidation(OutReason, TEXT("Compaction must be in [0,1] and hardening cannot be negative."));
    }
    if (ToolFrictionCoefficient < 0.0f || Restitution < 0.0f || VelocityDampingPerSecond < 0.0f)
    {
        return FailValidation(OutReason, TEXT("Contact and damping coefficients cannot be negative."));
    }
    return true;
}

float FSandMaterialParameters::GetLameMuPa() const
{
    return YoungsModulusPa / (2.0f * (1.0f + PoissonRatio));
}

float FSandMaterialParameters::GetLameLambdaPa() const
{
    return (YoungsModulusPa * PoissonRatio) /
        ((1.0f + PoissonRatio) * (1.0f - 2.0f * PoissonRatio));
}

float FSandMaterialParameters::GetDruckerPragerFrictionAlpha() const
{
    return MohrCoulombAngleToDruckerPragerAlpha(InternalFrictionAngleDegrees);
}

float FSandMaterialParameters::GetDruckerPragerCohesionInterceptPa() const
{
    const float AngleRadians = FMath::DegreesToRadians(InternalFrictionAngleDegrees);
    return (6.0f * CohesionPa * FMath::Cos(AngleRadians)) /
        (FMath::Sqrt(3.0f) * (3.0f - FMath::Sin(AngleRadians)));
}

float FSandMaterialParameters::GetDilationAlpha() const
{
    return MohrCoulombAngleToDruckerPragerAlpha(DilationAngleDegrees);
}

bool FSandSolverParameters::IsValid(FString* OutReason) const
{
    if (DomainSizeMeters.X <= 0.0 || DomainSizeMeters.Y <= 0.0 || DomainSizeMeters.Z <= 0.0)
    {
        return FailValidation(OutReason, TEXT("Every domain axis must be positive."));
    }
    if (InitialSandDepthMeters <= 0.0f || InitialSandDepthMeters > DomainSizeMeters.Z)
    {
        return FailValidation(OutReason, TEXT("Initial sand depth must be positive and no greater than the domain height."));
    }
    if (CellSizeMeters <= 0.0f || ParticlesPerCell <= 0 || CellsPerBlockAxis <= 0)
    {
        return FailValidation(OutReason, TEXT("Spatial discretization values must be positive."));
    }
    if (FixedSimulationHz <= 0.0f || MaxInternalSubsteps <= 0)
    {
        return FailValidation(OutReason, TEXT("Time discretization values must be positive."));
    }
    return true;
}

FIntVector FSandSolverParameters::GetGridCellResolution() const
{
    return FIntVector(
        FMath::CeilToInt(DomainSizeMeters.X / CellSizeMeters),
        FMath::CeilToInt(DomainSizeMeters.Y / CellSizeMeters),
        FMath::CeilToInt(DomainSizeMeters.Z / CellSizeMeters));
}

FIntVector FSandSolverParameters::GetGridNodeResolution() const
{
    // One ghost node on each side is required by the 3x3x3 quadratic B-spline stencil.
    return GetGridCellResolution() + FIntVector(3, 3, 3);
}

FIntVector FSandSolverParameters::GetInitialOccupiedCellResolution() const
{
    const FIntVector GridCells = GetGridCellResolution();
    return FIntVector(GridCells.X, GridCells.Y, FMath::CeilToInt(InitialSandDepthMeters / CellSizeMeters));
}

int64 FSandSolverParameters::GetInitialParticleCount() const
{
    const FIntVector Cells = GetInitialOccupiedCellResolution();
    return static_cast<int64>(Cells.X) * Cells.Y * Cells.Z * ParticlesPerCell;
}

FVector3f FSandSolverParameters::GetDomainMinimumMeters() const
{
    return FVector3f(
        static_cast<float>(-0.5 * DomainSizeMeters.X),
        static_cast<float>(-0.5 * DomainSizeMeters.Y),
        0.0f);
}

float FSandSolverParameters::GetFixedDeltaSeconds() const
{
    return 1.0f / FixedSimulationHz;
}
