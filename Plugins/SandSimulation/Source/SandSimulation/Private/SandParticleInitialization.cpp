#include "SandSimulationTypes.h"

#include "GlobalShader.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

namespace Sand::ParticleInitialization
{
struct alignas(16) FParticleData
{
    FVector4f PositionAndMass;
    FVector4f VelocityAndPlasticVolume;
};

static_assert(sizeof(FParticleData) == 32);

class FSandParticleInitializationCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandParticleInitializationCS);
    SHADER_USE_PARAMETER_STRUCT(FSandParticleInitializationCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, GridSizeX)
        SHADER_PARAMETER(uint32, GridSizeY)
        SHADER_PARAMETER(uint32, GridSizeZ)
        SHADER_PARAMETER(uint32, ParticlesPerCell)
        SHADER_PARAMETER(uint32, ParticleCount)
        SHADER_PARAMETER(float, CellSizeMeters)
        SHADER_PARAMETER(float, BulkDensityKgPerM3)
        SHADER_PARAMETER(float, InitialPlasticVolume)
        SHADER_PARAMETER(FVector3f, DomainMinimumMeters)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FParticleData>, Particles)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(
    FSandParticleInitializationCS,
    "/SandSimulation/Private/SandParticleInitialization.usf",
    "MainCS",
    SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackParameters, )
    RDG_BUFFER_ACCESS(SourceBuffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandParameterDefaultsTest,
    "SandSimulation.Parameters.Defaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandParameterDefaultsTest::RunTest(const FString& Parameters)
{
    const FSandSolverParameters Solver;
    const FSandMaterialParameters Material;

    TestTrue(TEXT("Default solver parameters are valid"), Solver.IsValid());
    TestTrue(TEXT("Default material parameters are valid"), Material.IsValid());
    TestEqual(TEXT("Grid cells"), Solver.GetGridCellResolution(), FIntVector(160, 160, 96));
    TestEqual(TEXT("Grid nodes including quadratic-kernel ghosts"), Solver.GetGridNodeResolution(), FIntVector(163, 163, 99));
    TestEqual(TEXT("Occupied cells"), Solver.GetInitialOccupiedCellResolution(), FIntVector(160, 160, 64));
    TestEqual(TEXT("Initial particles"), Solver.GetInitialParticleCount(), static_cast<int64>(6553600));
    TestEqual(TEXT("Fixed step"), Solver.GetFixedDeltaSeconds(), 1.0f / 30.0f);
    TestTrue(TEXT("Shear friction converts to a positive DP alpha"), Material.GetDruckerPragerFrictionAlpha() > 0.0f);
    TestEqual(TEXT("Zero cohesion stays zero"), Material.GetDruckerPragerCohesionInterceptPa(), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandParticleInitializationTest,
    "SandSimulation.GPU.ParticleInitialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandParticleInitializationTest::RunTest(const FString& Parameters)
{
    using namespace Sand::ParticleInitialization;

    constexpr uint32 GridSizeX = 4;
    constexpr uint32 GridSizeY = 3;
    constexpr uint32 GridSizeZ = 2;
    constexpr uint32 ParticlesPerCell = 4;
    constexpr uint32 ParticleCount = GridSizeX * GridSizeY * GridSizeZ * ParticlesPerCell;
    constexpr float CellSizeMeters = 0.125f;
    constexpr float BulkDensity = 1550.0f;
    constexpr float InitialPlasticVolume = 0.93f;
    const FVector3f DomainMinimum(-0.25f, -0.1875f, 0.0f);

    TArray<FParticleData> Results;
    Results.SetNumZeroed(ParticleCount);
    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    ENQUEUE_RENDER_COMMAND(SandParticleInitializationTest)(
        [CompletionEvent, &Results, DomainMinimum](FRHICommandListImmediate& RHICmdList)
        {
            if (RHICmdList.GetPipeline() == ERHIPipeline::None)
            {
                RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef ParticleBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(FParticleData), ParticleCount),
                TEXT("Sand.Test.InitialParticles"));

            FSandParticleInitializationCS::FParameters* ComputeParameters =
                GraphBuilder.AllocParameters<FSandParticleInitializationCS::FParameters>();
            ComputeParameters->GridSizeX = GridSizeX;
            ComputeParameters->GridSizeY = GridSizeY;
            ComputeParameters->GridSizeZ = GridSizeZ;
            ComputeParameters->ParticlesPerCell = ParticlesPerCell;
            ComputeParameters->ParticleCount = ParticleCount;
            ComputeParameters->CellSizeMeters = CellSizeMeters;
            ComputeParameters->BulkDensityKgPerM3 = BulkDensity;
            ComputeParameters->InitialPlasticVolume = InitialPlasticVolume;
            ComputeParameters->DomainMinimumMeters = DomainMinimum;
            ComputeParameters->Particles = GraphBuilder.CreateUAV(ParticleBuffer);

            const TShaderMapRef<FSandParticleInitializationCS> ComputeShader(
                GetGlobalShaderMap(GMaxRHIFeatureLevel));
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("Sand.Particles.Initialize"),
                ComputeShader,
                ComputeParameters,
                FComputeShaderUtils::GetGroupCount(ParticleCount, 64));

            FReadbackParameters* ReadbackParameters = GraphBuilder.AllocParameters<FReadbackParameters>();
            ReadbackParameters->SourceBuffer = ParticleBuffer;
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("Sand.Particles.InitializeReadback"),
                ReadbackParameters,
                ERDGPassFlags::Readback,
                [&Results, ReadbackParameters](FRHICommandListImmediate& ReadbackCommandList)
                {
                    constexpr uint32 NumBytes = ParticleCount * sizeof(FParticleData);
                    FRHIGPUBufferReadback Readback(TEXT("Sand.Particles.InitializeReadback"));
                    Readback.EnqueueCopy(ReadbackCommandList, ReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                    ReadbackCommandList.SubmitAndBlockUntilGPUIdle();

                    const void* SourceData = Readback.Lock(NumBytes);
                    FMemory::Memcpy(Results.GetData(), SourceData, NumBytes);
                    Readback.Unlock();
                });

            GraphBuilder.Execute();
            CompletionEvent->Trigger();
        });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    const float ExpectedParticleMass = BulkDensity * FMath::Pow(CellSizeMeters, 3.0f) / ParticlesPerCell;
    double TotalMass = 0.0;
    bool bAllParticlesInBounds = true;
    bool bAllVelocitiesZero = true;
    bool bAllPlasticVolumesInitialized = true;

    const FVector3f DomainMaximum = DomainMinimum + FVector3f(
        GridSizeX * CellSizeMeters,
        GridSizeY * CellSizeMeters,
        GridSizeZ * CellSizeMeters);

    for (const FParticleData& Particle : Results)
    {
        const FVector3f Position(
            Particle.PositionAndMass.X,
            Particle.PositionAndMass.Y,
            Particle.PositionAndMass.Z);
        TotalMass += Particle.PositionAndMass.W;
        bAllParticlesInBounds &=
            Position.X > DomainMinimum.X && Position.X < DomainMaximum.X &&
            Position.Y > DomainMinimum.Y && Position.Y < DomainMaximum.Y &&
            Position.Z > DomainMinimum.Z && Position.Z < DomainMaximum.Z;
        bAllVelocitiesZero &= FVector3f(
            Particle.VelocityAndPlasticVolume.X,
            Particle.VelocityAndPlasticVolume.Y,
            Particle.VelocityAndPlasticVolume.Z).IsNearlyZero();
        bAllPlasticVolumesInitialized &= FMath::IsNearlyEqual(
            Particle.VelocityAndPlasticVolume.W,
            InitialPlasticVolume);
    }

    const double ExpectedTotalMass = BulkDensity *
        (GridSizeX * CellSizeMeters) *
        (GridSizeY * CellSizeMeters) *
        (GridSizeZ * CellSizeMeters);
    TestTrue(TEXT("Every point lies strictly inside its 3D material volume"), bAllParticlesInBounds);
    TestTrue(TEXT("Every point starts at rest"), bAllVelocitiesZero);
    TestTrue(TEXT("Every point receives its plastic-volume state"), bAllPlasticVolumesInitialized);
    TestTrue(TEXT("Per-particle mass is correct"), FMath::IsNearlyEqual(Results[0].PositionAndMass.W, ExpectedParticleMass));
    TestTrue(TEXT("Total initialized mass matches density times volume"), FMath::IsNearlyEqual(TotalMass, ExpectedTotalMass, 0.01));
    return true;
}

#endif
