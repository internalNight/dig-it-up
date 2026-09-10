#include "SandSimulationComponent.h"

#include "SandSimulationModule.h"

USandSimulationComponent::USandSimulationComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FIntVector USandSimulationComponent::GetGridCellResolution() const
{
    return SolverParameters.GetGridCellResolution();
}

int64 USandSimulationComponent::GetInitialParticleCount() const
{
    return SolverParameters.GetInitialParticleCount();
}

bool USandSimulationComponent::AreParametersValid(FString& OutReason) const
{
    if (!SolverParameters.IsValid(&OutReason))
    {
        return false;
    }
    return MaterialParameters.IsValid(&OutReason);
}

void USandSimulationComponent::BeginPlay()
{
    Super::BeginPlay();

    FString FailureReason;
    if (!AreParametersValid(FailureReason))
    {
        UE_LOG(LogTemp, Error, TEXT("Sand simulation parameters are invalid on %s: %s"), *GetPathName(), *FailureReason);
        return;
    }

    if (bStartSimulationOnBeginPlay)
    {
        const FIntVector Resolution = GetGridCellResolution();
        UE_LOG(
            LogTemp,
            Display,
            TEXT("Sand domain prepared: cells=%dx%dx%d, particles=%lld, fixed-step=%.3f ms"),
            Resolution.X,
            Resolution.Y,
            Resolution.Z,
            GetInitialParticleCount(),
            1000.0f * SolverParameters.GetFixedDeltaSeconds());
    }
}

