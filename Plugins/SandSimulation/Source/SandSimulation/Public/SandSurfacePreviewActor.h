#pragma once

#include "GameFramework/Actor.h"
#include "SandSurfacePreviewActor.generated.h"

class UProceduralMeshComponent;

/**
 * Diagnostic continuous-surface renderer for the granular material-point state.
 * Density is reconstructed on the GPU; this first milestone extracts the
 * isosurface on a worker thread and uploads it to a Procedural Mesh Component.
 */
UCLASS(BlueprintType)
class SANDSIMULATION_API ASandSurfacePreviewActor : public AActor
{
    GENERATED_BODY()

public:
    ASandSurfacePreviewActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sand|Surface")
    TObjectPtr<UProceduralMeshComponent> SurfaceMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Surface")
    bool bBuildOnBeginPlay = true;

    /** Rendering field resolution; independent of the physical grid resolution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Surface", meta = (ClampMin = "0.0125", ClampMax = "0.1", Units = "m"))
    float VoxelSizeMeters = 0.025f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Surface", meta = (ClampMin = "0.0125", ClampMax = "0.1", Units = "m"))
    float PreviewParticleSpacingMeters = 0.03125f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Surface", meta = (ClampMin = "0.02", ClampMax = "0.15", Units = "m"))
    float KernelRadiusMeters = 0.070f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Surface", meta = (ClampMin = "0.1", ClampMax = "5.0"))
    float IsoDensity = 1.10f;

    float ParticleDensityWeight = 1.0f;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sand|Surface")
    void GeneratePreviewSurface();

    /** Starts one asynchronous reconstruction, or returns false while the previous frame is still building. */
    bool GenerateSurfaceFromParticlePositions(
        TArray<FVector3f> ParticlePositionsMeters,
        const FVector3f& MinimumMeters,
        const FVector3f& MaximumMeters);

    bool IsSurfaceBuildInFlight() const { return bSurfaceBuildInFlight; }

protected:
    virtual void BeginPlay() override;
    virtual void OnSurfaceMeshUpdated(const TArray<FVector>& Vertices, const TArray<int32>& Indices) {}

private:
    uint32 BuildGeneration = 0;
    uint64 CompletedBuildCount = 0;
    bool bSurfaceBuildInFlight = false;
    bool bCaptureScheduled = false;
};
