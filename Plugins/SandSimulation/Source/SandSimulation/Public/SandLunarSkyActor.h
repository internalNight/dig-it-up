#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandLunarSkyActor.generated.h"

class UProceduralMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Camera-following celestial shell with physically based angular scales. */
UCLASS()
class SANDSIMULATION_API ASandLunarSkyActor final : public AActor
{
    GENERATED_BODY()

public:
    ASandLunarSkyActor();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(const FVector& DirectionToSun);

protected:
    virtual void BeginPlay() override;

private:
    void BuildSky();
    void BuildEarth();
    void BuildSun();
    void BuildStars();
    void BuildSatellite();
    void UpdateSatellite();
    UMaterialInstanceDynamic* MakeCelestialMaterial(
        const FLinearColor& Tint, float ExposureGain);

    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> EarthMesh;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> SunMesh;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> StarMesh;

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> SatelliteMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> CelestialBaseMaterial;

    FVector SunDirection = FVector(0.52f,-0.83f,0.19f);
    // Nobile/Mons Mouton is a south-polar setting, so Earth sits low and nearly
    // fixed rather than hanging at a terrestrial-sky zenith.
    FVector EarthDirection = FVector(0.524f,-0.840f,0.139f);
    float SatelliteOrbitPhaseRadians = 0.20f;
    bool bLoggedVisualTestProjection = false;
};
