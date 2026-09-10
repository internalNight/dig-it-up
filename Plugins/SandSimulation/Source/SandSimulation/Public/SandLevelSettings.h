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
    UPROPERTY(Config, EditAnywhere, Category="Level", meta=(ClampMin="0.5", ClampMax="2.5"))
    float SandDepthMeters = 1.5f;
    UPROPERTY(Config, EditAnywhere, Category="Victory", meta=(ClampMin="5", ClampMax="50"))
    float ExposedSideCentimeters = 10.0f;
    UPROPERTY(Config, EditAnywhere, Category="Victory", meta=(ClampMin="0", ClampMax="10"))
    float VictoryDelaySeconds = 3.0f;
};
