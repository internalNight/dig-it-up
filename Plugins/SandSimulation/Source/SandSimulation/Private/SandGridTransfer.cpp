#include "GlobalShader.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

namespace Sand::GridTransfer
{
struct alignas(16) FParticleData
{
    FVector4f PositionAndMass;
    FVector4f VelocityAndPlasticVolume;
};

static_assert(sizeof(FParticleData) == 32);

float BitsAsFloat(const uint32 Bits)
{
    float Value;
    FMemory::Memcpy(&Value, &Bits, sizeof(float));
    return Value;
}

class FSandP2GCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandP2GCS);
    SHADER_USE_PARAMETER_STRUCT(FSandP2GCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, ParticleCount)
        SHADER_PARAMETER(uint32, GridSizeX)
        SHADER_PARAMETER(uint32, GridSizeY)
        SHADER_PARAMETER(uint32, GridSizeZ)
        SHADER_PARAMETER(float, CellSizeMeters)
        SHADER_PARAMETER(FVector3f, GridOriginMeters)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FParticleData>, ParticlesIn)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, GridScalars)
    END_SHADER_PARAMETER_STRUCT()
};

class FSandUpdateGridCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandUpdateGridCS);
    SHADER_USE_PARAMETER_STRUCT(FSandUpdateGridCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, GridSizeX)
        SHADER_PARAMETER(uint32, GridSizeY)
        SHADER_PARAMETER(uint32, GridSizeZ)
        SHADER_PARAMETER(float, CellSizeMeters)
        SHADER_PARAMETER(float, DeltaTimeSeconds)
        SHADER_PARAMETER(float, GravityMetersPerSecondSquared)
        SHADER_PARAMETER(FVector3f, GridOriginMeters)
        SHADER_PARAMETER(FVector3f, PhysicalDomainMinimumMeters)
        SHADER_PARAMETER(FVector3f, PhysicalDomainMaximumMeters)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, GridScalars)
    END_SHADER_PARAMETER_STRUCT()
};

