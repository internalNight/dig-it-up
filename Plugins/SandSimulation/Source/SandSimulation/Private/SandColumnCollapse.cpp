#include "SandSimulationTypes.h"
#include "SandMPMSolver.h"

#include "GlobalShader.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

namespace Sand::ColumnCollapse
{
BEGIN_SHADER_PARAMETER_STRUCT(FReadbackParameters, )
    RDG_BUFFER_ACCESS(SourceBuffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

bool WriteCollapseFrames(const TArray<TArray<MPM::FParticleData>>& Frames, const float FrameDeltaSeconds)
{
    const FString ArtifactDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Artifacts"));
    IFileManager::Get().MakeDirectory(*ArtifactDirectory, true);
    FString Json;
    Json.Reserve(Frames.Num() * 16000);
    Json += FString::Printf(TEXT("{\"frameDt\":%.6f,\"sampleMethod\":\"stable-hash-1-in-8\",\"frames\":["), FrameDeltaSeconds);
    for (int32 FrameIndex = 0; FrameIndex < Frames.Num(); ++FrameIndex)
    {
        if (FrameIndex > 0) Json += TEXT(",");
        Json += TEXT("[");
        const TArray<MPM::FParticleData>& Frame = Frames[FrameIndex];
        bool bFirstParticle = true;
        for (int32 ParticleIndex = 0; ParticleIndex < Frame.Num(); ++ParticleIndex)
        {
            uint32 Hash = static_cast<uint32>(ParticleIndex);
            Hash ^= Hash >> 16;
            Hash *= 0x7feb352du;
            Hash ^= Hash >> 15;
            Hash *= 0x846ca68bu;
            Hash ^= Hash >> 16;
            if ((Hash & 7u) != 0u)
            {
                continue;
            }
            if (!bFirstParticle) Json += TEXT(",");
            bFirstParticle = false;
            const FVector4f P = Frame[ParticleIndex].PositionAndMass;
            Json += FString::Printf(TEXT("[%.4f,%.4f,%.4f]"), P.X, P.Y, P.Z);
        }
        Json += TEXT("]");
    }
    Json += TEXT("]}");
    return FFileHelper::SaveStringToFile(
        Json,
        *FPaths::Combine(ArtifactDirectory, TEXT("column-collapse-data.json")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandFullSandboxInitializationTest,
    "SandSimulation.Runtime.FullSandboxInitialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandFullSandboxInitializationTest::RunTest(const FString& Parameters)
{
    using namespace Sand::MPM;
    constexpr float CellSizeMeters = 0.0625f;
    constexpr float DensityKgPerM3 = 1600.0f;
    const TArray<FParticleData> Particles = MakeInitialSandbox(
        CellSizeMeters, DensityKgPerM3, 30.0f);
    TestEqual(TEXT("The complete 5x5x2 m body contains one point per 6.25 cm cell"),
        Particles.Num(), 204800);

    double TotalMassKg = 0.0;
    float MinimumZ = TNumericLimits<float>::Max();
    float MaximumZ = TNumericLimits<float>::Lowest();
    bool bAllFinite = true;
    for (const FParticleData& Particle : Particles)
    {
        TotalMassKg += Particle.PositionAndMass.W;
        MinimumZ = FMath::Min(MinimumZ, Particle.PositionAndMass.Z);
        MaximumZ = FMath::Max(MaximumZ, Particle.PositionAndMass.Z);
        bAllFinite &= FMath::IsFinite(Particle.PositionAndMass.X) &&
            FMath::IsFinite(Particle.PositionAndMass.Y) &&
            FMath::IsFinite(Particle.PositionAndMass.Z) &&
            FMath::IsFinite(Particle.PositionAndMass.W);
    }
    TestTrue(TEXT("All sandbox particle values are finite"), bAllFinite);
    TestTrue(TEXT("The lowest layer is active near the container floor"),
        FMath::IsNearlyEqual(MinimumZ, 0.03125f, 1.0e-5f));
    TestTrue(TEXT("The highest initial layer ends at the 2 m sand surface"),
        FMath::IsNearlyEqual(MaximumZ, 1.96875f, 1.0e-5f));
    TestTrue(TEXT("The full volume has the expected 80,000 kg dry-sand mass"),
        FMath::IsNearlyEqual(TotalMassKg, 80000.0, 0.1));
    const auto Easy = MakeInitialSandbox(CellSizeMeters,DensityKgPerM3,40.0f,1.5f);
    TestEqual(TEXT("Easy level contains 153600 active points"),Easy.Num(),153600);
    TestTrue(TEXT("Easy surface matches 1.5 m fill"),FMath::IsNearlyEqual(Easy.Last().PositionAndMass.Z,1.46875f));
    TestTrue(TEXT("Easy bottom remains active"),FMath::IsNearlyEqual(Easy[0].PositionAndMass.Z,0.03125f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandColumnCollapseTest,
    "SandSimulation.GPU.ColumnCollapse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandColumnCollapseTest::RunTest(const FString& Parameters)
{
    using namespace Sand::ColumnCollapse;
    using namespace Sand::MPM;

    constexpr float CellSize = 0.05f;
    constexpr float DeltaTime = 1.0f / 300.0f;
    constexpr uint32 TotalSteps = 360;
    constexpr uint32 SnapshotStride = 10;
    constexpr uint32 GridSizeX = 33;
    constexpr uint32 GridSizeY = 18;
    constexpr uint32 GridSizeZ = 23;
    constexpr uint32 GridNodeCount = GridSizeX * GridSizeY * GridSizeZ;
    constexpr uint32 GridScalarCount = 4 * GridNodeCount;
    const FVector3f PhysicalMinimum(-0.75f, -0.375f, 0.0f);
    const FVector3f PhysicalMaximum(0.75f, 0.375f, 1.0f);
    const FVector3f GridOrigin = PhysicalMinimum - FVector3f(CellSize);

    const FSandMaterialParameters Material;
    TArray<FParticleData> InitialParticles = MakeInitialColumn(
        CellSize,
        4,
        Material.BulkDensityKgPerM3,
        Material.InternalFrictionAngleDegrees);
    const uint32 ParticleCount = InitialParticles.Num();
    TArray<TArray<FParticleData>> Frames;
    Frames.SetNum(TotalSteps / SnapshotStride + 1);
    Frames[0] = InitialParticles;
    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    ENQUEUE_RENDER_COMMAND(SandColumnCollapseTest)(
        [CompletionEvent, InitialParticles, &Frames, PhysicalMinimum, PhysicalMaximum, GridOrigin, Material](FRHICommandListImmediate& RHICmdList)
        {
            if (RHICmdList.GetPipeline() == ERHIPipeline::None)
            {
                RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef ParticleA = CreateStructuredBuffer(GraphBuilder, TEXT("Sand.Collapse.ParticlesA"), InitialParticles);
            FRDGBufferRef ParticleB = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(FParticleData), InitialParticles.Num()),
                TEXT("Sand.Collapse.ParticlesB"));
            FRDGBufferRef GridBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GridScalarCount),
                TEXT("Sand.Collapse.Grid"));
            FRDGBufferRef DisabledToolImpulseBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 6),
                TEXT("Sand.Collapse.DisabledToolImpulse"));
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DisabledToolImpulseBuffer), 0u);
            FRDGBufferRef CurrentParticles = ParticleA;
            FRDGBufferRef NextParticles = ParticleB;
            uint32 SnapshotIndex = 1;

            for (uint32 Step = 1; Step <= TotalSteps; ++Step)
            {
                AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GridBuffer), 0u);

                FSandMPMP2GCS::FParameters* P2G = GraphBuilder.AllocParameters<FSandMPMP2GCS::FParameters>();
                P2G->MPMParticleCount = InitialParticles.Num();
                P2G->MPMGridSizeX = GridSizeX;
                P2G->MPMGridSizeY = GridSizeY;
                P2G->MPMGridSizeZ = GridSizeZ;
                P2G->MPMCellSizeMeters = CellSize;
                P2G->MPMDeltaTimeSeconds = DeltaTime;
                P2G->MPMGridOriginMeters = GridOrigin;
                P2G->MPMParticlesIn = GraphBuilder.CreateSRV(CurrentParticles);
                P2G->MPMGridScalars = GraphBuilder.CreateUAV(GridBuffer);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.MPM.P2G"),
                    TShaderMapRef<FSandMPMP2GCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    P2G,
                    FComputeShaderUtils::GetGroupCount(InitialParticles.Num(), 64));

                FSandMPMUpdateGridCS::FParameters* Update = GraphBuilder.AllocParameters<FSandMPMUpdateGridCS::FParameters>();
                Update->MPMGridSizeX = GridSizeX;
                Update->MPMGridSizeY = GridSizeY;
                Update->MPMGridSizeZ = GridSizeZ;
                Update->MPMCellSizeMeters = CellSize;
                Update->MPMDeltaTimeSeconds = DeltaTime;
                Update->MPMGravityMetersPerSecondSquared = -9.81f;
                Update->MPMBoundaryFriction = Material.ToolFrictionCoefficient;
                Update->MPMGridOriginMeters = GridOrigin;
                Update->MPMPhysicalDomainMinimumMeters = PhysicalMinimum;
                Update->MPMPhysicalDomainMaximumMeters = PhysicalMaximum;
                Update->MPMToolColliderCount = 0u;
                Update->MPMGridScalars = GraphBuilder.CreateUAV(GridBuffer);
                Update->MPMToolImpulseScalars = GraphBuilder.CreateUAV(DisabledToolImpulseBuffer);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.MPM.UpdateGrid"),
                    TShaderMapRef<FSandMPMUpdateGridCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    Update,
                    FComputeShaderUtils::GetGroupCount(GridNodeCount, 64));

