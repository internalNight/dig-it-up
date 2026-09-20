#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandLunarGemActor.generated.h"

class UProceduralMeshComponent;

/** A small, rigid, faceted golden specimen hidden inside the lunar regolith. */
UCLASS()
class SANDSIMULATION_API ASandLunarGemActor final : public AActor
{
    GENERATED_BODY()

public:
    ASandLunarGemActor();
    void Configure(float DiameterMeters);
    float GetTopWorldZ() const;
    float GetRadiusCentimeters() const { return RadiusCentimeters; }

protected:
    virtual void BeginPlay() override;

private:
    void BuildGemMesh();

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> GemMesh;

    float RadiusCentimeters = 11.0f;
};
