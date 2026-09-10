#pragma once

#include "GameFramework/GameModeBase.h"
#include "SandPreviewGameMode.generated.h"

/** Minimal presentation world used while the surface renderer is under construction. */
UCLASS()
class SANDSIMULATION_API ASandPreviewGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASandPreviewGameMode();
    virtual void Tick(float DeltaSeconds) override;
    void CheckExposedFloor(const TArray<FVector>& Vertices, const TArray<int32>& Indices);
    void ContinueExploring();
    bool IsVictoryVisible() const { return bVictoryVisible; }
    bool HasWon() const { return bHasWon; }
    float GetVictoryCountdown() const;

protected:
    virtual void BeginPlay() override;
private:
    float FirstExposureTime = -1.0f;
    float LastExposureCheck = -1.0f;
    TArray<uint8> InitialFloorCoverage;
    bool bHasWon = false;
    bool bVictoryVisible = false;
};
