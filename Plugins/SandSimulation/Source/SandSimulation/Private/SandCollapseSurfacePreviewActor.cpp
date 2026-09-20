#include "SandCollapseSurfacePreviewActor.h"

#include "SandMPMSolver.h"
#include "SandExcavatorPawn.h"
#include "SandRoadheaderPawn.h"
#include "SandLevelSettings.h"
#include "SandLunarTerrain.h"
#include "SandLunarWorldActor.h"
#include "SandPreviewGameMode.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"

struct FLunarChunkCache
{
    using FParticleData = Sand::MPM::FParticleData;

    FVector2f ActiveCenterMeters = FVector2f::ZeroVector;
    float ActiveWidthMeters = 15.0f;
    float ChunkWidthMeters = 5.0f;
    float PlayableWidthMeters = 100.0f;
    float SandDepthMeters = 1.2f;
    float DensityKgPerM3 = 1550.0f;
    float FrictionAngleDegrees = 40.0f;
    // A 100 m field plus the half-window edge buffer needs fewer than 512
    // five-metre chunks. Keeping all of them preserves the complete session
    // history; GPU residency is still limited to the local 3 x 3 set.
    int32 MaximumCachedChunks = 512;
    int32 ShiftCount = 0;
    TMap<FIntPoint,TArray<FParticleData>> Chunks;
    TArray<FIntPoint> LeastRecentlyUsed;

    int32 ResidentChunksPerAxis() const
    {
        return FMath::Max(1,FMath::RoundToInt(
            ActiveWidthMeters / ChunkWidthMeters));
    }

    float ChunkOriginMeters() const
    {
        return -0.5f * ActiveWidthMeters;
    }

    FIntPoint MinimumResidentChunk(const FVector2f Center) const
    {
        return FIntPoint(
            FMath::RoundToInt(Center.X / ChunkWidthMeters),
            FMath::RoundToInt(Center.Y / ChunkWidthMeters));
    }

    bool IsResidentChunk(const FIntPoint Key, const FVector2f Center) const
    {
        const FIntPoint MinimumKey = MinimumResidentChunk(Center);
        const int32 Count = ResidentChunksPerAxis();
        return Key.X >= MinimumKey.X && Key.X < MinimumKey.X + Count &&
            Key.Y >= MinimumKey.Y && Key.Y < MinimumKey.Y + Count;
    }

    void Touch(const FIntPoint Key)
    {
        LeastRecentlyUsed.RemoveSingle(Key);
        LeastRecentlyUsed.Add(Key);
    }

    void StoreResidentWindow(const TArray<FParticleData>& Particles)
    {
        const FIntPoint MinimumKey = MinimumResidentChunk(ActiveCenterMeters);
        const int32 ChunkCount = ResidentChunksPerAxis();
        const int32 ExpectedParticlesPerChunk =
            FMath::CeilToInt(1.08f * Particles.Num() / FMath::Max(1,ChunkCount*ChunkCount));
        for (int32 Y = 0; Y < ChunkCount; ++Y)
        {
            for (int32 X = 0; X < ChunkCount; ++X)
            {
                const FIntPoint Key = MinimumKey + FIntPoint(X,Y);
                TArray<FParticleData>& Chunk = Chunks.FindOrAdd(Key);
                Chunk.Reset(ExpectedParticlesPerChunk);
                Touch(Key);
            }
        }
        for (const FParticleData& Particle : Particles)
        {
            const FVector3f Position(Particle.PositionAndMass);
            const FIntPoint Key(
                FMath::Clamp(FMath::FloorToInt(
                    (Position.X - ChunkOriginMeters()) / ChunkWidthMeters),
                    MinimumKey.X, MinimumKey.X + ChunkCount - 1),
                FMath::Clamp(FMath::FloorToInt(
                    (Position.Y - ChunkOriginMeters()) / ChunkWidthMeters),
                    MinimumKey.Y, MinimumKey.Y + ChunkCount - 1));
            Chunks.FindChecked(Key).Add(Particle);
        }
    }

    TArray<FParticleData> LoadWindow(const FVector2f NewCenter, const float CellSize)
    {
        TArray<FParticleData> Result;
        const FIntPoint MinimumKey = MinimumResidentChunk(NewCenter);
        const int32 ChunkCount = ResidentChunksPerAxis();
        for (int32 Y = 0; Y < ChunkCount; ++Y)
        {
            for (int32 X = 0; X < ChunkCount; ++X)
            {
                const FIntPoint Key = MinimumKey + FIntPoint(X,Y);
                if (TArray<FParticleData>* Saved = Chunks.Find(Key))
                {
                    TArray<FParticleData> Restored = MoveTemp(*Saved);
                    Chunks.Remove(Key);
                    LeastRecentlyUsed.RemoveSingle(Key);
                    Result.Append(MoveTemp(Restored));
                }
                else
                {
                    const FVector2f ChunkCenter(
                        ChunkOriginMeters() + (Key.X + 0.5f) * ChunkWidthMeters,
                        ChunkOriginMeters() + (Key.Y + 0.5f) * ChunkWidthMeters);
                    Result.Append(Sand::MPM::MakeInitialSandbox(
                        CellSize,DensityKgPerM3,FrictionAngleDegrees,
                        SandDepthMeters,ChunkWidthMeters,true,ChunkCenter,
                        PlayableWidthMeters));
                }
            }
        }
        ActiveCenterMeters = NewCenter;
        ++ShiftCount;
        while (Chunks.Num() > MaximumCachedChunks && !LeastRecentlyUsed.IsEmpty())
        {
            const FIntPoint Oldest = LeastRecentlyUsed[0];
            LeastRecentlyUsed.RemoveAt(0,EAllowShrinking::No);
            Chunks.Remove(Oldest);
        }
        return Result;
    }

    int32 PrefetchWindowEdge(const FVector2f NewCenter, const float CellSize,
        const int32 MaximumChunksToCreate = 1)
    {
        const FIntPoint MinimumKey = MinimumResidentChunk(NewCenter);
        const int32 ChunkCount = ResidentChunksPerAxis();
        int32 Created = 0;
        for (int32 Y = 0; Y < ChunkCount && Created < MaximumChunksToCreate; ++Y)
        {
            for (int32 X = 0; X < ChunkCount && Created < MaximumChunksToCreate; ++X)
            {
                const FIntPoint Key = MinimumKey + FIntPoint(X,Y);
                if (IsResidentChunk(Key,ActiveCenterMeters) || Chunks.Contains(Key))
                {
                    continue;
                }
                const FVector2f ChunkCenter(
                    ChunkOriginMeters() + (Key.X + 0.5f) * ChunkWidthMeters,
                    ChunkOriginMeters() + (Key.Y + 0.5f) * ChunkWidthMeters);
                Chunks.Add(Key,Sand::MPM::MakeInitialSandbox(
                    CellSize,DensityKgPerM3,FrictionAngleDegrees,
                    SandDepthMeters,ChunkWidthMeters,true,ChunkCenter,
                    PlayableWidthMeters));
                Touch(Key);
                ++Created;
            }
        }
        return Created;
    }

