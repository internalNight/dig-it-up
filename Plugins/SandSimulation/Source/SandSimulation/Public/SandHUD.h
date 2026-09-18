#pragma once

#include "GameFramework/HUD.h"
#include "SandHUD.generated.h"

/** Persistent first-playable controls and vehicle status overlay. */
UCLASS()
class SANDSIMULATION_API ASandHUD final : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
    bool UsesMobileControls() const;
    void PollMobileControls();
    void GetMobileControls(float& Throttle, float& Steering, float& Boom,
        float& Stick, float& Bucket) const;
    FVector2D GetMobileLookDelta() const { return LookDelta; }

private:
    void DrawMobileControls();
    void DrawPerformanceMetrics();
    struct FTouchCapture
    {
        bool bDown = false;
        int8 Control = -1; // 0: drive, 1: boom, 2: stick, 3: bucket, 4: camera
        FVector2D Start = FVector2D::ZeroVector;
        FVector2D Position = FVector2D::ZeroVector;
    };
    FTouchCapture Touches[10];
    FVector2D DriveInput = FVector2D::ZeroVector;
    FVector2D LookDelta = FVector2D::ZeroVector; // fraction of viewport height this frame
    bool bDesktopTouchInputConfigured = false;
    float LeverInput[3] = {0.0f, 0.0f, 0.0f};
    TWeakObjectPtr<class ASandCollapseSurfacePreviewActor> PerformanceActor;
    double PerformanceWindowStartSeconds = 0.0;
    uint64 PerformanceWindowStartSimulationFrames = 0;
    uint64 PerformanceWindowStartSurfaceFrames = 0;
    int32 PerformanceWindowRenderFrames = 0;
    float DisplayRenderFps = 0.0f;
    float DisplaySimulationHz = 0.0f;
    float DisplaySurfaceHz = 0.0f;

    // Canvas requires a live UFont even when a Slate composite font is supplied.
    UPROPERTY(Transient)
    TObjectPtr<class UFont> VictoryFont;

    bool bShowControls = true;
};
