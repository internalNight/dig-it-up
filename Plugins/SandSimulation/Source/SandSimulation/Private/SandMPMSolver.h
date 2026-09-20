#pragma once

#include "GlobalShader.h"
#include "Containers/StaticArray.h"
#include "RenderGraphResources.h"
#include "SandSimulationTypes.h"
#include "ShaderParameterStruct.h"
#include "Templates/Function.h"

namespace Sand::MPM
{
float CouplingStepSeconds();
inline float FittedInternalStep(float Outer,float MaximumInternal) {
    const int32 Count=FMath::Max(1,FMath::CeilToInt(Outer/MaximumInternal-1.e-4f));
    return Outer/Count;
}
struct alignas(16) FParticleData
{
    FVector4f PositionAndMass;
    FVector4f VelocityAndVolume;
    FVector4f StressRow0AndCompaction;
    FVector4f StressRemainder;
    /** Local bucket-space position in xyz and a sub-grid carried flag in w. */
    FVector4f BucketLocalAndCarried;
};

static_assert(sizeof(FParticleData) == 80);

class FSandMPMP2GCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandMPMP2GCS);
    SHADER_USE_PARAMETER_STRUCT(FSandMPMP2GCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, MPMParticleCount)
        SHADER_PARAMETER(uint32, MPMGridSizeX)
        SHADER_PARAMETER(uint32, MPMGridSizeY)
        SHADER_PARAMETER(uint32, MPMGridSizeZ)
        SHADER_PARAMETER(float, MPMCellSizeMeters)
        SHADER_PARAMETER(float, MPMDeltaTimeSeconds)
        SHADER_PARAMETER(FVector3f, MPMGridOriginMeters)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FParticleData>, MPMParticlesIn)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, MPMGridScalars)
    END_SHADER_PARAMETER_STRUCT()
};

class FSandMPMUpdateGridCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandMPMUpdateGridCS);
    SHADER_USE_PARAMETER_STRUCT(FSandMPMUpdateGridCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, MPMGridSizeX)
        SHADER_PARAMETER(uint32, MPMGridSizeY)
        SHADER_PARAMETER(uint32, MPMGridSizeZ)
        SHADER_PARAMETER(float, MPMCellSizeMeters)
        SHADER_PARAMETER(float, MPMDeltaTimeSeconds)
        SHADER_PARAMETER(float, MPMGravityMetersPerSecondSquared)
        SHADER_PARAMETER(float, MPMBoundaryFriction)
        SHADER_PARAMETER(FVector3f, MPMGridOriginMeters)
        SHADER_PARAMETER(FVector3f, MPMPhysicalDomainMinimumMeters)
        SHADER_PARAMETER(FVector3f, MPMPhysicalDomainMaximumMeters)
        SHADER_PARAMETER(uint32, MPMToolColliderCount)
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolCentersMeters, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAxesX, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAxesY, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAxesZ, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolHalfExtentsMeters, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolLinearVelocitiesMetersPerSecond, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAngularVelocitiesRadiansPerSecond, [64])
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, MPMGridScalars)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, MPMToolImpulseScalars)
    END_SHADER_PARAMETER_STRUCT()
};

class FSandMPMG2PCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandMPMG2PCS);
    SHADER_USE_PARAMETER_STRUCT(FSandMPMG2PCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, MPMParticleCount)
        SHADER_PARAMETER(uint32, MPMGridSizeX)
        SHADER_PARAMETER(uint32, MPMGridSizeY)
        SHADER_PARAMETER(uint32, MPMGridSizeZ)
        SHADER_PARAMETER(float, MPMCellSizeMeters)
        SHADER_PARAMETER(float, MPMDeltaTimeSeconds)
        SHADER_PARAMETER(float, MPMLameMuPa)
        SHADER_PARAMETER(float, MPMLameLambdaPa)
        SHADER_PARAMETER(float, MPMFrictionSlope)
        SHADER_PARAMETER(float, MPMCohesionInterceptPa)
        SHADER_PARAMETER(float, MPMHardeningRate)
        SHADER_PARAMETER(uint32, MPMObjectiveMaterial)
        SHADER_PARAMETER(float, MPMDilationSlope)
        SHADER_PARAMETER(float, MPMVelocityDampingPerSecond)
        SHADER_PARAMETER(float, MPMBoundaryFriction)
        SHADER_PARAMETER(FVector3f, MPMGridOriginMeters)
        SHADER_PARAMETER(FVector3f, MPMPhysicalDomainMinimumMeters)
        SHADER_PARAMETER(FVector3f, MPMPhysicalDomainMaximumMeters)
        SHADER_PARAMETER(uint32, MPMToolColliderCount)
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolCentersMeters, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAxesX, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAxesY, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAxesZ, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolHalfExtentsMeters, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolLinearVelocitiesMetersPerSecond, [64])
        SHADER_PARAMETER_ARRAY(FVector4f, MPMToolAngularVelocitiesRadiansPerSecond, [64])
        SHADER_PARAMETER(uint32, MPMBucketInteriorEnabled)
        SHADER_PARAMETER(float, MPMBucketInteriorDampingPerSecond)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorCenterMeters)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorAxisX)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorAxisY)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorAxisZ)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorHalfExtentsMeters)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorLinearVelocityMetersPerSecond)
        SHADER_PARAMETER(FVector3f, MPMBucketInteriorAngularVelocityRadiansPerSecond)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FParticleData>, MPMParticlesIn)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, MPMGridScalarsIn)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, MPMToolImpulseScalars)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FParticleData>, MPMParticlesOut)
    END_SHADER_PARAMETER_STRUCT()
};