    TArray<float> BuildHeightfield(
        const FIntPoint Key,
        const int32 Resolution,
        const float CellSize) const
    {
        TArray<float> Heights;
        Heights.Init(-TNumericLimits<float>::Max(),Resolution * Resolution);
        const float Spacing = ChunkWidthMeters / (Resolution - 1);
        const float MinimumX = ChunkOriginMeters() + Key.X * ChunkWidthMeters;
        const float MinimumY = ChunkOriginMeters() + Key.Y * ChunkWidthMeters;
        if (const TArray<FParticleData>* Chunk = Chunks.Find(Key))
        {
            for (const FParticleData& Particle : *Chunk)
            {
                if (Particle.BucketLocalAndCarried.W > 0.5f)
                {
                    continue;
                }
                const FVector3f Position(Particle.PositionAndMass);
                const int32 X = FMath::Clamp(FMath::RoundToInt(
                    (Position.X - MinimumX) / Spacing),0,Resolution - 1);
                const int32 Y = FMath::Clamp(FMath::RoundToInt(
                    (Position.Y - MinimumY) / Spacing),0,Resolution - 1);
                float& Height = Heights[X + Y * Resolution];
                Height = FMath::Max(Height,Position.Z + 0.42f * CellSize);
            }
        }
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                float& Height = Heights[X + Y * Resolution];
                if (Height <= -TNumericLimits<float>::Max() * 0.5f)
                {
                    const bool bBoundary = X == 0 || Y == 0 ||
                        X == Resolution - 1 || Y == Resolution - 1;
                    Height = bBoundary
                        ? Sand::Lunar::ActiveSurfaceHeightMeters(
                            MinimumX + X * Spacing,MinimumY + Y * Spacing,
                            SandDepthMeters,PlayableWidthMeters)
                        : 0.02f;
                }
            }
        }
        return Heights;
    }
};

ASandCollapseSurfacePreviewActor::ASandCollapseSurfacePreviewActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.0f;
    bBuildOnBeginPlay = false;
    VoxelSizeMeters = 0.030f;
    KernelRadiusMeters = 0.085f;
    IsoDensity = 1.10f;
    RuntimeMaterial.InternalFrictionAngleDegrees = 40.0f;
    RuntimeMaterial.CohesionPa = 250.0f;
    RuntimeMaterial.VelocityDampingPerSecond = 0.65f;
}

ASandCollapseSurfacePreviewActor::~ASandCollapseSurfacePreviewActor() = default;

