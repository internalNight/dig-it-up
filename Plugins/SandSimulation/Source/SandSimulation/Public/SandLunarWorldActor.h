#pragma once

#include "GameFramework/Actor.h"
#include "SandLunarWorldActor.generated.h"

class ASandCollapseSurfacePreviewActor;
class UProceduralMeshComponent;
class USceneComponent;

/** Large, lightweight lunar context around the local GPU-MPM excavation patch. */
UCLASS()
class SANDSIMULATION_API ASandLunarWorldActor final : public AActor
{
    GENERATED_BODY()

public:
    ASandLunarWorldActor();
    virtual void Tick(float DeltaSeconds) override;
    void CacheDeformationChunk(
        FIntPoint ChunkKey,
        TArray<float>&& HeightMeters,
        int32 Resolution,
        float SpacingMeters);
    void SetActiveWindowCenterMeters(FVector2f NewCenterMeters);
    void SetOverviewMode(bool bEnabled);

protected:
    virtual void BeginPlay() override;

private:
    void BuildMacroTerrain();
    void BuildDistantTerrain();
    void BuildTransitionTerrain();
    void BuildCachedDeformationTerrain();
    void BuildRocks();

    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> MacroTerrain;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> DistantTerrain;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> TransitionTerrain;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> CachedDeformationTerrain;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> StaticRocks;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UProceduralMeshComponent>> DynamicRocks;

    TArray<float> DynamicRockSupportRadiiCentimeters;
    TArray<float> DynamicRockHalfHeightsCentimeters;
    TWeakObjectPtr<ASandCollapseSurfacePreviewActor> SandSurface;
    float RockTestElapsedSeconds = 0.0f;
    float RockTestLastLogSeconds = -1.0f;
    bool bRockTestImpulseApplied = false;
    FVector2f ActiveWindowCenterMeters = FVector2f::ZeroVector;
    TMap<FIntPoint,TArray<float>> CachedDeformationHeights;
    int32 CachedDeformationResolution = 0;
    float CachedDeformationSpacingMeters = 0.0f;
    bool bOverviewMode = false;
};
