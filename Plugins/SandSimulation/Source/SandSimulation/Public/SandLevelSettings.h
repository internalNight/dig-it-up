#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SandLevelSettings.generated.h"

/** First playable level settings. Restart the level after changing config. */
UCLASS(Config=Game, DefaultConfig)
class SANDSIMULATION_API USandLevelSettings : public UObject
{
    GENERATED_BODY()
public:
    /** Default presentation: a large LROC-informed lunar landscape around the active MPM patch. */
    UPROPERTY(Config, EditAnywhere, Category="Level")
    bool bLunarWorld = true;

    UPROPERTY(Config, EditAnywhere, Category="Level", meta=(ClampMin="0.5", ClampMax="2.5"))
    float SandDepthMeters = 1.2f;

    /** Width of the fully movable square MPM terrain at the centre of the lunar world. */
    UPROPERTY(Config, EditAnywhere, Category="Level", meta=(ClampMin="5.0", ClampMax="14.0", Units="m"))
    float ActiveWidthMeters = 10.0f;

    /** Total driveable lunar field streamed through the local MPM window. */
    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="20.0", ClampMax="200.0", Units="m"))
    float LunarPlayableWidthMeters = 100.0f;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="256.0", ClampMax="4096.0", Units="m"))
    float LunarLandscapeSizeMeters = 2048.0f;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="33", ClampMax="257"))
    int32 LunarLandscapeResolution = 257;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="0", ClampMax="320"))
    int32 LunarRockCount = 140;
    UPROPERTY(Config, EditAnywhere, Category="Victory", meta=(ClampMin="5", ClampMax="50"))
    float ExposedSideCentimeters = 10.0f;
    UPROPERTY(Config, EditAnywhere, Category="Victory", meta=(ClampMin="0", ClampMax="10"))
    float VictoryDelaySeconds = 3.0f;
};
