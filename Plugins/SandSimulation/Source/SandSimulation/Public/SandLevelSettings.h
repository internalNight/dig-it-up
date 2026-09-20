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
    UPROPERTY(Config, EditAnywhere, Category="Level", meta=(ClampMin="5.0", ClampMax="25.0", Units="m"))
    float ActiveWidthMeters = 15.0f;

    /** Total driveable lunar field streamed through the local MPM window. */
    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="20.0", ClampMax="200.0", Units="m"))
    float LunarPlayableWidthMeters = 100.0f;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="256.0", ClampMax="4096.0", Units="m"))
    float LunarLandscapeSizeMeters = 2048.0f;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="33", ClampMax="257"))
    int32 LunarLandscapeResolution = 257;

    /** Low-cost non-colliding outer terrain used only to carry the lunar horizon. */
    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="2048.0", ClampMax="16384.0", Units="m"))
    float LunarHorizonSizeMeters = 8192.0f;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="33", ClampMax="257"))
    int32 LunarHorizonResolution = 129;

    UPROPERTY(Config, EditAnywhere, Category="Lunar World", meta=(ClampMin="0", ClampMax="320"))
    int32 LunarRockCount = 140;

    /** Diameter of the buried faceted specimen; deliberately below half the bucket width. */
    UPROPERTY(Config, EditAnywhere, Category="Lunar Mission", meta=(ClampMin="0.08", ClampMax="0.32", Units="m"))
    float LunarGemDiameterMeters = 0.22f;

    /** Vertical distance from the undisturbed surface to the specimen centre. */
    UPROPERTY(Config, EditAnywhere, Category="Lunar Mission", meta=(ClampMin="0.12", ClampMax="0.8", Units="m"))
    float LunarGemBurialDepthMeters = 0.30f;

    UPROPERTY(Config, EditAnywhere, Category="Lunar Mission", meta=(ClampMin="1.0", ClampMax="15.0", Units="s"))
    float LunarDetectorDurationSeconds = 5.0f;

    UPROPERTY(Config, EditAnywhere, Category="Victory", meta=(ClampMin="5", ClampMax="50"))
    float ExposedSideCentimeters = 10.0f;
    UPROPERTY(Config, EditAnywhere, Category="Victory", meta=(ClampMin="0", ClampMax="10"))
    float VictoryDelaySeconds = 3.0f;
};
