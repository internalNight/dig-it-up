#include "SandCollapseSurfacePreviewActor.h"

#include "SandMPMSolver.h"
#include "SandExcavatorPawn.h"
#include "SandRoadheaderPawn.h"
#include "SandLevelSettings.h"
#include "SandPreviewGameMode.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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

void ASandCollapseSurfacePreviewActor::BeginPlay()
{
    Super::BeginPlay();

    const FSandMaterialParameters Material =
        FParse::Param(FCommandLine::Get(), TEXT("SandLooseBaseline"))
        ? FSandMaterialParameters() : RuntimeMaterial;
    SimulationState = Sand::MPM::CreateRuntimeSandboxSimulation(Material);
    // Acceptance fixture: a sloping corner excavation with two intact bottom layers.
    // Removed material is stacked in the upper air region, conserving mass.
    if (FParse::Param(FCommandLine::Get(), TEXT("SandVictoryTest")))
    {
        int32 Relocated = 0;
        const float Depth = GetDefault<USandLevelSettings>()->SandDepthMeters;
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
    if (Excavator.IsValid())
    {
        Excavator->UpdateVisibleSandSupport(Vertices, Indices);
    }
    if (auto* Mode = Cast<ASandPreviewGameMode>(GetWorld()->GetAuthGameMode()))
    {
        Mode->CheckExposedFloor(Vertices, Indices);
    }
}

void ASandCollapseSurfacePreviewActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(auto* Mode=Cast<ASandPreviewGameMode>(GetWorld()->GetAuthGameMode()))
        if(Mode->IsSelectingVehicle()) return;
    ToolSampleElapsedSeconds += DeltaSeconds;
    if (!SimulationState.IsValid() || bSimulationStepInFlight)
    {
        return;
    }

    constexpr float FixedFrameSeconds = 1.0f / 30.0f;
    SimulationAccumulatorSeconds += DeltaSeconds * SimulationSpeed;
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
        for (TActorIterator<ASandExcavatorPawn> It(GetWorld()); It; ++It)
        {
            Excavator = *It;
            break;
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
        Tool.bBucketInteriorEnabled = !bFullDump;
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

    // The GPU simulation stays at 30 Hz; CPU marching-cubes surface work is intentionally
    // limited to 10 Hz so a 16 GB machine cannot build up an unbounded mesh backlog.
    const bool bRefreshSurface = (CompletedSimulationFrames / 3u) !=
        ((CompletedSimulationFrames + FramesToAdvance) / 3u);
    const TWeakObjectPtr<ASandCollapseSurfacePreviewActor> WeakThis(this);
    const TWeakObjectPtr<ASandExcavatorPawn> WeakExcavator = Excavator;
    const uint32 ActiveToolColliderCount = Tool.ColliderCount;
    const bool bBucketRetentionEnabled = Tool.bBucketInteriorEnabled;
    Sand::MPM::EnqueueRuntimeSimulationSteps(
        SimulationState.ToSharedRef(),
        FMath::RoundToInt(1.0f/(30.0f*SimulationState->InternalDeltaSeconds)) * FramesToAdvance,
        bRefreshSurface,
        Tool,
        [WeakThis, WeakExcavator, FramesToAdvance, ActiveToolColliderCount,
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
            const bool bLogFrame = WeakThis->CompletedSimulationFrames == FramesToAdvance ||
                WeakThis->CompletedSimulationFrames % 60 == 0;
            int32 MovedParticleCount = -1;
            int32 BucketInteriorParticleCount = -1;
            int32 CarriedParticleCount = -1;
            float MaximumParticleDisplacementMeters = -1.0f;

            if (auto* Machine=Cast<ASandRoadheaderPawn>(WeakExcavator.Get()))
            {
                Machine->CompletePhysicalStep(ToolInteraction,FramesToAdvance/30.0f,Particles);
            }
            if (WeakExcavator.IsValid() && WeakExcavator->ChassisBody->IsSimulatingPhysics())
            {
                // Equal and opposite bucket reaction. The raw grid contact can
                // jump as individual 10 cm nodes enter a thin plate, so feed a
                // filtered, acceleration-limited impulse to the much smaller
                // Chaos chassis. Track reaction is handled by its suspension.
                FVector ReactionImpulseMeters =
                    -FVector(ToolInteraction.SandLinearImpulseKgMetersPerSecond);
                const float OuterStepSeconds = FramesToAdvance / 30.0f;
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
                if (!WeakThis->SmoothedBucketReactionImpulseMeters.IsNearlyZero(0.005f))
                {
                    WeakExcavator->ChassisBody->AddImpulse(
                        WeakThis->SmoothedBucketReactionImpulseMeters * 100.0,
                        NAME_None,
                        false);
                }
            }

            if (!Particles.IsEmpty())
            {
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
                    if (Particle.BucketLocalAndCarried.W < 0.5f)
                    {
                        SupportPositions.Add(Position);
                    }
                    if (Position.X * Position.X + Position.Y * Position.Y < 0.10f * 0.10f)
                    {
                        PitSurfaceZ = FMath::Max(PitSurfaceZ, Position.Z + 0.03125f);
                    }
                }
                if (bLogFrame && FParse::Param(FCommandLine::Get(), TEXT("SandPitTest")))
                {
                    UE_LOG(LogTemp, Display, TEXT("Pit retention: sim %.2f s, centre depth %.3f m, particles %d"),
                        WeakThis->CompletedSimulationFrames / 30.0f, 2.0f - PitSurfaceZ, Particles.Num());
                }
                if (WeakExcavator.IsValid())
                {
                    WeakExcavator->UpdateSandSupportSurface(SupportPositions);
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
