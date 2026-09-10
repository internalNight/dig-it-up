#pragma once

#include "Components/ActorComponent.h"
#include "SandSimulationTypes.h"
#include "SandSimulationComponent.generated.h"

/** Runtime owner for one self-contained sand solve. GPU resources are attached here in Gate 1. */
UCLASS(ClassGroup = (Sand), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class SANDSIMULATION_API USandSimulationComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    USandSimulationComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand")
    FSandSolverParameters SolverParameters;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand")
    FSandMaterialParameters MaterialParameters;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sand|Runtime")
    bool bStartSimulationOnBeginPlay = true;

    UFUNCTION(BlueprintPure, Category = "Sand|Diagnostics")
    FIntVector GetGridCellResolution() const;

    UFUNCTION(BlueprintPure, Category = "Sand|Diagnostics")
    int64 GetInitialParticleCount() const;

    UFUNCTION(BlueprintPure, Category = "Sand|Diagnostics")
    bool AreParametersValid(FString& OutReason) const;

protected:
    virtual void BeginPlay() override;
};

