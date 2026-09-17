#pragma once

#include "CoreMinimal.h"

namespace Sand::Machine
{
// Closed stadium chain path, SI units. Top run moves from front (+X) to rear.
// Blades stay transverse on the straight runs and rotate around both sprockets.
constexpr float Front = 0.32f;
constexpr float Rear = -0.62f;
constexpr float ChainRadius = 0.065f;
constexpr float Top = 0.045f;
constexpr float Run = Front - Rear;
constexpr float Loop = 2.0f * Run + 2.0f * PI * ChainRadius;
constexpr int32 BladeCount = 10;
constexpr int32 DrumCount = 8;
inline void ChainPose(float Distance, FVector3f& Position, FVector3f& Tangent, float FrontPosition=Front)
{
    const float PathRun=FrontPosition-Rear, PathLoop=2*PathRun+2*PI*ChainRadius;
    float S = FMath::Fmod(Distance, PathLoop);
    if (S < 0) S += PathLoop;
    if (S < PathRun)
    {
        Position = FVector3f(FrontPosition-S, 0, Top);
        Tangent = FVector3f(-1,0,0);
    }
    else if (S < PathRun + PI*ChainRadius)
    {
        const float A = (S-PathRun)/ChainRadius;
        Position = FVector3f(Rear-ChainRadius*FMath::Sin(A),0,Top-ChainRadius+ChainRadius*FMath::Cos(A));
        Tangent = FVector3f(-FMath::Cos(A),0,-FMath::Sin(A));
    }
    else if (S < 2*PathRun + PI*ChainRadius)
    {
        Position = FVector3f(Rear+S-PathRun-PI*ChainRadius,0,Top-2*ChainRadius);
        Tangent = FVector3f(1,0,0);
    }
    else
    {
        const float A = (S-2*PathRun-PI*ChainRadius)/ChainRadius;
        Position = FVector3f(FrontPosition+ChainRadius*FMath::Sin(A),0,Top-ChainRadius-ChainRadius*FMath::Cos(A));
        Tangent = FVector3f(FMath::Cos(A),0,FMath::Sin(A));
    }
}
}
