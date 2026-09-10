#pragma once

#include "GameFramework/Actor.h"
#include "SandSimulationVolume.generated.h"

class UBoxComponent;
class USandSimulationComponent;
class USceneComponent;

/**
 * Placeable sandbox frame. The outer wire box is the full 5x5x3 m solve domain;
 * the inner wire box shows the initial, fully movable 2 m sand fill.
 */
UCLASS(BlueprintType)
class SANDSIMULATION_API ASandSimulationVolume final : public AActor
{
    GENERATED_BODY()

public:
    ASandSimulationVolume();
    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sand")
    TObjectPtr<USandSimulationComponent> SandSimulation;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sand|Preview")
    TObjectPtr<UBoxComponent> DomainBounds;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sand|Preview")
    TObjectPtr<UBoxComponent> InitialSandBounds;

private:
    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;
};