                FSandMPMG2PCS::FParameters* G2P = GraphBuilder.AllocParameters<FSandMPMG2PCS::FParameters>();
                G2P->MPMParticleCount = InitialParticles.Num();
                G2P->MPMGridSizeX = GridSizeX;
                G2P->MPMGridSizeY = GridSizeY;
                G2P->MPMGridSizeZ = GridSizeZ;
                G2P->MPMCellSizeMeters = CellSize;
                G2P->MPMDeltaTimeSeconds = DeltaTime;
                G2P->MPMLameMuPa = Material.GetLameMuPa();
                G2P->MPMLameLambdaPa = Material.GetLameLambdaPa();
                G2P->MPMFrictionSlope = 6.0f * FMath::Sin(FMath::DegreesToRadians(Material.InternalFrictionAngleDegrees)) /
                    (3.0f - FMath::Sin(FMath::DegreesToRadians(Material.InternalFrictionAngleDegrees)));
                G2P->MPMCohesionInterceptPa = Material.GetDruckerPragerCohesionInterceptPa();
                G2P->MPMHardeningRate = Material.HardeningRate;
                G2P->MPMVelocityDampingPerSecond = Material.VelocityDampingPerSecond;
                G2P->MPMBoundaryFriction = Material.ToolFrictionCoefficient;
                G2P->MPMGridOriginMeters = GridOrigin;
                G2P->MPMPhysicalDomainMinimumMeters = PhysicalMinimum;
                G2P->MPMPhysicalDomainMaximumMeters = PhysicalMaximum;
                G2P->MPMToolColliderCount = 0u;
                G2P->MPMBucketInteriorEnabled = 0u;
                G2P->MPMParticlesIn = GraphBuilder.CreateSRV(CurrentParticles);
                G2P->MPMGridScalarsIn = GraphBuilder.CreateSRV(GridBuffer);
                G2P->MPMParticlesOut = GraphBuilder.CreateUAV(NextParticles);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.MPM.G2P"),
                    TShaderMapRef<FSandMPMG2PCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    G2P,
                    FComputeShaderUtils::GetGroupCount(InitialParticles.Num(), 64));

                Swap(CurrentParticles, NextParticles);
                if (Step % SnapshotStride == 0)
                {
                    FReadbackParameters* ReadbackParameters = GraphBuilder.AllocParameters<FReadbackParameters>();
                    ReadbackParameters->SourceBuffer = CurrentParticles;
                    const uint32 ThisSnapshot = SnapshotIndex++;
                    GraphBuilder.AddPass(
                        RDG_EVENT_NAME("Sand.MPM.CollapseSnapshot"),
                        ReadbackParameters,
                        ERDGPassFlags::Readback,
                        [&Frames, ReadbackParameters, ThisSnapshot](FRHICommandListImmediate& ReadbackCommandList)
                        {
                            const uint32 NumBytes = Frames[0].Num() * sizeof(FParticleData);
                            Frames[ThisSnapshot].SetNumUninitialized(Frames[0].Num());
                            FRHIGPUBufferReadback Readback(TEXT("Sand.MPM.CollapseSnapshot"));
                            Readback.EnqueueCopy(ReadbackCommandList, ReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                            ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                            const void* SourceData = Readback.Lock(NumBytes);
                            FMemory::Memcpy(Frames[ThisSnapshot].GetData(), SourceData, NumBytes);
                            Readback.Unlock();
                        });
                }
            }

            GraphBuilder.Execute();
            CompletionEvent->Trigger();
        });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    const TArray<FParticleData>& FinalParticles = Frames.Last();
    bool bAllFiniteAndContained = true;
    float FinalMaximumX = -FLT_MAX;
    float FinalMaximumZ = -FLT_MAX;
    float FinalMinimumZ = FLT_MAX;
    for (const FParticleData& Particle : FinalParticles)
    {
        const FVector3f Position(Particle.PositionAndMass.X, Particle.PositionAndMass.Y, Particle.PositionAndMass.Z);
        bAllFiniteAndContained &= !Position.ContainsNaN() &&
            Position.X >= PhysicalMinimum.X && Position.X <= PhysicalMaximum.X &&
            Position.Y >= PhysicalMinimum.Y && Position.Y <= PhysicalMaximum.Y &&
            Position.Z >= PhysicalMinimum.Z && Position.Z <= PhysicalMaximum.Z;
        FinalMaximumX = FMath::Max(FinalMaximumX, Position.X);
        FinalMaximumZ = FMath::Max(FinalMaximumZ, Position.Z);
        FinalMinimumZ = FMath::Min(FinalMinimumZ, Position.Z);
    }

    TestTrue(TEXT("Column collapse remains finite and inside the open container"), bAllFiniteAndContained);
    TestTrue(TEXT("Released column develops measurable horizontal runout"), FinalMaximumX > -0.10f);
    TestTrue(TEXT("Material remains supported above the floor"), FinalMinimumZ >= 0.0f);
    TestTrue(TEXT("Column height falls during collapse"), FinalMaximumZ < 0.68f);
    TestTrue(TEXT("Collapse frames are written for visual review"), WriteCollapseFrames(Frames, SnapshotStride * DeltaTime));
    AddInfo(FString::Printf(TEXT("Final extent: maxX=%.3f m, minZ=%.3f m, maxZ=%.3f m, particles=%u"),
        FinalMaximumX, FinalMinimumZ, FinalMaximumZ, ParticleCount));
    return true;
}

#endif
