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

private:
    // Canvas requires a live UFont even when a Slate composite font is supplied.
    UPROPERTY(Transient)
    TObjectPtr<class UFont> VictoryFont;

    bool bShowControls = true;
};