TArray<FParticleData> MakeInitialColumn(
    float CellSize,
    int32 ParticlesPerCell,
    float Density,
    float FrictionAngleDegrees);

/** Creates the complete movable sand body; no lower layer is frozen or omitted. */
TArray<FParticleData> MakeInitialSandbox(
    float CellSize,
    float Density,
    float FrictionAngleDegrees,
    float SandDepthMeters = 2.0f,
    float WidthMeters = 5.0f,
    bool bLunarTerrain = false);

using FCollapseFramesCallback = TUniqueFunction<void(TArray<TArray<FParticleData>>&& Frames, float FrameDeltaSeconds)>;

/** Runs the validated small 3D collapse entirely on the GPU and returns full particle snapshots on the game thread. */
void EnqueueValidatedColumnCollapse(
    const FSandMaterialParameters& Material,
    FCollapseFramesCallback&& Completion);

struct FRuntimeSimulationState
{
    TArray<FParticleData> InitialParticles;
    TRefCountPtr<FRDGPooledBuffer> ParticleBuffer;
    FSandMaterialParameters Material;
    FVector3f PhysicalMinimum = FVector3f(-0.75f, -0.375f, 0.0f);
    FVector3f PhysicalMaximum = FVector3f(0.75f, 0.375f, 1.0f);
    FVector3f GridOrigin = FVector3f(-0.80f, -0.425f, -0.05f);
    FIntVector GridSize = FIntVector(33, 18, 23);
    float CellSize = 0.05f;
    float InternalDeltaSeconds = 1.0f / 300.0f;
    bool bInitialized = false;
};

struct FToolOrientedBoxState
{
    // Optional analytic machine motion evaluated at each MPM substep.
    FVector3f BaseVelocity = FVector3f::ZeroVector;
    uint8 Shape = 0; // 0 box, 1 capped cylinder along local Y
    float ChainDriveRatio=0, SurfaceOffset=0;
    uint8 Motion = 0; // 0 static sample, 1 drum rotor, 2 chain, 3 arbitrary-axis rotor, 4 endless-belt straight surface
    FVector3f RotorAxis = FVector3f(0,1,0);
    FVector3f RotorCenter = FVector3f(.51f,0,.01f);
    FVector3f RotorOffset = FVector3f::ZeroVector;
    FQuat4f RotorOrientation = FQuat4f::Identity;
    FVector3f MotionOrigin = FVector3f::ZeroVector;
    FQuat4f MotionRotation = FQuat4f::Identity;
    float SeparationSpeedLimit = 2.0f;
    // Negative uses the material-wide tool coefficient. Non-negative values
    // describe a distinct physical surface such as a polished screw flight.
    float FrictionCoefficient = -1.0f;
    float OrbitRadius = .145f;
    float ChainFront = .32f;
    float Phase = 0;
    float Speed = 0;
    FVector3f CenterMeters = FVector3f::ZeroVector;
    FVector3f AxisX = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f AxisY = FVector3f(0.0f, 1.0f, 0.0f);
    FVector3f AxisZ = FVector3f(0.0f, 0.0f, 1.0f);
    FVector3f HalfExtentsMeters = FVector3f(0.05f);
    FVector3f LinearVelocityMetersPerSecond = FVector3f::ZeroVector;
    FVector3f AngularVelocityRadiansPerSecond = FVector3f::ZeroVector;
};

FToolOrientedBoxState SampleMachineCollider(const FToolOrientedBoxState& C, float Time);

struct FToolColliderState
{
    static constexpr uint32 MaxColliderCount = 64;
    uint32 ColliderCount = 0;
    TStaticArray<FToolOrientedBoxState, MaxColliderCount> Colliders;
    bool bBucketInteriorEnabled = false;
    float BucketInteriorDampingPerSecond = 6.0f;
    FToolOrientedBoxState BucketInterior;

    FToolOrientedBoxState& AddCollider()
    {
        check(ColliderCount < MaxColliderCount);
        return Colliders[ColliderCount++];
    }
};

struct FToolInteractionResult
{
    /** Momentum transferred from the tool to sand over this outer simulation step (kg*m/s). */
    TStaticArray<FVector3f, 64> ColliderLinear{};
    TStaticArray<FVector3f, 64> ColliderAngular{};
    FVector3f SandLinearImpulseKgMetersPerSecond = FVector3f::ZeroVector;
    /** Angular impulse about the tool center transferred from the tool to sand (kg*m^2/s). */
    FVector3f SandAngularImpulseKgMetersSquaredPerSecond = FVector3f::ZeroVector;
};

using FRuntimeStepCallback = TUniqueFunction<void(
    TArray<FParticleData>&& Particles,
    const FToolInteractionResult& ToolInteraction,
    double GpuSeconds)>;

TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> CreateRuntimeColumnSimulation(
    const FSandMaterialParameters& Material);

TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> CreateRuntimeSandboxSimulation(
    const FSandMaterialParameters& Material);

/** Advances persistent GPU particle state. Callback is delivered on the game thread. */
void EnqueueRuntimeSimulationSteps(
    TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> State,
    uint32 InternalStepCount,
    bool bReadbackParticles,
    const FToolColliderState& Tool,
    FRuntimeStepCallback&& Completion);
}