class FSandG2PCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandG2PCS);
    SHADER_USE_PARAMETER_STRUCT(FSandG2PCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, ParticleCount)
        SHADER_PARAMETER(uint32, GridSizeX)
        SHADER_PARAMETER(uint32, GridSizeY)
        SHADER_PARAMETER(uint32, GridSizeZ)
        SHADER_PARAMETER(float, CellSizeMeters)
        SHADER_PARAMETER(FVector3f, GridOriginMeters)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FParticleData>, ParticlesIn)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, GridScalarsIn)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FParticleData>, ParticlesOut)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSandP2GCS, "/SandSimulation/Private/SandGridTransfer.usf", "P2GCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandUpdateGridCS, "/SandSimulation/Private/SandGridTransfer.usf", "UpdateGridCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandG2PCS, "/SandSimulation/Private/SandGridTransfer.usf", "G2PCS", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackParameters, )
    RDG_BUFFER_ACCESS(SourceBuffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandGridTransferTest,
    "SandSimulation.GPU.GridTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandGridTransferTest::RunTest(const FString& Parameters)
{
    using namespace Sand::GridTransfer;

    constexpr uint32 GridSize = 7;
    constexpr uint32 GridNodeCount = GridSize * GridSize * GridSize;
    constexpr uint32 GridScalarCount = 4 * GridNodeCount;
    constexpr float CellSize = 0.5f;
    constexpr float DeltaTime = 1.0f / 120.0f;
    constexpr float Gravity = -9.81f;
    const FVector3f GridOrigin(-1.5f, -1.5f, -0.5f);
    const FVector3f PhysicalMinimum(-1.0f, -1.0f, 0.0f);
    const FVector3f PhysicalMaximum(1.0f, 1.0f, 2.0f);

    TArray<FParticleData> InputParticles;
    InputParticles.SetNum(4);
    InputParticles[0] = { FVector4f(-0.125f, -0.125f, 0.875f, 1.0f), FVector4f(0.0f, 0.0f, 0.0f, 1.0f) };
    InputParticles[1] = { FVector4f( 0.125f,  0.125f, 0.875f, 1.0f), FVector4f(0.0f, 0.0f, 0.0f, 1.0f) };
    InputParticles[2] = { FVector4f( 0.125f, -0.125f, 1.125f, 1.0f), FVector4f(0.0f, 0.0f, 0.0f, 1.0f) };
    InputParticles[3] = { FVector4f(-0.125f,  0.125f, 1.125f, 1.0f), FVector4f(0.0f, 0.0f, 0.0f, 1.0f) };

    TArray<FParticleData> OutputParticles;
    OutputParticles.SetNumZeroed(InputParticles.Num());
    TArray<uint32> GridResults;
    GridResults.SetNumZeroed(GridScalarCount);
    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    ENQUEUE_RENDER_COMMAND(SandGridTransferTest)(
        [CompletionEvent, InputParticles, &OutputParticles, &GridResults, GridOrigin, PhysicalMinimum, PhysicalMaximum](FRHICommandListImmediate& RHICmdList)
        {
            if (RHICmdList.GetPipeline() == ERHIPipeline::None)
            {
                RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Sand.Test.ParticlesIn"), InputParticles);
            FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(FParticleData), InputParticles.Num()),
                TEXT("Sand.Test.ParticlesOut"));
            FRDGBufferRef GridBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GridScalarCount),
                TEXT("Sand.Test.GridScalars"));
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GridBuffer), 0u);

            FSandP2GCS::FParameters* P2GParameters = GraphBuilder.AllocParameters<FSandP2GCS::FParameters>();
            P2GParameters->ParticleCount = InputParticles.Num();
            P2GParameters->GridSizeX = GridSize;
            P2GParameters->GridSizeY = GridSize;
            P2GParameters->GridSizeZ = GridSize;
            P2GParameters->CellSizeMeters = CellSize;
            P2GParameters->GridOriginMeters = GridOrigin;
            P2GParameters->ParticlesIn = GraphBuilder.CreateSRV(InputBuffer);
            P2GParameters->GridScalars = GraphBuilder.CreateUAV(GridBuffer);
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("Sand.Grid.P2G"),
                TShaderMapRef<FSandP2GCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                P2GParameters,
                FComputeShaderUtils::GetGroupCount(InputParticles.Num(), 64));

            FSandUpdateGridCS::FParameters* UpdateParameters = GraphBuilder.AllocParameters<FSandUpdateGridCS::FParameters>();
            UpdateParameters->GridSizeX = GridSize;
            UpdateParameters->GridSizeY = GridSize;
            UpdateParameters->GridSizeZ = GridSize;
            UpdateParameters->CellSizeMeters = CellSize;
            UpdateParameters->DeltaTimeSeconds = DeltaTime;
            UpdateParameters->GravityMetersPerSecondSquared = Gravity;
            UpdateParameters->GridOriginMeters = GridOrigin;
            UpdateParameters->PhysicalDomainMinimumMeters = PhysicalMinimum;
            UpdateParameters->PhysicalDomainMaximumMeters = PhysicalMaximum;
            UpdateParameters->GridScalars = GraphBuilder.CreateUAV(GridBuffer);
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("Sand.Grid.Update"),
                TShaderMapRef<FSandUpdateGridCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                UpdateParameters,
                FComputeShaderUtils::GetGroupCount(GridNodeCount, 64));

            FSandG2PCS::FParameters* G2PParameters = GraphBuilder.AllocParameters<FSandG2PCS::FParameters>();
            G2PParameters->ParticleCount = InputParticles.Num();
            G2PParameters->GridSizeX = GridSize;
            G2PParameters->GridSizeY = GridSize;
            G2PParameters->GridSizeZ = GridSize;
            G2PParameters->CellSizeMeters = CellSize;
            G2PParameters->GridOriginMeters = GridOrigin;
            G2PParameters->ParticlesIn = GraphBuilder.CreateSRV(InputBuffer);
            G2PParameters->GridScalarsIn = GraphBuilder.CreateSRV(GridBuffer);
            G2PParameters->ParticlesOut = GraphBuilder.CreateUAV(OutputBuffer);
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("Sand.Grid.G2P"),
                TShaderMapRef<FSandG2PCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                G2PParameters,
                FComputeShaderUtils::GetGroupCount(InputParticles.Num(), 64));

            FReadbackParameters* ParticleReadbackParameters = GraphBuilder.AllocParameters<FReadbackParameters>();
            ParticleReadbackParameters->SourceBuffer = OutputBuffer;
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("Sand.Grid.ParticleReadback"),
                ParticleReadbackParameters,
                ERDGPassFlags::Readback,
                [&OutputParticles, ParticleReadbackParameters](FRHICommandListImmediate& ReadbackCommandList)
                {
                    const uint32 NumBytes = OutputParticles.Num() * sizeof(FParticleData);
                    FRHIGPUBufferReadback Readback(TEXT("Sand.Grid.ParticleReadback"));
                    Readback.EnqueueCopy(ReadbackCommandList, ParticleReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                    ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                    const void* SourceData = Readback.Lock(NumBytes);
                    FMemory::Memcpy(OutputParticles.GetData(), SourceData, NumBytes);
                    Readback.Unlock();
                });

            FReadbackParameters* GridReadbackParameters = GraphBuilder.AllocParameters<FReadbackParameters>();
            GridReadbackParameters->SourceBuffer = GridBuffer;
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("Sand.Grid.StateReadback"),
                GridReadbackParameters,
                ERDGPassFlags::Readback,
                [&GridResults, GridReadbackParameters](FRHICommandListImmediate& ReadbackCommandList)
                {
                    constexpr uint32 NumBytes = GridScalarCount * sizeof(uint32);
                    FRHIGPUBufferReadback Readback(TEXT("Sand.Grid.StateReadback"));
                    Readback.EnqueueCopy(ReadbackCommandList, GridReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                    ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                    const void* SourceData = Readback.Lock(NumBytes);
                    FMemory::Memcpy(GridResults.GetData(), SourceData, NumBytes);
                    Readback.Unlock();
                });

            GraphBuilder.Execute();
            CompletionEvent->Trigger();
        });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    double GridMass = 0.0;
    for (uint32 NodeIndex = 0; NodeIndex < GridNodeCount; ++NodeIndex)
    {
        GridMass += BitsAsFloat(GridResults[4 * NodeIndex]);
    }

    bool bGravityTransferred = true;
    bool bHorizontalVelocityStayedZero = true;
    for (const FParticleData& Particle : OutputParticles)
    {
        bGravityTransferred &= FMath::IsNearlyEqual(
            Particle.VelocityAndPlasticVolume.Z,
            Gravity * DeltaTime,
            1.0e-5f);
        bHorizontalVelocityStayedZero &= FMath::IsNearlyZero(Particle.VelocityAndPlasticVolume.X, 1.0e-6f) &&
            FMath::IsNearlyZero(Particle.VelocityAndPlasticVolume.Y, 1.0e-6f);
    }

    TestTrue(TEXT("P2G conserves total particle mass"), FMath::IsNearlyEqual(GridMass, 4.0, 1.0e-5));
    TestTrue(TEXT("Grid gravity transfers back to every particle"), bGravityTransferred);
    TestTrue(TEXT("Gravity does not create horizontal velocity"), bHorizontalVelocityStayedZero);
    return true;
}

#endif
