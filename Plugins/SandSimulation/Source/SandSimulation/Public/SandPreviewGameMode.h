#pragma once

#include "GameFramework/GameModeBase.h"
#include "SandPreviewGameMode.generated.h"

class ASandLunarGemActor;

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
    void SelectVehicle(bool bRoadheader);
    bool IsSelectingVehicle() const { return bSelectingVehicle; }
    bool IsVictoryVisible() const { return bVictoryVisible; }
    bool HasWon() const { return bHasWon; }
    float GetVictoryCountdown() const;
    void ActivateMineralDetector(const FVector& SourceLocation);
    bool IsMineralDetectorActive() const;
    float GetMineralDetectorRemainingSeconds() const;
    bool HasLunarGem() const { return LunarGem.IsValid(); }
    FVector GetLunarGemLocation() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    bool bChangedFixedClock=false, bPreviousFixedClock=false;
    double PreviousFixedDelta=0;
    float FirstExposureTime = -1.0f;
    float LastExposureCheck = -1.0f;
    TArray<uint8> InitialFloorCoverage;
    bool bSelectingVehicle = true;
    bool bHasWon = false;
    bool bVictoryVisible = false;
    float DetectorActiveUntil = -1.0f;
    TWeakObjectPtr<ASandLunarGemActor> LunarGem;
};
