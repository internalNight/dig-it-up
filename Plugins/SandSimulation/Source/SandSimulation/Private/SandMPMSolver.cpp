#include "SandMPMSolver.h"
#include "SandMachineKinematics.h"
#include "SandLunarTerrain.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SandLevelSettings.h"

#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"

namespace Sand::MPM
{
float CouplingStepSeconds() {
    if(!FParse::Param(FCommandLine::Get(),TEXT("SandSynchronous"))) return 1.f/30;
    int32 Hz=60; FParse::Value(FCommandLine::Get(),TEXT("SandCouplingHz="),Hz);
    checkf(Hz==30 || Hz==60 || Hz==120,TEXT("SandCouplingHz must be 30, 60 or 120"));
    return 1.f/Hz;
}
IMPLEMENT_GLOBAL_SHADER(FSandMPMP2GCS, "/SandSimulation/Private/SandElastoplasticMPM.usf", "MPMP2GCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandMPMUpdateGridCS, "/SandSimulation/Private/SandElastoplasticMPM.usf", "MPMUpdateGridCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSandMPMG2PCS, "/SandSimulation/Private/SandElastoplasticMPM.usf", "MPMG2PCS", SF_Compute);

TArray<FParticleData> MakeInitialColumn(
    const float CellSize,
    const int32 ParticlesPerCell,
    const float Density,
    const float FrictionAngleDegrees)
{
    constexpr int32 CellsX = 8;
    constexpr int32 CellsY = 8;
    constexpr int32 CellsZ = 14;
    const FVector3f ColumnMinimum(-0.65f, -0.2f, 0.0f);
    static const FVector3f Offsets[4] =
    {
        FVector3f(0.25f, 0.25f, 0.25f),
        FVector3f(0.75f, 0.75f, 0.25f),
        FVector3f(0.75f, 0.25f, 0.75f),
        FVector3f(0.25f, 0.75f, 0.75f)
    };

    TArray<FParticleData> Particles;
    Particles.Reserve(CellsX * CellsY * CellsZ * ParticlesPerCell);
    const float ParticleVolume = FMath::Pow(CellSize, 3.0f) / ParticlesPerCell;
    const float ParticleMass = Density * ParticleVolume;
    const float K0 = 1.0f - FMath::Sin(FMath::DegreesToRadians(FrictionAngleDegrees));
    const float ColumnHeight = CellsZ * CellSize;

    for (int32 Z = 0; Z < CellsZ; ++Z)
    {
        for (int32 Y = 0; Y < CellsY; ++Y)
        {
            for (int32 X = 0; X < CellsX; ++X)
            {
                for (int32 Point = 0; Point < ParticlesPerCell; ++Point)
                {
                    const FVector3f Position = ColumnMinimum +
                        (FVector3f(X, Y, Z) + Offsets[Point]) * CellSize;
                    const float VerticalPressure = Density * 9.81f * (ColumnHeight - Position.Z);
                    FParticleData Particle;
                    Particle.PositionAndMass = FVector4f(Position, ParticleMass);
                    Particle.VelocityAndVolume = FVector4f(0.0f, 0.0f, 0.0f, ParticleVolume);
                    Particle.StressRow0AndCompaction = FVector4f(-K0 * VerticalPressure, 0.0f, 0.0f, 0.45f);
                    Particle.StressRemainder = FVector4f(-K0 * VerticalPressure, 0.0f, -VerticalPressure, 0.0f);
                    Particle.BucketLocalAndCarried = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
                    Particles.Add(Particle);
                }
            }
        }
    }
    return Particles;
}

TArray<FParticleData> MakeInitialSandbox(
    const float CellSize,
    const float Density,
    const float FrictionAngleDegrees,
    const float SandDepthMeters,
    const float WidthMeters,
    const bool bLunarTerrain)
{
    const int32 CellsX = FMath::RoundToInt(WidthMeters / CellSize);
    const int32 CellsY = FMath::RoundToInt(WidthMeters / CellSize);
    const float MaximumSurfaceMeters = bLunarTerrain ? SandDepthMeters + 0.32f : SandDepthMeters;
    const int32 CellsZ = FMath::CeilToInt(MaximumSurfaceMeters / CellSize);
    const FVector3f Minimum(-0.5f * WidthMeters, -0.5f * WidthMeters, 0.0f);
    const float ParticleVolume = FMath::Pow(CellSize, 3.0f);
    const float ParticleMass = Density * ParticleVolume;
    const float K0 = 1.0f - FMath::Sin(FMath::DegreesToRadians(FrictionAngleDegrees));

    TArray<FParticleData> Particles;
    Particles.Reserve(CellsX * CellsY * CellsZ);
    for (int32 Z = 0; Z < CellsZ; ++Z)
    {
        for (int32 Y = 0; Y < CellsY; ++Y)
        {
            for (int32 X = 0; X < CellsX; ++X)
            {
                const FVector3f Position = Minimum +
                    (FVector3f(X, Y, Z) + FVector3f(0.5f)) * CellSize;
                const float SurfaceMeters = bLunarTerrain
                    ? Sand::Lunar::ActiveSurfaceHeightMeters(Position.X, Position.Y, SandDepthMeters, WidthMeters)
                    : SandDepthMeters;
                if (Position.Z >= SurfaceMeters)
                {
                    continue;
                }
                const float VerticalPressure = Density * 9.81f * (SurfaceMeters - Position.Z);
                FParticleData Particle;
                Particle.PositionAndMass = FVector4f(Position, ParticleMass);
                Particle.VelocityAndVolume = FVector4f(0.0f, 0.0f, 0.0f, ParticleVolume);
                Particle.StressRow0AndCompaction = FVector4f(
                    -K0 * VerticalPressure, 0.0f, 0.0f, 0.45f);
                Particle.StressRemainder = FVector4f(
                    -K0 * VerticalPressure, 0.0f, -VerticalPressure, 0.0f);
                Particle.BucketLocalAndCarried = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
                Particles.Add(Particle);
            }
        }
    }
    return Particles;
}

BEGIN_SHADER_PARAMETER_STRUCT(FCollapseReadbackParameters, )
    RDG_BUFFER_ACCESS(SourceBuffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FRuntimeReadbackParameters, )
    RDG_BUFFER_ACCESS(ParticleSource, ERHIAccess::CopySrc)
    RDG_BUFFER_ACCESS(ToolImpulseSource, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

void EnqueueValidatedColumnCollapse(
    const FSandMaterialParameters& Material,
    FCollapseFramesCallback&& Completion)
{
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
    TArray<FParticleData> InitialParticles = MakeInitialColumn(
        CellSize,
        4,
        Material.BulkDensityKgPerM3,
        Material.InternalFrictionAngleDegrees);
    TSharedRef<TArray<TArray<FParticleData>>, ESPMode::ThreadSafe> Frames =
        MakeShared<TArray<TArray<FParticleData>>, ESPMode::ThreadSafe>();
    Frames->SetNum(TotalSteps / SnapshotStride + 1);
    (*Frames)[0] = InitialParticles;

    ENQUEUE_RENDER_COMMAND(SandRuntimeColumnCollapse)(
        [InitialParticles = MoveTemp(InitialParticles), Frames, PhysicalMinimum, PhysicalMaximum,
            GridOrigin, Material, Completion = MoveTemp(Completion)](FRHICommandListImmediate& RHICmdList) mutable
        {
            if (RHICmdList.GetPipeline() == ERHIPipeline::None)
            {
                RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef ParticleA = CreateStructuredBuffer(
                GraphBuilder, TEXT("Sand.RuntimeCollapse.ParticlesA"), InitialParticles);
            FRDGBufferRef ParticleB = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(FParticleData), InitialParticles.Num()),
                TEXT("Sand.RuntimeCollapse.ParticlesB"));
            FRDGBufferRef GridBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GridScalarCount),
                TEXT("Sand.RuntimeCollapse.Grid"));
            FRDGBufferRef DisabledToolImpulseBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 6 + 64*6),
                TEXT("Sand.RuntimeCollapse.DisabledToolImpulse"));
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
                    RDG_EVENT_NAME("Sand.RuntimeMPM.P2G"),
                    TShaderMapRef<FSandMPMP2GCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    P2G,
                    FComputeShaderUtils::GetGroupCount(InitialParticles.Num(), 64));

                FSandMPMUpdateGridCS::FParameters* Update =
                    GraphBuilder.AllocParameters<FSandMPMUpdateGridCS::FParameters>();
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
                    RDG_EVENT_NAME("Sand.RuntimeMPM.UpdateGrid"),
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
                const float SinPhi = FMath::Sin(FMath::DegreesToRadians(Material.InternalFrictionAngleDegrees));
                G2P->MPMFrictionSlope = 6.0f * SinPhi / (3.0f - SinPhi);
                G2P->MPMCohesionInterceptPa = Material.GetDruckerPragerCohesionInterceptPa();
                G2P->MPMHardeningRate = Material.HardeningRate;
                G2P->MPMObjectiveMaterial = Material.bObjectiveMaterial;
                const float SinPsi=FMath::Sin(FMath::DegreesToRadians(Material.DilationAngleDegrees));
                G2P->MPMDilationSlope=6*SinPsi/(3-SinPsi);
                G2P->MPMVelocityDampingPerSecond = Material.VelocityDampingPerSecond;
                G2P->MPMBoundaryFriction = Material.ToolFrictionCoefficient;
                G2P->MPMGridOriginMeters = GridOrigin;
                G2P->MPMPhysicalDomainMinimumMeters = PhysicalMinimum;
                G2P->MPMPhysicalDomainMaximumMeters = PhysicalMaximum;
                // G2P advects positions to the end of this substep. Project
                // against that same end-time solid, not the grid midpoint pose.
                G2P->MPMToolColliderCount = 0u;
                G2P->MPMBucketInteriorEnabled = 0u;
                G2P->MPMParticlesIn = GraphBuilder.CreateSRV(CurrentParticles);
                G2P->MPMGridScalarsIn = GraphBuilder.CreateSRV(GridBuffer);
                G2P->MPMToolImpulseScalars = GraphBuilder.CreateUAV(DisabledToolImpulseBuffer);
                G2P->MPMParticlesOut = GraphBuilder.CreateUAV(NextParticles);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.RuntimeMPM.G2P"),
                    TShaderMapRef<FSandMPMG2PCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    G2P,
                    FComputeShaderUtils::GetGroupCount(InitialParticles.Num(), 64));

                Swap(CurrentParticles, NextParticles);
                if (Step % SnapshotStride == 0)
                {
                    FCollapseReadbackParameters* ReadbackParameters =
                        GraphBuilder.AllocParameters<FCollapseReadbackParameters>();
                    ReadbackParameters->SourceBuffer = CurrentParticles;
                    const uint32 ThisSnapshot = SnapshotIndex++;
                    GraphBuilder.AddPass(
                        RDG_EVENT_NAME("Sand.RuntimeMPM.Snapshot"),
                        ReadbackParameters,
                        ERDGPassFlags::Readback,
                        [Frames, ReadbackParameters, ThisSnapshot](FRHICommandListImmediate& ReadbackCommandList)
                        {
                            const uint32 NumBytes = (*Frames)[0].Num() * sizeof(FParticleData);
                            (*Frames)[ThisSnapshot].SetNumUninitialized((*Frames)[0].Num());
                            FRHIGPUBufferReadback Readback(TEXT("Sand.RuntimeMPM.Snapshot"));
                            Readback.EnqueueCopy(
                                ReadbackCommandList, ReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                            ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                            const void* SourceData = Readback.Lock(NumBytes);
                            FMemory::Memcpy((*Frames)[ThisSnapshot].GetData(), SourceData, NumBytes);
                            Readback.Unlock();
                        });
                }
            }

            GraphBuilder.Execute();
            AsyncTask(ENamedThreads::GameThread,
                [Frames, Completion = MoveTemp(Completion)]() mutable
                {
                    Completion(MoveTemp(*Frames), SnapshotStride * DeltaTime);
                });
        });
}

TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> CreateRuntimeColumnSimulation(
    const FSandMaterialParameters& Material)
{
    TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> State =
        MakeShared<FRuntimeSimulationState, ESPMode::ThreadSafe>();
    State->Material = Material;
    State->InitialParticles = MakeInitialColumn(
        State->CellSize,
        4,
        Material.BulkDensityKgPerM3,
        Material.InternalFrictionAngleDegrees);
    return State;
}

TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> CreateRuntimeSandboxSimulation(
    const FSandMaterialParameters& Material)
{
    TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> State =
        MakeShared<FRuntimeSimulationState, ESPMode::ThreadSafe>();
    State->Material = Material;
    State->CellSize = 0.05f;
    FString Quality;
    FParse::Value(FCommandLine::Get(), TEXT("SandQuality="), Quality);
#if PLATFORM_ANDROID
    if (Quality.IsEmpty()) Quality = TEXT("Mobile");
#endif
    if (Quality == TEXT("Legacy")) State->CellSize = 0.0625f;
    if (Quality == TEXT("Mobile")) State->CellSize = 0.10f;
    if (Quality == TEXT("Fine")) State->CellSize = 0.03125f;
    if (Quality == TEXT("Ultra")) State->CellSize = 0.025f;
    const bool bBench = FParse::Param(FCommandLine::Get(), TEXT("SandRoadheaderBench"));
    const bool bLegacyAcceptance =
        FParse::Param(FCommandLine::Get(),TEXT("SandVictoryTest")) ||
        FParse::Param(FCommandLine::Get(),TEXT("SandBoundaryTest")) ||
        FParse::Param(FCommandLine::Get(),TEXT("SandBoomRaiseTest")) ||
        FParse::Param(FCommandLine::Get(),TEXT("SandSlopeCoastTest"));
    const USandLevelSettings* LevelSettings = GetDefault<USandLevelSettings>();
    const bool bLunarWorld = LevelSettings->bLunarWorld &&
        !FParse::Param(FCommandLine::Get(), TEXT("SandLegacyBox")) && !bBench && !bLegacyAcceptance;
    if (bLunarWorld && Quality.IsEmpty())
    {
        // Four times the playable area at a bounded point count. The current
        // 8 GB target cannot keep the old 5 cm grid over the enlarged volume.
        State->CellSize = 0.0625f;
    }
    if (bBench && Quality.IsEmpty()) State->CellSize = 0.025f;
    State->InternalDeltaSeconds = 1.0f / (State->CellSize <= 0.025f ? 1200.0f : State->CellSize <= 0.03125f ? 900.0f : State->CellSize <= 0.05f ? 600.0f : 300.0f);
    if (Quality == TEXT("Mobile"))
    {
        // Eight internal steps per 30 Hz coupling frame save two full
        // particle/grid passes while retaining the same physical outer step.
        State->InternalDeltaSeconds = 1.0f / 240.0f;
    }
    int32 InternalHz = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("SandInternalHz="), InternalHz) && InternalHz > 0)
    {
        State->InternalDeltaSeconds = 1.0f / FMath::Clamp(InternalHz, 120, 1200);
    }
    int32 SubstepScale=1; FParse::Value(FCommandLine::Get(),TEXT("SandSubsteps="),SubstepScale);
    State->InternalDeltaSeconds/=FMath::Clamp(SubstepScale,1,4);
    State->InternalDeltaSeconds=FittedInternalStep(CouplingStepSeconds(),State->InternalDeltaSeconds);
    UE_LOG(LogTemp,Display,TEXT("TIME_GRID outerSeconds=%.9f internalSeconds=%.9f substeps=%d"),CouplingStepSeconds(),State->InternalDeltaSeconds,FMath::RoundToInt(CouplingStepSeconds()/State->InternalDeltaSeconds));
    const float Width = bLunarWorld ? LevelSettings->ActiveWidthMeters : 5.0f;
    State->PhysicalMinimum = FVector3f(-0.5f * Width, -0.5f * Width, 0.0f);
    const float Depth = bLunarWorld ? LevelSettings->SandDepthMeters : 1.5f;
    const float MaximumSurface = bLunarWorld ? Depth + 0.32f : Depth;
    State->PhysicalMaximum = FVector3f(0.5f * Width, 0.5f * Width, MaximumSurface + 1.0f);
    State->GridOrigin = State->PhysicalMinimum - FVector3f(State->CellSize);
    State->GridSize = FIntVector(FMath::CeilToInt(Width/State->CellSize)+3,
        FMath::CeilToInt(Width/State->CellSize)+3,
        FMath::CeilToInt((MaximumSurface+1.0f)/State->CellSize)+3);
    if(!bBench) State->InitialParticles = MakeInitialSandbox(
        State->CellSize,
        Material.BulkDensityKgPerM3,
        Material.InternalFrictionAngleDegrees, Depth, Width, bLunarWorld);
    if (bBench)
    {
        State->PhysicalMinimum = FVector3f(-1.2f,-0.6f,0);
        State->PhysicalMaximum = FVector3f(1.2f,0.6f,1.1f);
        State->GridOrigin = State->PhysicalMinimum-FVector3f(State->CellSize);
        const FVector3f Size = (State->PhysicalMaximum-State->PhysicalMinimum)/State->CellSize;
        State->GridSize = FIntVector(FMath::CeilToInt(Size.X)+3,FMath::CeilToInt(Size.Y)+3,FMath::CeilToInt(Size.Z)+3);
        State->InitialParticles.Reset();
        const float H=State->CellSize;
        for(float Z=0.3f+H/2; Z<0.65f; Z+=H)
        for(float Y=-0.2f+H/2; Y<0.2f; Y+=H)
        for(float X=0.65f+H/2; X<1.05f; X+=H)
        {
            FParticleData P;
            FMemory::Memzero(P);
            P.PositionAndMass=FVector4f(X,Y,Z,Material.BulkDensityKgPerM3*H*H*H);
            P.VelocityAndVolume=FVector4f(0,0,0,H*H*H);
            P.StressRow0AndCompaction.W=0.45f;
            State->InitialParticles.Add(P);
        }
    }
    FString SoilCase;
    if(FParse::Value(FCommandLine::Get(),TEXT("SandSoilBench="),SoilCase)) {
        State->PhysicalMinimum=FVector3f(-.4f,-.4f,0);
        State->PhysicalMaximum=FVector3f(1.4f,.4f,.9f);
        State->GridOrigin=State->PhysicalMinimum-FVector3f(State->CellSize);
        const FVector3f N=(State->PhysicalMaximum-State->PhysicalMinimum)/State->CellSize;
        State->GridSize=FIntVector(FMath::CeilToInt(N.X)+3,FMath::CeilToInt(N.Y)+3,FMath::CeilToInt(N.Z)+3);
        State->InitialParticles.Reset();
        const float H=State->CellSize;
        const bool Collapse=SoilCase==TEXT("Collapse");
        const float XMin=Collapse?.2f:-.4f, XMax=Collapse?.9f:1.4f;
        const float YMin=Collapse?-.2f:-.4f, YMax=Collapse?.2f:.4f;
        for(float Z0=0;Z0<.4f-1.e-5f;Z0+=H) for(float Y0=YMin;Y0<YMax-1.e-5f;Y0+=H) for(float X0=XMin;X0<XMax-1.e-5f;X0+=H) {
            const float DX=FMath::Min(H,XMax-X0),DY=FMath::Min(H,YMax-Y0),DZ=FMath::Min(H,.4f-Z0);
            const float X=X0+DX/2,Y=Y0+DY/2,Z=Z0+DZ/2,Volume=DX*DY*DZ;
            FParticleData P; FMemory::Memzero(P);
            P.PositionAndMass=FVector4f(X,Y,Z,Material.BulkDensityKgPerM3*Volume);
            P.VelocityAndVolume=FVector4f(0,0,0,Volume);
            const float S=-Material.BulkDensityKgPerM3*9.81f*(.4f-Z);
            const float K=1-FMath::Sin(FMath::DegreesToRadians(Material.InternalFrictionAngleDegrees));
            P.StressRow0AndCompaction=FVector4f(K*S,0,0,Material.InitialRelativeCompaction);
            P.StressRemainder=FVector4f(K*S,0,S,0);
            State->InitialParticles.Add(P);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("MPM quality: cell %.5f m, dt %.6f s, particles %d, grid %d x %d x %d"),
        State->CellSize,State->InternalDeltaSeconds,State->InitialParticles.Num(),State->GridSize.X,State->GridSize.Y,State->GridSize.Z);
    return State;
}

