#include "SandSimulationVolume.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "SandSimulationComponent.h"

ASandSimulationVolume::ASandSimulationVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    DomainBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("DomainBounds"));
    DomainBounds->SetupAttachment(SceneRoot);
    DomainBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DomainBounds->SetHiddenInGame(true);
    DomainBounds->ShapeColor = FColor(70, 160, 255);
    DomainBounds->SetLineThickness(2.0f);

    InitialSandBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InitialSandBounds"));
    InitialSandBounds->SetupAttachment(SceneRoot);
    InitialSandBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InitialSandBounds->SetHiddenInGame(true);
    InitialSandBounds->ShapeColor = FColor(224, 164, 78);
    InitialSandBounds->SetLineThickness(3.0f);

    SandSimulation = CreateDefaultSubobject<USandSimulationComponent>(TEXT("SandSimulation"));
}

void ASandSimulationVolume::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    constexpr double UnrealUnitsPerMeter = 100.0;
    const FVector DomainSizeUU = SandSimulation->SolverParameters.DomainSizeMeters * UnrealUnitsPerMeter;
    const double SandDepthUU = SandSimulation->SolverParameters.InitialSandDepthMeters * UnrealUnitsPerMeter;

    DomainBounds->SetBoxExtent(0.5 * DomainSizeUU);
    DomainBounds->SetRelativeLocation(FVector(0.0, 0.0, 0.5 * DomainSizeUU.Z));

    InitialSandBounds->SetBoxExtent(FVector(0.5 * DomainSizeUU.X, 0.5 * DomainSizeUU.Y, 0.5 * SandDepthUU));
    InitialSandBounds->SetRelativeLocation(FVector(0.0, 0.0, 0.5 * SandDepthUU));
}
