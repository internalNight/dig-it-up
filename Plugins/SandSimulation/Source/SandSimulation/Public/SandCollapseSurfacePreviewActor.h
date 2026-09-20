#pragma once

#include "SandSurfacePreviewActor.h"
#include "SandSimulationTypes.h"
#include "SandCollapseSurfacePreviewActor.generated.h"

namespace Sand::MPM
{
struct FRuntimeSimulationState;
}
class ASandExcavatorPawn;
struct FLunarChunkCache;

/** Plays the validated GPU MPM column collapse through the continuous volumetric surface renderer. */
UCLASS()
class SANDSIMULATION_API ASandCollapseSurfacePreviewActor final : public ASandSurfacePreviewActor
{
    GENERATED_BODY()

public:
    ASandCollapseSurfacePreviewActor();
    virtual ~ASandCollapseSurfacePreviewActor() override;

    uint64 GetCompletedSimulationFrames() const { return CompletedSimulationFrames; }
    float GetLastGpuStepMilliseconds() const { return LastGpuStepMilliseconds; }
    bool SampleSandSurfaceHeightCentimeters(
        const FVector2D& WorldPositionCentimeters,
        float RadiusCentimeters,
        float& OutHeightCentimeters) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Preview", meta = (ClampMin = "0.1", ClampMax = "2.0"))
    float SimulationSpeed = 1.0f;

    /** Gameplay granular-soil preset; independent of the silver-grey visual material. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Material")
    FSandMaterialParameters RuntimeMaterial;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnSurfaceMeshUpdated(const TArray<FVector>& Vertices, const TArray<int32>& Indices) override;

private:
    TSharedPtr<Sand::MPM::FRuntimeSimulationState, ESPMode::ThreadSafe> SimulationState;
    TWeakObjectPtr<ASandExcavatorPawn> Excavator;
    float SimulationAccumulatorSeconds = 0.0f;
    bool bSimulationStepInFlight = false;
    uint64 CompletedSimulationFrames = 0;
    uint64 LastSurfaceSampleFrame = 0;
    float LastGpuStepMilliseconds = 0.0f;
    FTransform PreviousBucketTransform;
    float ToolSampleElapsedSeconds = 0.0f;
    bool bHasPreviousBucketTransform = false;
    FVector SmoothedBucketReactionImpulseMeters = FVector::ZeroVector;
    TArray<float> SupportHeightGridCentimeters;
    FIntPoint SupportHeightGridSize = FIntPoint::ZeroValue;
    FVector2f SupportHeightGridMinimumMeters = FVector2f::ZeroVector;
    float SupportHeightGridCellMeters = 0.1f;
    TSharedPtr<FLunarChunkCache> LunarChunkCache;
};