FToolOrientedBoxState SampleMachineCollider(const FToolOrientedBoxState& C, float Time)
{
    auto R=C;
    if (C.Motion==0) { R.CenterMeters += C.LinearVelocityMetersPerSecond*Time; return R; }
    if(C.Motion==4) {
        // An endless belt has a stationary geometric envelope and moving skin.
        // Only chassis translation moves its finite contact envelope.
        R.CenterMeters+=C.BaseVelocity*Time;
        R.LinearVelocityMetersPerSecond=C.BaseVelocity+C.MotionRotation.RotateVector(FVector3f(C.Speed,0,0));
        return R;
    }
    FVector3f P, X, Y(0,1,0), Z;
    if(C.Motion==3)
    {
        const FQuat4f Spin(C.RotorAxis,C.Phase+C.Speed*Time);
        const FVector3f Offset=Spin.RotateVector(C.RotorOffset);
        const FQuat4f Orientation=Spin*C.RotorOrientation;
        P=C.RotorCenter+Offset;
        X=Orientation.GetAxisX(); Y=Orientation.GetAxisY(); Z=Orientation.GetAxisZ();
        R.AngularVelocityRadiansPerSecond=C.MotionRotation.RotateVector(C.RotorAxis*C.Speed);
        R.LinearVelocityMetersPerSecond=C.MotionRotation.RotateVector(FVector3f::CrossProduct(C.RotorAxis*C.Speed,Offset));
    }
    else if(C.Motion==1)
    {
        const float A=C.Phase+C.Speed*Time;
        X=FVector3f(FMath::Cos(A),0,-FMath::Sin(A));
        Z=FVector3f::CrossProduct(X,Y);
        P=C.RotorCenter+C.OrbitRadius*X;
        R.AngularVelocityRadiansPerSecond=C.MotionRotation.RotateVector(Y*C.Speed);
        R.LinearVelocityMetersPerSecond=C.MotionRotation.RotateVector(FVector3f::CrossProduct(Y*C.Speed,C.OrbitRadius*X));
    }
    else
    {
        Sand::Machine::ChainPose(C.Phase+C.Speed*Time,P,X,C.ChainFront);
        Z=FVector3f::CrossProduct(X,Y);
        R.LinearVelocityMetersPerSecond=C.MotionRotation.RotateVector(X*C.Speed);
        // Angular velocity is nonzero only on the semicircular sprockets.
        const float PathRun=C.ChainFront-Sand::Machine::Rear, PathLoop=2*PathRun+2*PI*Sand::Machine::ChainRadius;
        float S=FMath::Fmod(C.Phase+C.Speed*Time,PathLoop);
        if(S<0) S+=PathLoop;
        bool Turn=(S>PathRun && S<PathRun+PI*Sand::Machine::ChainRadius) || S>2*PathRun+PI*Sand::Machine::ChainRadius;
        R.AngularVelocityRadiansPerSecond=C.MotionRotation.RotateVector(Y*(Turn ? -C.Speed/Sand::Machine::ChainRadius : 0));
        const FVector3f Offset=-C.SurfaceOffset*Z;
        P+=Offset;
        R.LinearVelocityMetersPerSecond+=FVector3f::CrossProduct(R.AngularVelocityRadiansPerSecond,C.MotionRotation.RotateVector(Offset));
    }
    R.CenterMeters=C.MotionOrigin+C.MotionRotation.RotateVector(P)+C.BaseVelocity*Time;
    R.LinearVelocityMetersPerSecond+=C.BaseVelocity;
    R.AxisX=C.MotionRotation.RotateVector(X);
    R.AxisY=C.MotionRotation.RotateVector(Y);
    R.AxisZ=C.MotionRotation.RotateVector(Z);
    return R;
}