void ASandCollapseSurfacePreviewActor::BeginPlay()
{
    Super::BeginPlay();

    FSandMaterialParameters Material =
        FParse::Param(FCommandLine::Get(), TEXT("SandLooseBaseline"))
        ? FSandMaterialParameters() : RuntimeMaterial;
    FString Model;
    FParse::Value(FCommandLine::Get(),TEXT("SandMaterial="),Model);
    if(Model==TEXT("DryCorotated")) {
        Material=FSandMaterialParameters();
        Material.bObjectiveMaterial=true;
        Material.InternalFrictionAngleDegrees=35;
        Material.CohesionPa=0;
        Material.DilationAngleDegrees=0;
        Material.HardeningRate=0;
        Material.InitialRelativeCompaction=.5f;
    }
    // Independent sensitivity controls. These are not calibrated soil presets.
    FParse::Value(FCommandLine::Get(),TEXT("SandPhi="),Material.InternalFrictionAngleDegrees);
    FParse::Value(FCommandLine::Get(),TEXT("SandCohesionPa="),Material.CohesionPa);
    FParse::Value(FCommandLine::Get(),TEXT("SandToolMu="),Material.ToolFrictionCoefficient);
    FParse::Value(FCommandLine::Get(),TEXT("SandDamping="),Material.VelocityDampingPerSecond);
    FParse::Value(FCommandLine::Get(),TEXT("SandDilation="),Material.DilationAngleDegrees);
    FString MaterialError;
    if(!FMath::IsFinite(Material.InternalFrictionAngleDegrees) || !FMath::IsFinite(Material.CohesionPa) ||
        !FMath::IsFinite(Material.ToolFrictionCoefficient) || !FMath::IsFinite(Material.VelocityDampingPerSecond) || !Material.IsValid(&MaterialError))
    {
        UE_LOG(LogTemp,Fatal,TEXT("Invalid sand sensitivity parameters: %s"),*MaterialError);
        return;
    }
    UE_LOG(LogTemp,Display,TEXT("SOIL_CONFIG phiDeg=%.3f cohesionPa=%.3f toolMu=%.3f dampingPerSec=%.3f dilationActive=%d dilationDeg=%.3f"),
        Material.InternalFrictionAngleDegrees,Material.CohesionPa,Material.ToolFrictionCoefficient,Material.VelocityDampingPerSecond,Material.bObjectiveMaterial,Material.DilationAngleDegrees);
    SimulationState = Sand::MPM::CreateRuntimeSandboxSimulation(Material);
    const USandLevelSettings* LevelSettings = GetDefault<USandLevelSettings>();
    const float ResidentWidth =
        SimulationState->PhysicalMaximum.X - SimulationState->PhysicalMinimum.X;
    if (LevelSettings->bLunarWorld && ResidentWidth > 8.0f &&
        LevelSettings->LunarPlayableWidthMeters > ResidentWidth + 1.0f)
    {
        LunarChunkCache = MakeShared<FLunarChunkCache>();
        LunarChunkCache->ActiveWidthMeters = ResidentWidth;
        LunarChunkCache->ChunkWidthMeters = 5.0f;
        LunarChunkCache->PlayableWidthMeters = LevelSettings->LunarPlayableWidthMeters;
        LunarChunkCache->SandDepthMeters = LevelSettings->SandDepthMeters;
        LunarChunkCache->DensityKgPerM3 = Material.BulkDensityKgPerM3;
        LunarChunkCache->FrictionAngleDegrees = Material.InternalFrictionAngleDegrees;
        UE_LOG(LogTemp,Display,
            TEXT("LUNAR_STREAM enabled playable=%.1fm resident=%.1fm chunk=%.1fm cacheLimit=%d"),
            LunarChunkCache->PlayableWidthMeters,LunarChunkCache->ActiveWidthMeters,
            LunarChunkCache->ChunkWidthMeters,LunarChunkCache->MaximumCachedChunks);
    }
    SupportHeightGridCellMeters = SimulationState->CellSize;
    SupportHeightGridMinimumMeters = FVector2f(
        SimulationState->PhysicalMinimum.X, SimulationState->PhysicalMinimum.Y);
    SupportHeightGridSize = FIntPoint(
        FMath::CeilToInt((SimulationState->PhysicalMaximum.X - SimulationState->PhysicalMinimum.X) /
            SupportHeightGridCellMeters) + 1,
        FMath::CeilToInt((SimulationState->PhysicalMaximum.Y - SimulationState->PhysicalMinimum.Y) /
            SupportHeightGridCellMeters) + 1);
    SupportHeightGridCentimeters.Init(-TNumericLimits<float>::Max(),
        SupportHeightGridSize.X * SupportHeightGridSize.Y);
    if (SimulationState->PhysicalMaximum.X - SimulationState->PhysicalMinimum.X > 8.0f)
    {
        // The 15 m lunar window uses a 10 cm physical grid by default. A
        // 12 cm display grid keeps async surface work near the former 10 m
        // cost while the physical window grows by 2.25x in plan area.
        VoxelSizeMeters = 0.12f;
        KernelRadiusMeters = 0.18f;
        // The finite MPM volume is visually continued by the transition mesh.
        // Its closed marching-cubes side faces must not cast a square shadow.
        SurfaceMesh->SetCastShadow(false);
    }
    else if (SimulationState->CellSize >= 0.099f)
    {
        // Match the mobile surface kernel to the coarser physical grid.
        VoxelSizeMeters = 0.08f;
        KernelRadiusMeters = 0.14f;
    }
    // Acceptance fixture: a sloping corner excavation with two intact bottom layers.
    // Removed material is stacked in the upper air region, conserving mass.
    if (FParse::Param(FCommandLine::Get(), TEXT("SandVictoryTest")))
    {
        int32 Relocated = 0;
        const float Depth = SimulationState->PhysicalMaximum.X - SimulationState->PhysicalMinimum.X > 8.0f
            ? GetDefault<USandLevelSettings>()->SandDepthMeters : 1.5f;
        for (auto& Particle : SimulationState->InitialParticles)
        {
            const FVector3f P(Particle.PositionAndMass);
            const float Radius = FVector2f(P.X + 1.7f, P.Y + 1.6f).Size();
            const float Ramp = FMath::Clamp(0.125f + FMath::Max(0.0f,Radius-0.45f)*0.65f,0.125f,Depth);
            if (P.Z > Ramp)
            {
                const int32 X = Relocated % 32;
                const int32 Y = (Relocated / 32) % 80;
                const int32 Z = Relocated / (32 * 80);
                Particle.PositionAndMass = FVector4f(
                    0.5f + (X + 0.5f) * 0.0625f, -2.5f + (Y + 0.5f) * 0.0625f,
                    Depth + (Z + 0.5f) * 0.0625f, Particle.PositionAndMass.W);
                Particle.StressRow0AndCompaction = FVector4f(0,0,0,0.45f);
                Particle.StressRemainder = FVector4f(0,0,0,0);
                ++Relocated;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("Victory fixture: relocated %d particles; bottom layer remains intact"), Relocated);
    }
    // Mass-conserving repeatable pit test: lift the cut plug beside the hole.
    // The normal playable sandbox still starts with a completely filled volume.
    if (FParse::Param(FCommandLine::Get(), TEXT("SandPitTest")))
    {
        for (auto& Particle : SimulationState->InitialParticles)
        {
            FVector3f P(Particle.PositionAndMass);
            if (P.X * P.X + P.Y * P.Y < 0.25f * 0.25f && P.Z > 1.55f)
            {
                P.X += 1.45f;
                P.Z += 0.4375f;
                Particle.PositionAndMass = FVector4f(P, Particle.PositionAndMass.W);
                Particle.StressRow0AndCompaction = FVector4f(0, 0, 0, 0.45f);
                Particle.StressRemainder = FVector4f(0, 0, 0, 0);
            }
        }
    }
    // Test-only fixture for end-to-end validation of the lunar objective.
    // It removes only the shallow plug directly above the test-position gem.
    if (FParse::Param(FCommandLine::Get(),TEXT("SandGemTest")))
    {
        const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
        const float Surface = Sand::Lunar::ActiveSurfaceHeightMeters(
            0.0f,0.0f,Settings->SandDepthMeters,
            Settings->LunarPlayableWidthMeters);
        int32 Relocated = 0;
        for (auto& Particle : SimulationState->InitialParticles)
        {
            FVector3f P(Particle.PositionAndMass);
            if (P.X*P.X+P.Y*P.Y < FMath::Square(0.24f) &&
                P.Z > Surface-0.20f)
            {
                P.X += 1.1f;
                P.Z += 0.25f;
                Particle.PositionAndMass = FVector4f(P,Particle.PositionAndMass.W);
                Particle.StressRow0AndCompaction = FVector4f(0,0,0,0.45f);
                Particle.StressRemainder = FVector4f(0,0,0,0);
                ++Relocated;
            }
        }
        UE_LOG(LogTemp,Display,
            TEXT("LUNAR_GEM_TEST excavatedParticles=%d surface=%.3fm"),
            Relocated,Surface);
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench")))
    {
        VoxelSizeMeters=0.02f;
        KernelRadiusMeters=0.0425f;
    }
    ParticleDensityWeight=FMath::Pow(SimulationState->CellSize * (0.085f/0.0625f) / KernelRadiusMeters,3.0f);
    const auto& InitialParticles = SimulationState->InitialParticles;
    UE_LOG(LogTemp, Display, TEXT("Runtime soil: friction %.1f deg, cohesion %.1f Pa, damping %.2f /s; %d particles"),
        Material.InternalFrictionAngleDegrees, Material.CohesionPa,
        Material.VelocityDampingPerSecond, InitialParticles.Num());
    TArray<FVector3f> InitialPositions;
    InitialPositions.Reserve(InitialParticles.Num());
    for (const Sand::MPM::FParticleData& Particle : InitialParticles)
    {
        InitialPositions.Add(FVector3f(Particle.PositionAndMass));
    }
    GenerateSurfaceFromParticlePositions(
        MoveTemp(InitialPositions),
        SimulationState->PhysicalMinimum-FVector3f(.10f),
        SimulationState->PhysicalMaximum+FVector3f(.10f));

}

void ASandCollapseSurfacePreviewActor::OnSurfaceMeshUpdated(
    const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
    if (bHasPendingLunarWindowCenter)
    {
        for (TActorIterator<ASandLunarWorldActor> It(GetWorld()); It; ++It)
        {
            It->SetActiveWindowCenterMeters(PendingLunarWindowCenterMeters);
        }
        UE_LOG(LogTemp,Display,TEXT("LUNAR_WINDOW_VISUAL_COMMIT center=(%.1f,%.1f)m"),
            PendingLunarWindowCenterMeters.X,PendingLunarWindowCenterMeters.Y);
        bHasPendingLunarWindowCenter = false;
    }
    if (Excavator.IsValid())
    {
        Excavator->UpdateVisibleSandSupport(Vertices, Indices);
    }
    if (auto* Mode = Cast<ASandPreviewGameMode>(GetWorld()->GetAuthGameMode()))
    {
        Mode->CheckExposedFloor(Vertices, Indices);
    }
}

bool ASandCollapseSurfacePreviewActor::SampleSandSurfaceHeightCentimeters(
    const FVector2D& WorldPositionCentimeters,
    const float RadiusCentimeters,
    float& OutHeightCentimeters) const
{
    if (SupportHeightGridCentimeters.IsEmpty() || SupportHeightGridSize.X <= 0 ||
        SupportHeightGridSize.Y <= 0)
    {
        return false;
    }
    const FVector2f PositionMeters(WorldPositionCentimeters / 100.0f);
    const FVector2f GridPosition =
        (PositionMeters - SupportHeightGridMinimumMeters) / SupportHeightGridCellMeters;
    const int32 RadiusCells = FMath::Max(1,
        FMath::CeilToInt((RadiusCentimeters / 100.0f) / SupportHeightGridCellMeters));
    const int32 CenterX = FMath::RoundToInt(GridPosition.X);
    const int32 CenterY = FMath::RoundToInt(GridPosition.Y);
    TArray<float, TInlineAllocator<128>> Heights;
    for (int32 Y = CenterY - RadiusCells; Y <= CenterY + RadiusCells; ++Y)
    {
        for (int32 X = CenterX - RadiusCells; X <= CenterX + RadiusCells; ++X)
        {
            if (X < 0 || Y < 0 || X >= SupportHeightGridSize.X || Y >= SupportHeightGridSize.Y)
            {
                continue;
            }
            const FVector2f CellPosition = SupportHeightGridMinimumMeters +
                FVector2f(X, Y) * SupportHeightGridCellMeters;
            if ((CellPosition - PositionMeters).SquaredLength() >
                FMath::Square(RadiusCentimeters / 100.0f + 0.5f * SupportHeightGridCellMeters))
            {
                continue;
            }
            const float Height = SupportHeightGridCentimeters[X + Y * SupportHeightGridSize.X];
            if (Height > -TNumericLimits<float>::Max() * 0.5f)
            {
                Heights.Add(Height);
            }
        }
    }
    if (Heights.IsEmpty())
    {
        return false;
    }
    Heights.Sort();
    // A high percentile follows the upper bearing envelope while rejecting a
    // lone airborne particle that would otherwise kick a rigid rock upward.
    OutHeightCentimeters = Heights[FMath::Clamp(
        FMath::FloorToInt(0.75f * (Heights.Num() - 1)), 0, Heights.Num() - 1)];
    return true;
}

void ASandCollapseSurfacePreviewActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(auto* Mode=Cast<ASandPreviewGameMode>(GetWorld()->GetAuthGameMode()))
        if(Mode->IsSelectingVehicle()) return;
    ToolSampleElapsedSeconds += DeltaSeconds;
    if (!SimulationState.IsValid())
    {
        return;
    }

    const float FixedFrameSeconds = Sand::MPM::CouplingStepSeconds();
    // A mobile GPU job can span multiple render frames. Count that wall time
    // even while the previous step is in flight; otherwise the sand runs in
    // slow motion despite a responsive camera and touch interface.
    SimulationAccumulatorSeconds = FMath::Min(
        SimulationAccumulatorSeconds + DeltaSeconds * SimulationSpeed,
        4.0f * FixedFrameSeconds);
    if (bSimulationStepInFlight)
    {
        return;
    }
    if (SimulationAccumulatorSeconds < FixedFrameSeconds)
    {
        return;
    }
    const uint32 FramesToAdvance = static_cast<uint32>(FMath::Clamp(
        FMath::FloorToInt(SimulationAccumulatorSeconds / FixedFrameSeconds), 1,
            Cast<ASandRoadheaderPawn>(Excavator.Get()) || FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench")) ? 1 : 2));
    SimulationAccumulatorSeconds -= FramesToAdvance * FixedFrameSeconds;
    bSimulationStepInFlight = true;

    if (!Excavator.IsValid())
    {
        TActorIterator<ASandExcavatorPawn> It(GetWorld());
        if (It)
        {
            Excavator = *It;
        }
    }
    Sand::MPM::FToolColliderState Tool;
    auto* Roadheader=Cast<ASandRoadheaderPawn>(Excavator.Get());
    if (Roadheader) { Roadheader->BuildPhysicalTool(Tool,0); }
    else if (Excavator.IsValid())
    {
        const FVector3f ChassisLinearVelocity(
            Excavator->ChassisBody->GetPhysicsLinearVelocity() / 100.0);
        const FVector3f ChassisAngularVelocity(
            Excavator->ChassisBody->GetPhysicsAngularVelocityInRadians());
        const auto AddTrack = [&Tool, ChassisLinearVelocity, ChassisAngularVelocity](
            const UBoxComponent* Track)
        {
            const FTransform TrackTransform = Track->GetComponentTransform();
            Sand::MPM::FToolOrientedBoxState& TrackTool = Tool.AddCollider();
            TrackTool.CenterMeters = FVector3f(TrackTransform.GetLocation() / 100.0);
            TrackTool.AxisX = FVector3f(TrackTransform.GetUnitAxis(EAxis::X));
            TrackTool.AxisY = FVector3f(TrackTransform.GetUnitAxis(EAxis::Y));
            TrackTool.AxisZ = FVector3f(TrackTransform.GetUnitAxis(EAxis::Z));
            TrackTool.HalfExtentsMeters =
                FVector3f(Track->GetScaledBoxExtent() / 100.0) + FVector3f(0.003f);
            TrackTool.LinearVelocityMetersPerSecond = ChassisLinearVelocity;
            TrackTool.AngularVelocityRadiansPerSecond = ChassisAngularVelocity;
        };
        AddTrack(Excavator->LeftTrackCollider);
        AddTrack(Excavator->RightTrackCollider);

        const FTransform BucketTransform = Excavator->BucketPivot->GetComponentTransform();
        FVector3f BucketPivotLinearVelocity = FVector3f::ZeroVector;
        FVector3f BucketAngularVelocity = FVector3f::ZeroVector;
        // The async solver may skip render ticks. Measure tool motion over its
        // actual sampling interval, otherwise slow frames inject excess speed.
        const float OuterStepSeconds = ToolSampleElapsedSeconds;
        if (bHasPreviousBucketTransform && OuterStepSeconds > UE_SMALL_NUMBER)
        {
            BucketPivotLinearVelocity = FVector3f(
                (BucketTransform.GetLocation() - PreviousBucketTransform.GetLocation()) /
                (100.0 * OuterStepSeconds));
            FQuat DeltaRotation = BucketTransform.GetRotation() * PreviousBucketTransform.GetRotation().Inverse();
            DeltaRotation.Normalize();
            FVector RotationAxis;
            double RotationAngle;
            DeltaRotation.ToAxisAndAngle(RotationAxis, RotationAngle);
            if (RotationAngle > PI)
            {
                RotationAngle -= 2.0 * PI;
            }
            BucketAngularVelocity = FVector3f(
                RotationAxis * (RotationAngle / OuterStepSeconds));
        }
        else
        {
            BucketPivotLinearVelocity = FVector3f(
                Excavator->ChassisBody->GetPhysicsLinearVelocityAtPoint(
                    BucketTransform.GetLocation()) / 100.0);
        }
        PreviousBucketTransform = BucketTransform;
        bHasPreviousBucketTransform = true;
        ToolSampleElapsedSeconds = 0.0f;

        const auto AddBucketPlate = [
            &Tool, &BucketTransform, BucketPivotLinearVelocity, BucketAngularVelocity](
            const UBoxComponent* Plate)
        {
            const FTransform PlateTransform = Plate->GetComponentTransform();
            Sand::MPM::FToolOrientedBoxState& BucketTool = Tool.AddCollider();
            BucketTool.CenterMeters = FVector3f(PlateTransform.GetLocation() / 100.0);
            BucketTool.AxisX = FVector3f(PlateTransform.GetUnitAxis(EAxis::X));
            BucketTool.AxisY = FVector3f(PlateTransform.GetUnitAxis(EAxis::Y));
            BucketTool.AxisZ = FVector3f(PlateTransform.GetUnitAxis(EAxis::Z));
            // A small shell plus the exact particle-contact pass keeps thin
            // plates sealed without the former 2.5 cm invisible stand-off.
            BucketTool.HalfExtentsMeters =
                FVector3f(Plate->GetScaledBoxExtent() / 100.0) + FVector3f(0.0125f);
            const FVector3f LeverArmMeters = BucketTool.CenterMeters -
                FVector3f(BucketTransform.GetLocation() / 100.0);
            BucketTool.LinearVelocityMetersPerSecond = BucketPivotLinearVelocity +
                FVector3f::CrossProduct(BucketAngularVelocity, LeverArmMeters);
            BucketTool.AngularVelocityRadiansPerSecond = BucketAngularVelocity;
        };
        AddBucketPlate(Excavator->BucketCollider);
        AddBucketPlate(Excavator->BucketBackCollider);
        AddBucketPlate(Excavator->BucketLeftCollider);
        AddBucketPlate(Excavator->BucketRightCollider);
        AddBucketPlate(Excavator->BucketLipCollider);

        const FVector InteriorCenterCentimeters = BucketTransform.TransformPosition(
            FVector(13.0, 0.0, 0.5));
        // Use the bucket linkage angle for the definitive dump state. A world-space
        // opening test alone is ambiguous on a tilted chassis and previously could
        // never reach its release threshold at the mechanical stop, leaving the
        // captured load attached indefinitely.
        const bool bFullDump = Excavator->GetBucketAngleDegrees() <= -155.0f;
        Tool.bBucketInteriorEnabled = !bFullDump && !FParse::Param(FCommandLine::Get(),TEXT("SandDisableBucketCarry"));
        const float OpeningUpwardComponent = FVector::DotProduct(
            BucketTransform.GetUnitAxis(EAxis::Z), FVector::UpVector);
        const float RetentionBlend = FMath::Clamp(
            (OpeningUpwardComponent + 0.60f) / 0.40f, 0.0f, 1.0f);
        Tool.BucketInteriorDampingPerSecond = FMath::Lerp(2.0f, 60.0f, RetentionBlend);
        Tool.BucketInterior.CenterMeters = FVector3f(InteriorCenterCentimeters / 100.0);
        Tool.BucketInterior.AxisX = FVector3f(BucketTransform.GetUnitAxis(EAxis::X));
        Tool.BucketInterior.AxisY = FVector3f(BucketTransform.GetUnitAxis(EAxis::Y));
        Tool.BucketInterior.AxisZ = FVector3f(BucketTransform.GetUnitAxis(EAxis::Z));
        Tool.BucketInterior.HalfExtentsMeters = FVector3f(0.14f, 0.16f, 0.095f);
        const FVector3f InteriorLeverArmMeters = Tool.BucketInterior.CenterMeters -
            FVector3f(BucketTransform.GetLocation() / 100.0);
        Tool.BucketInterior.LinearVelocityMetersPerSecond = BucketPivotLinearVelocity +
            FVector3f::CrossProduct(BucketAngularVelocity, InteriorLeverArmMeters);
        Tool.BucketInterior.AngularVelocityRadiansPerSecond = BucketAngularVelocity;
    }

    // The mobile mesh may refresh every physical frame, but only after its
    // previous GPU density readback, CPU extraction and upload complete.
    // Slow devices naturally skip busy frames without paying for a particle
    // readback whose data would otherwise be discarded.
    const bool bExpandedLunarPatch =
        SimulationState->PhysicalMaximum.X - SimulationState->PhysicalMinimum.X > 8.0f;
    const uint32 SurfaceStride = SimulationState->CellSize >= 0.099f
        ? 1u : (bExpandedLunarPatch ? 2u :
            static_cast<uint32>(FMath::RoundToInt(0.1f / FixedFrameSeconds)));
    const bool bRefreshSurface = !IsSurfaceBuildInFlight() &&
        CompletedSimulationFrames + FramesToAdvance >= LastSurfaceSampleFrame + SurfaceStride;
    if (bRefreshSurface)
    {
        LastSurfaceSampleFrame = CompletedSimulationFrames + FramesToAdvance;
    }
    const TWeakObjectPtr<ASandCollapseSurfacePreviewActor> WeakThis(this);
    const TWeakObjectPtr<ASandExcavatorPawn> WeakExcavator = Excavator;
    const uint32 ActiveToolColliderCount = Tool.ColliderCount;
    const bool bBucketRetentionEnabled = Tool.bBucketInteriorEnabled;
    Sand::MPM::EnqueueRuntimeSimulationSteps(
        SimulationState.ToSharedRef(),
        FMath::RoundToInt(FixedFrameSeconds/SimulationState->InternalDeltaSeconds) * FramesToAdvance,
        bRefreshSurface,
        Tool,
        [WeakThis, WeakExcavator, FramesToAdvance, FixedFrameSeconds, ActiveToolColliderCount,
            bBucketRetentionEnabled](
            TArray<Sand::MPM::FParticleData>&& Particles,
            const Sand::MPM::FToolInteractionResult& ToolInteraction,
            const double GpuSeconds)
        {
            if (!WeakThis.IsValid())
            {
                return;
            }
            WeakThis->bSimulationStepInFlight = false;
            WeakThis->CompletedSimulationFrames += FramesToAdvance;
            WeakThis->LastGpuStepMilliseconds = static_cast<float>(GpuSeconds * 1000.0);
            const bool bLogFrame = WeakThis->CompletedSimulationFrames == FramesToAdvance ||
                WeakThis->CompletedSimulationFrames % FMath::RoundToInt(2.f/FixedFrameSeconds) == 0;
            int32 MovedParticleCount = -1;
            int32 BucketInteriorParticleCount = -1;
            int32 CarriedParticleCount = -1;
            float MaximumParticleDisplacementMeters = -1.0f;

            if (auto* Machine=Cast<ASandRoadheaderPawn>(WeakExcavator.Get()))
            {
                Machine->CompletePhysicalStep(ToolInteraction,FramesToAdvance*FixedFrameSeconds,Particles);
            }
            if (WeakExcavator.IsValid() && WeakExcavator->ChassisBody->IsSimulatingPhysics())
            {
                const bool Direct=FParse::Param(FCommandLine::Get(),TEXT("SandDirectReaction"));
                if(Direct) {
                    // Sum both grid and material-point contact impulses. The
                    // legacy aggregate omitted the material-point correction.
                    FVector J=FVector::ZeroVector;
                    for(uint32 I=2;I<ActiveToolColliderCount;++I) J-=FVector(ToolInteraction.ColliderLinear[I]);
                    const FVector Force=J/(FramesToAdvance*FixedFrameSeconds);
                    WeakExcavator->SetAppliedContactForce(Force);
                    // This mode requires a synchronous world step: force and
                    // traction act over the same dt, with no reaction clipping.
                    WeakExcavator->ChassisBody->AddForce(100*Force);
                    if(bLogFrame) UE_LOG(LogTemp,Display,TEXT("CHASSIS_COUPLING rawFxN=%.3f rawFyN=%.3f rawFzN=%.3f filteredFxN=%.3f filteredFyN=%.3f filteredFzN=%.3f forceCapN=-1"),Force.X,Force.Y,Force.Z,Force.X,Force.Y,Force.Z);
                } else {
                // Equal and opposite bucket reaction. The raw grid contact can
                // jump as individual 10 cm nodes enter a thin plate, so feed a
                // filtered, acceleration-limited impulse to the much smaller
                // Chaos chassis. Track reaction is handled by its suspension.
                FVector ReactionImpulseMeters =
                    -FVector(ToolInteraction.SandLinearImpulseKgMetersPerSecond);
                const float OuterStepSeconds = FramesToAdvance*FixedFrameSeconds;
                const float MaximumImpulse =
                    WeakExcavator->ChassisBody->GetMass() * 1.25f * OuterStepSeconds;
                ReactionImpulseMeters = ReactionImpulseMeters.GetClampedToMaxSize(MaximumImpulse);
                if (ReactionImpulseMeters.SizeSquared() < FMath::Square(0.025f))
                {
                    ReactionImpulseMeters = FVector::ZeroVector;
                }
                WeakThis->SmoothedBucketReactionImpulseMeters = FMath::Lerp(
                    WeakThis->SmoothedBucketReactionImpulseMeters,
                    ReactionImpulseMeters,
                    0.24f);
                WeakExcavator->SetAppliedContactForce(WeakThis->SmoothedBucketReactionImpulseMeters/OuterStepSeconds);
                if(bLogFrame)
                {
                    const FVector RawForce=-FVector(ToolInteraction.SandLinearImpulseKgMetersPerSecond)/OuterStepSeconds;
                    const FVector AppliedForce=WeakThis->SmoothedBucketReactionImpulseMeters/OuterStepSeconds;
                    UE_LOG(LogTemp,Display,TEXT("CHASSIS_COUPLING rawFxN=%.3f rawFyN=%.3f rawFzN=%.3f filteredFxN=%.3f filteredFyN=%.3f filteredFzN=%.3f forceCapN=%.3f"),
                        RawForce.X,RawForce.Y,RawForce.Z,AppliedForce.X,AppliedForce.Y,AppliedForce.Z,MaximumImpulse/OuterStepSeconds);
                }
                if (!WeakThis->SmoothedBucketReactionImpulseMeters.IsNearlyZero(0.005f))
                {
                    WeakExcavator->ChassisBody->AddImpulse(
                        WeakThis->SmoothedBucketReactionImpulseMeters * 100.0,
                        NAME_None,
                        false);
                }
            }

            }

            if (!Particles.IsEmpty())
            {
                if (WeakThis->LunarChunkCache.IsValid() && WeakExcavator.IsValid() &&
                    WeakThis->SimulationState.IsValid())
                {
                    FLunarChunkCache& Cache = *WeakThis->LunarChunkCache;
                    const FVector ExcavatorLocationMeters =
                        WeakExcavator->GetActorLocation() / 100.0;
                    const float MaximumCenter = Cache.ChunkWidthMeters * FMath::CeilToFloat(
                        0.5f * (Cache.PlayableWidthMeters - Cache.ActiveWidthMeters) /
                        Cache.ChunkWidthMeters);
                    const FVector2f RequestedCenter(
                        FMath::Clamp(Cache.ChunkWidthMeters * FMath::RoundToInt(
                            ExcavatorLocationMeters.X / Cache.ChunkWidthMeters),
                            -MaximumCenter,MaximumCenter),
                        FMath::Clamp(Cache.ChunkWidthMeters * FMath::RoundToInt(
                            ExcavatorLocationMeters.Y / Cache.ChunkWidthMeters),
                            -MaximumCenter,MaximumCenter));
                    // Prepare one entering strip chunk per surface update while
                    // the machine approaches an edge. Ordinary driving then
                    // spreads seeding over several frames instead of creating
                    // three chunks on the exact migration frame.
                    FVector2f PrefetchCenter = Cache.ActiveCenterMeters;
                    const FVector2f OffsetFromWindowCenter =
                        FVector2f(ExcavatorLocationMeters.X,ExcavatorLocationMeters.Y) -
                        Cache.ActiveCenterMeters;
                    constexpr float PrefetchThresholdMeters = 1.25f;
                    if (FMath::Abs(OffsetFromWindowCenter.X) > PrefetchThresholdMeters)
                    {
                        PrefetchCenter.X += FMath::Sign(OffsetFromWindowCenter.X) *
                            Cache.ChunkWidthMeters;
                    }
                    if (FMath::Abs(OffsetFromWindowCenter.Y) > PrefetchThresholdMeters)
                    {
                        PrefetchCenter.Y += FMath::Sign(OffsetFromWindowCenter.Y) *
                            Cache.ChunkWidthMeters;
                    }
                    PrefetchCenter.X = FMath::Clamp(
                        PrefetchCenter.X,-MaximumCenter,MaximumCenter);
                    PrefetchCenter.Y = FMath::Clamp(
                        PrefetchCenter.Y,-MaximumCenter,MaximumCenter);
                    if (!PrefetchCenter.Equals(Cache.ActiveCenterMeters,0.01f))
                    {
                        const int32 Prefetched = Cache.PrefetchWindowEdge(
                            PrefetchCenter,WeakThis->SimulationState->CellSize);
                        if (Prefetched > 0)
                        {
                            UE_LOG(LogTemp,Verbose,
                                TEXT("LUNAR_WINDOW_PREFETCH target=(%.1f,%.1f)m chunks=%d"),
                                PrefetchCenter.X,PrefetchCenter.Y,Prefetched);
                        }
                    }
                    if (!RequestedCenter.Equals(Cache.ActiveCenterMeters,0.01f))
                    {
                        const double ShiftCpuStart = FPlatformTime::Seconds();
                        const FVector2f PreviousCenter = Cache.ActiveCenterMeters;
                        Cache.StoreResidentWindow(Particles);
                        constexpr int32 DeformationResolution = 33;
                        const float DeformationSpacing = Cache.ChunkWidthMeters /
                            (DeformationResolution - 1);
                        const FIntPoint PreviousMinimumKey =
                            Cache.MinimumResidentChunk(PreviousCenter);
                        int32 DepartingChunkCount = 0;
                        for (TActorIterator<ASandLunarWorldActor> It(
                            WeakThis->GetWorld()); It; ++It)
                        {
                            for (int32 Y = 0; Y < Cache.ResidentChunksPerAxis(); ++Y)
                            {
                                for (int32 X = 0; X < Cache.ResidentChunksPerAxis(); ++X)
                                {
                                    const FIntPoint Key = PreviousMinimumKey + FIntPoint(X,Y);
                                    if (Cache.IsResidentChunk(Key,RequestedCenter))
                                    {
                                        continue;
                                    }
                                    It->CacheDeformationChunk(Key,
                                        Cache.BuildHeightfield(Key,DeformationResolution,
                                            WeakThis->SimulationState->CellSize),
                                        DeformationResolution,DeformationSpacing);
                                    ++DepartingChunkCount;
                                }
                            }
                        }
                        TArray<Sand::MPM::FParticleData> NewParticles =
                            Cache.LoadWindow(RequestedCenter,WeakThis->SimulationState->CellSize);
                        const int32 ResidentParticleCount = NewParticles.Num();
                        Particles = NewParticles;
                        Sand::MPM::ResetRuntimeSandboxWindow(
                            WeakThis->SimulationState.ToSharedRef(),MoveTemp(NewParticles),
                            RequestedCenter,Cache.ActiveWidthMeters,
                            Cache.SandDepthMeters + 0.32f);
                        WeakThis->SupportHeightGridCellMeters =
                            WeakThis->SimulationState->CellSize;
                        WeakThis->SupportHeightGridMinimumMeters = FVector2f(
                            WeakThis->SimulationState->PhysicalMinimum.X,
                            WeakThis->SimulationState->PhysicalMinimum.Y);
                        WeakThis->SupportHeightGridSize = FIntPoint(
                            FMath::CeilToInt(Cache.ActiveWidthMeters /
                                WeakThis->SupportHeightGridCellMeters) + 1,
                            FMath::CeilToInt(Cache.ActiveWidthMeters /
                                WeakThis->SupportHeightGridCellMeters) + 1);
                        WeakThis->SupportHeightGridCentimeters.Init(
                            -TNumericLimits<float>::Max(),
                            WeakThis->SupportHeightGridSize.X *
                                WeakThis->SupportHeightGridSize.Y);
                        // Keep the old surface and transition opening visible
                        // until the async mesh for the new resident particles
                        // is ready. OnSurfaceMeshUpdated commits both together.
                        WeakThis->PendingLunarWindowCenterMeters = RequestedCenter;
                        WeakThis->bHasPendingLunarWindowCenter = true;
                        const double ShiftCpuMilliseconds =
                            1000.0 * (FPlatformTime::Seconds()-ShiftCpuStart);
                        UE_LOG(LogTemp,Display,
                            TEXT("LUNAR_WINDOW_SHIFT index=%d old=(%.1f,%.1f)m new=(%.1f,%.1f)m residentParticles=%d cachedChunks=%d departingChunks=%d cpuMs=%.2f"),
                            Cache.ShiftCount,PreviousCenter.X,PreviousCenter.Y,
                            RequestedCenter.X,RequestedCenter.Y,ResidentParticleCount,
                            Cache.Chunks.Num(),DepartingChunkCount,ShiftCpuMilliseconds);
                    }
                }
                if (bLogFrame && WeakThis->SimulationState.IsValid())
                {
                    MovedParticleCount = 0;
                    BucketInteriorParticleCount = 0;
                    CarriedParticleCount = 0;
                    MaximumParticleDisplacementMeters = 0.0f;
                    const TArray<Sand::MPM::FParticleData>& InitialParticles =
                        WeakThis->SimulationState->InitialParticles;
                    const FTransform BucketWorldToLocal = WeakExcavator.IsValid()
                        ? WeakExcavator->BucketPivot->GetComponentTransform().Inverse()
                        : FTransform::Identity;
                    for (int32 ParticleIndex = 0;
                        ParticleIndex < FMath::Min(Particles.Num(), InitialParticles.Num());
                        ++ParticleIndex)
                    {
                        const FVector3f PositionMeters(Particles[ParticleIndex].PositionAndMass);
                        const FVector3f InitialPositionMeters(
                            InitialParticles[ParticleIndex].PositionAndMass);
                        const float Displacement = (PositionMeters - InitialPositionMeters).Length();
                        MaximumParticleDisplacementMeters =
                            FMath::Max(MaximumParticleDisplacementMeters, Displacement);
                        if (Displacement > 0.05f)
                        {
                            ++MovedParticleCount;
                        }
                        if (Particles[ParticleIndex].BucketLocalAndCarried.W > 0.5f)
                        {
                            ++CarriedParticleCount;
                        }

                        if (WeakExcavator.IsValid())
                        {
                            const FVector BucketLocalCentimeters = BucketWorldToLocal.TransformPosition(
                                FVector(PositionMeters) * 100.0);
                            if (BucketLocalCentimeters.X >= -0.5 && BucketLocalCentimeters.X <= 27.0 &&
                                FMath::Abs(BucketLocalCentimeters.Y) <= 16.0 &&
                                BucketLocalCentimeters.Z >= -8.5 && BucketLocalCentimeters.Z <= 9.0)
                            {
                                ++BucketInteriorParticleCount;
                            }
                        }
                    }
                }

                TArray<FVector3f> Positions;
                TArray<FVector3f> SupportPositions;
                Positions.Reserve(Particles.Num());
                SupportPositions.Reserve(Particles.Num());
                float PitSurfaceZ = 0.0f;
                for (const Sand::MPM::FParticleData& Particle : Particles)
                {
                    const FVector3f Position(Particle.PositionAndMass);
                    auto* Machine=Cast<ASandRoadheaderPawn>(WeakExcavator.Get());
                    if(!Machine || !Machine->IsConveyorRegion(Position)) Positions.Add(Position);
                    // A load above a track is not load-bearing terrain.
                    if (Particle.BucketLocalAndCarried.W < 0.5f && (!Machine || !Machine->IsConveyorRegion(Position)))
                    {
                        SupportPositions.Add(Position);
                    }
                    if (Position.X * Position.X + Position.Y * Position.Y < 0.10f * 0.10f)
                    {
                        PitSurfaceZ = FMath::Max(PitSurfaceZ, Position.Z + 0.03125f);
                    }
                }
                WeakThis->SupportHeightGridCentimeters.Init(-TNumericLimits<float>::Max(),
                    WeakThis->SupportHeightGridSize.X * WeakThis->SupportHeightGridSize.Y);
                const float ParticleSurfaceRadiusCentimeters =
                    42.0f * WeakThis->SimulationState->CellSize;
                for (const FVector3f& Position : SupportPositions)
                {
                    const int32 GridX = FMath::FloorToInt(
                        (Position.X - WeakThis->SupportHeightGridMinimumMeters.X) /
                        WeakThis->SupportHeightGridCellMeters);
                    const int32 GridY = FMath::FloorToInt(
                        (Position.Y - WeakThis->SupportHeightGridMinimumMeters.Y) /
                        WeakThis->SupportHeightGridCellMeters);
                    if (GridX >= 0 && GridY >= 0 && GridX < WeakThis->SupportHeightGridSize.X &&
                        GridY < WeakThis->SupportHeightGridSize.Y)
                    {
                        float& Height = WeakThis->SupportHeightGridCentimeters[
                            GridX + GridY * WeakThis->SupportHeightGridSize.X];
                        Height = FMath::Max(Height,
                            Position.Z * 100.0f + ParticleSurfaceRadiusCentimeters);
                    }
                }
                if (bLogFrame && FParse::Param(FCommandLine::Get(), TEXT("SandPitTest")))
                {
                    UE_LOG(LogTemp, Display, TEXT("Pit retention: sim %.2f s, centre depth %.3f m, particles %d"),
                        WeakThis->CompletedSimulationFrames*FixedFrameSeconds, 2.0f - PitSurfaceZ, Particles.Num());
                }
                if (WeakExcavator.IsValid())
                {
                    const float CellSizeCm = 100.0f * WeakThis->SimulationState->CellSize;
                    const float SurfaceRadiusCm = FParse::Param(FCommandLine::Get(), TEXT("SandDirectReaction"))
                        ? 0.5f * CellSizeCm
                        : (CellSizeCm >= 9.5f ? 0.5f * CellSizeCm - 0.5f : 0.42f * CellSizeCm);
                    WeakExcavator->UpdateSandSupportSurface(
                        SupportPositions, CellSizeCm, SurfaceRadiusCm);
                }
                WeakThis->GenerateSurfaceFromParticlePositions(
                    MoveTemp(Positions),
                    WeakThis->SimulationState->PhysicalMinimum-FVector3f(.10f),
                    WeakThis->SimulationState->PhysicalMaximum+FVector3f(.10f));
            }

            if (bLogFrame)
            {
                UE_LOG(LogTemp, Display,
                    TEXT("Persistent runtime MPM frame %llu: %.2f ms GPU+readback, %u tool colliders, raw sand impulse %.6f kg*m/s, chassis (X,Z)=(%.1f,%.1f) cm pitch %.1f roll %.1f, supports %d/4, bucket Z %.1f cm, bucket angle %.1f, opening-up %.3f, retention %d, moved particles %d, max displacement %.3f m, bucket interior %d, carried %d"),
                    WeakThis->CompletedSimulationFrames,
                    GpuSeconds * 1000.0,
                    ActiveToolColliderCount,
                    FVector(ToolInteraction.SandLinearImpulseKgMetersPerSecond).Length(),
                    WeakExcavator.IsValid() ? WeakExcavator->GetActorLocation().X : -1.0,
                    WeakExcavator.IsValid() ? WeakExcavator->GetActorLocation().Z : -1.0,
                    WeakExcavator.IsValid() ? WeakExcavator->GetActorRotation().Pitch : 0.0,
                    WeakExcavator.IsValid() ? WeakExcavator->GetActorRotation().Roll : 0.0,
                    WeakExcavator.IsValid() ? WeakExcavator->GetGroundedSupportCount() : 0,
                    WeakExcavator.IsValid() ? WeakExcavator->BucketCollider->GetComponentLocation().Z : -1.0,
                    WeakExcavator.IsValid() ? WeakExcavator->GetBucketAngleDegrees() : 0.0,
                    WeakExcavator.IsValid() ? WeakExcavator->BucketPivot->GetUpVector().Z : -2.0,
                    bBucketRetentionEnabled ? 1 : 0,
                    MovedParticleCount,
                    MaximumParticleDisplacementMeters,
                    BucketInteriorParticleCount,
                    CarriedParticleCount);
            }
        });
}
