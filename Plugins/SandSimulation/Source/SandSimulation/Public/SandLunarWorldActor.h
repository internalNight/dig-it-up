#pragma once

#include "GameFramework/Actor.h"
#include "SandLunarWorldActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;

/** Large, lightweight lunar context around the local GPU-MPM excavation patch. */
UCLASS()
class SANDSIMULATION_API ASandLunarWorldActor final : public AActor
{
    GENERATED_BODY()

public:
    ASandLunarWorldActor();

protected:
    virtual void BeginPlay() override;

private:
    void BuildMacroTerrain();
    void BuildTransitionTerrain();
    void BuildRocks();

    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> MacroTerrain;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> TransitionTerrain;

    UPROPERTY()
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Rocks;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> HighlandMaterial;
};