void EnqueueRuntimeSimulationSteps(
    TSharedRef<FRuntimeSimulationState, ESPMode::ThreadSafe> State,
    const uint32 InternalStepCount,
    const bool bReadbackParticles,
    const FToolColliderState& Tool,
    FRuntimeStepCallback&& Completion)
{
    // The optional coupled path completes one GPU step before the next world
    // step. Wall-clock slowdown cannot advance Chaos ahead of the sand.
    const bool Synchronous=FParse::Param(FCommandLine::Get(),TEXT("SandSynchronous"));
    auto Finish=MakeShared<TUniqueFunction<void()>,ESPMode::ThreadSafe>();
    ENQUEUE_RENDER_COMMAND(SandRuntimePersistentStep)(
        [State, InternalStepCount, bReadbackParticles, Tool, Synchronous, Finish, Completion = MoveTemp(Completion)](
            FRHICommandListImmediate& RHICmdList) mutable
        {
            const double StartSeconds = FPlatformTime::Seconds();
            if (RHICmdList.GetPipeline() == ERHIPipeline::None)
            {
                RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef CurrentParticles = State->bInitialized
                ? GraphBuilder.RegisterExternalBuffer(State->ParticleBuffer, TEXT("Sand.Runtime.PersistentParticles"))
                : CreateStructuredBuffer(
                    GraphBuilder, TEXT("Sand.Runtime.InitialParticles"), State->InitialParticles);
            FRDGBufferRef NextParticles = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(FParticleData), State->InitialParticles.Num()),
                TEXT("Sand.Runtime.ScratchParticles"));
            const uint32 GridNodeCount =
                State->GridSize.X * State->GridSize.Y * State->GridSize.Z;
            FRDGBufferRef GridBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4 * GridNodeCount),
                TEXT("Sand.Runtime.Grid"));
            FRDGBufferRef ToolImpulseBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 6 + 64*6),
                TEXT("Sand.Runtime.ToolImpulse"));
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ToolImpulseBuffer), 0u);

            for (uint32 Step = 0; Step < FMath::Max(1u, InternalStepCount); ++Step)
            {
                AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GridBuffer), 0u);

                FSandMPMP2GCS::FParameters* P2G = GraphBuilder.AllocParameters<FSandMPMP2GCS::FParameters>();
                P2G->MPMParticleCount = State->InitialParticles.Num();
                P2G->MPMGridSizeX = State->GridSize.X;
                P2G->MPMGridSizeY = State->GridSize.Y;
                P2G->MPMGridSizeZ = State->GridSize.Z;
                P2G->MPMCellSizeMeters = State->CellSize;
                P2G->MPMDeltaTimeSeconds = State->InternalDeltaSeconds;
                P2G->MPMGridOriginMeters = State->GridOrigin;
                P2G->MPMParticlesIn = GraphBuilder.CreateSRV(CurrentParticles);
                P2G->MPMGridScalars = GraphBuilder.CreateUAV(GridBuffer);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.PersistentMPM.P2G"),
                    TShaderMapRef<FSandMPMP2GCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    P2G,
                    FComputeShaderUtils::GetGroupCount(State->InitialParticles.Num(), 64));

                FSandMPMUpdateGridCS::FParameters* Update =
                    GraphBuilder.AllocParameters<FSandMPMUpdateGridCS::FParameters>();
                Update->MPMGridSizeX = State->GridSize.X;
                Update->MPMGridSizeY = State->GridSize.Y;
                Update->MPMGridSizeZ = State->GridSize.Z;
                Update->MPMCellSizeMeters = State->CellSize;
                Update->MPMDeltaTimeSeconds = State->InternalDeltaSeconds;
                Update->MPMGravityMetersPerSecondSquared = -9.81f;
                Update->MPMBoundaryFriction = State->Material.ToolFrictionCoefficient;
                Update->MPMGridOriginMeters = State->GridOrigin;
                Update->MPMPhysicalDomainMinimumMeters = State->PhysicalMinimum;
                Update->MPMPhysicalDomainMaximumMeters = State->PhysicalMaximum;
                Update->MPMToolColliderCount = FMath::Min(
                    Tool.ColliderCount, FToolColliderState::MaxColliderCount);
                for (uint32 ColliderIndex = 0; ColliderIndex < FToolColliderState::MaxColliderCount;
                    ++ColliderIndex)
                {
                    const FToolOrientedBoxState Collider = SampleMachineCollider(Tool.Colliders[ColliderIndex], (Step+1.0f)*State->InternalDeltaSeconds);
                    Update->MPMToolCentersMeters[ColliderIndex] = FVector4f(Collider.CenterMeters, Collider.SeparationSpeedLimit);
                    Update->MPMToolAxesX[ColliderIndex] = FVector4f(Collider.AxisX, Collider.FrictionCoefficient);
                    Update->MPMToolAxesY[ColliderIndex] = FVector4f(Collider.AxisY, 0.0f);
                    Update->MPMToolAxesZ[ColliderIndex] = FVector4f(Collider.AxisZ, 0.0f);
                    Update->MPMToolHalfExtentsMeters[ColliderIndex] = FVector4f(
                        Collider.HalfExtentsMeters, (float)Collider.Shape);
                    Update->MPMToolLinearVelocitiesMetersPerSecond[ColliderIndex] = FVector4f(
                        Collider.LinearVelocityMetersPerSecond, 0.0f);
                    Update->MPMToolAngularVelocitiesRadiansPerSecond[ColliderIndex] = FVector4f(
                        Collider.AngularVelocityRadiansPerSecond, 0.0f);
                }
                Update->MPMGridScalars = GraphBuilder.CreateUAV(GridBuffer);
                Update->MPMToolImpulseScalars = GraphBuilder.CreateUAV(ToolImpulseBuffer);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.PersistentMPM.UpdateGrid"),
                    TShaderMapRef<FSandMPMUpdateGridCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    Update,
                    FComputeShaderUtils::GetGroupCount(GridNodeCount, 64));

                FSandMPMG2PCS::FParameters* G2P = GraphBuilder.AllocParameters<FSandMPMG2PCS::FParameters>();
                G2P->MPMParticleCount = State->InitialParticles.Num();
                G2P->MPMGridSizeX = State->GridSize.X;
                G2P->MPMGridSizeY = State->GridSize.Y;
                G2P->MPMGridSizeZ = State->GridSize.Z;
                G2P->MPMCellSizeMeters = State->CellSize;
                G2P->MPMDeltaTimeSeconds = State->InternalDeltaSeconds;
                G2P->MPMLameMuPa = State->Material.GetLameMuPa();
                G2P->MPMLameLambdaPa = State->Material.GetLameLambdaPa();
                const float SinPhi = FMath::Sin(
                    FMath::DegreesToRadians(State->Material.InternalFrictionAngleDegrees));
                G2P->MPMFrictionSlope = 6.0f * SinPhi / (3.0f - SinPhi);
                G2P->MPMCohesionInterceptPa = State->Material.GetDruckerPragerCohesionInterceptPa();
                G2P->MPMHardeningRate = State->Material.HardeningRate;
                G2P->MPMObjectiveMaterial=State->Material.bObjectiveMaterial;
                const float SinPsi=FMath::Sin(FMath::DegreesToRadians(State->Material.DilationAngleDegrees));
                G2P->MPMDilationSlope=6*SinPsi/(3-SinPsi);
                G2P->MPMVelocityDampingPerSecond = State->Material.VelocityDampingPerSecond;
                G2P->MPMBoundaryFriction = State->Material.ToolFrictionCoefficient;
                G2P->MPMGridOriginMeters = State->GridOrigin;
                G2P->MPMPhysicalDomainMinimumMeters = State->PhysicalMinimum;
                G2P->MPMPhysicalDomainMaximumMeters = State->PhysicalMaximum;
                G2P->MPMToolColliderCount = FMath::Min(
                    Tool.ColliderCount, FToolColliderState::MaxColliderCount);
                for (uint32 ColliderIndex = 0; ColliderIndex < FToolColliderState::MaxColliderCount;
                    ++ColliderIndex)
                {
                    const FToolOrientedBoxState Collider = SampleMachineCollider(Tool.Colliders[ColliderIndex], (Step+0.5f)*State->InternalDeltaSeconds);
                    G2P->MPMToolCentersMeters[ColliderIndex] = FVector4f(Collider.CenterMeters, Collider.SeparationSpeedLimit);
                    G2P->MPMToolAxesX[ColliderIndex] = FVector4f(Collider.AxisX, Collider.FrictionCoefficient);
                    G2P->MPMToolAxesY[ColliderIndex] = FVector4f(Collider.AxisY, 0.0f);
                    G2P->MPMToolAxesZ[ColliderIndex] = FVector4f(Collider.AxisZ, 0.0f);
                    G2P->MPMToolHalfExtentsMeters[ColliderIndex] = FVector4f(
                        Collider.HalfExtentsMeters, (float)Collider.Shape);
                    G2P->MPMToolLinearVelocitiesMetersPerSecond[ColliderIndex] = FVector4f(
                        Collider.LinearVelocityMetersPerSecond, 0.0f);
                    G2P->MPMToolAngularVelocitiesRadiansPerSecond[ColliderIndex] = FVector4f(
                        Collider.AngularVelocityRadiansPerSecond, 0.0f);
                }
                G2P->MPMBucketInteriorEnabled = Tool.bBucketInteriorEnabled ? 1u : 0u;
                G2P->MPMBucketInteriorDampingPerSecond = Tool.BucketInteriorDampingPerSecond;
                G2P->MPMBucketInteriorCenterMeters = Tool.BucketInterior.CenterMeters;
                G2P->MPMBucketInteriorAxisX = Tool.BucketInterior.AxisX;
                G2P->MPMBucketInteriorAxisY = Tool.BucketInterior.AxisY;
                G2P->MPMBucketInteriorAxisZ = Tool.BucketInterior.AxisZ;
                G2P->MPMBucketInteriorHalfExtentsMeters = Tool.BucketInterior.HalfExtentsMeters;
                G2P->MPMBucketInteriorLinearVelocityMetersPerSecond =
                    Tool.BucketInterior.LinearVelocityMetersPerSecond;
                G2P->MPMBucketInteriorAngularVelocityRadiansPerSecond =
                    Tool.BucketInterior.AngularVelocityRadiansPerSecond;
                G2P->MPMParticlesIn = GraphBuilder.CreateSRV(CurrentParticles);
                G2P->MPMGridScalarsIn = GraphBuilder.CreateSRV(GridBuffer);
                G2P->MPMToolImpulseScalars = GraphBuilder.CreateUAV(ToolImpulseBuffer);
                G2P->MPMParticlesOut = GraphBuilder.CreateUAV(NextParticles);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("Sand.PersistentMPM.G2P"),
                    TShaderMapRef<FSandMPMG2PCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
                    G2P,
                    FComputeShaderUtils::GetGroupCount(State->InitialParticles.Num(), 64));
                Swap(CurrentParticles, NextParticles);
            }

            TSharedRef<TArray<FParticleData>, ESPMode::ThreadSafe> ReadbackParticles =
                MakeShared<TArray<FParticleData>, ESPMode::ThreadSafe>();
            TSharedRef<FToolInteractionResult, ESPMode::ThreadSafe> ToolInteraction =
                MakeShared<FToolInteractionResult, ESPMode::ThreadSafe>();
            {
                // Most 30 Hz simulation frames only need the tiny tool-impulse readback.
                // Avoid allocating a 16 MB CPU particle array unless this is one of the
                // rate-limited surface frames.
                if (bReadbackParticles)
                {
                    ReadbackParticles->SetNumUninitialized(State->InitialParticles.Num());
                }
                FRuntimeReadbackParameters* ReadbackParameters =
                    GraphBuilder.AllocParameters<FRuntimeReadbackParameters>();
                ReadbackParameters->ParticleSource = CurrentParticles;
                ReadbackParameters->ToolImpulseSource = ToolImpulseBuffer;
                GraphBuilder.AddPass(
                    RDG_EVENT_NAME("Sand.PersistentMPM.Readback"),
                    ReadbackParameters,
                    ERDGPassFlags::Readback,
                    [ReadbackParticles, ToolInteraction, ReadbackParameters, bReadbackParticles](
                        FRHICommandListImmediate& ReadbackCommandList)
                    {
                        const uint32 ParticleBytes = ReadbackParticles->Num() * sizeof(FParticleData);
                        constexpr uint32 ToolBytes = (6 + 64*6) * sizeof(float);
                        FRHIGPUBufferReadback ParticleReadback(TEXT("Sand.PersistentMPM.ParticleReadback"));
                        FRHIGPUBufferReadback ToolReadback(TEXT("Sand.PersistentMPM.ToolReadback"));
                        if (bReadbackParticles)
                        {
                            ParticleReadback.EnqueueCopy(
                                ReadbackCommandList,
                                ReadbackParameters->ParticleSource->GetRHI(),
                                ParticleBytes);
                        }
                        ToolReadback.EnqueueCopy(
                            ReadbackCommandList,
                            ReadbackParameters->ToolImpulseSource->GetRHI(),
                            ToolBytes);
                        ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                        if (bReadbackParticles)
                        {
                            const void* SourceData = ParticleReadback.Lock(ParticleBytes);
                            FMemory::Memcpy(ReadbackParticles->GetData(), SourceData, ParticleBytes);
                            ParticleReadback.Unlock();
                        }
                        else
                        {
                            ReadbackParticles->Reset();
                        }

                        const float* ToolValues = static_cast<const float*>(ToolReadback.Lock(ToolBytes));
                        ToolInteraction->SandLinearImpulseKgMetersPerSecond = FVector3f(
                            ToolValues[0], ToolValues[1], ToolValues[2]);
                        ToolInteraction->SandAngularImpulseKgMetersSquaredPerSecond = FVector3f(
                            ToolValues[3], ToolValues[4], ToolValues[5]);
                        for (uint32 I=0; I<64; ++I)
                        {
                            const float* V = ToolValues + 6 + 6*I;
                            ToolInteraction->ColliderLinear[I] = FVector3f(V[0],V[1],V[2]);
                            ToolInteraction->ColliderAngular[I] = FVector3f(V[3],V[4],V[5]);
                        }
                        ToolReadback.Unlock();
                    });
            }

            GraphBuilder.QueueBufferExtraction(CurrentParticles, &State->ParticleBuffer);
            GraphBuilder.Execute();
            State->bInitialized = true;
            const double GpuSeconds = FPlatformTime::Seconds() - StartSeconds;

            auto Invoke=[ReadbackParticles, ToolInteraction, GpuSeconds, Completion = MoveTemp(Completion)]() mutable
                {
                    Completion(MoveTemp(*ReadbackParticles), *ToolInteraction, GpuSeconds);
                };
            if(Synchronous) *Finish=MoveTemp(Invoke);
            else AsyncTask(ENamedThreads::GameThread,MoveTemp(Invoke));
        });
    if(Synchronous) { FlushRenderingCommands(); if(*Finish) (*Finish)(); }
}
}
