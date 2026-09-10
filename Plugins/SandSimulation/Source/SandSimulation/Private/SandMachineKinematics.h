#pragma once

#include "CoreMinimal.h"

namespace Sand::Machine
{
// Closed stadium chain path, SI units. Top run moves from front (+X) to rear.
// Blades stay transverse on the straight runs and rotate around both sprockets.
constexpr float Front = 0.32f;
constexpr float Rear = -0.62f;
constexpr float Radius = 0.065f;
constexpr float Top = 0.045f;
constexpr float Run = Front - Rear;
constexpr float Loop = 2.0f * Run + 2.0f * PI * Radius;
constexpr int32 BladeCount = 10;
constexpr int32 DrumCount = 8;
inline void ChainPose(float Distance, FVector3f& Position, FVector3f& Tangent)
{
    float S = FMath::Fmod(Distance, Loop);
    if (S < 0) S += Loop;
    if (S < Run)
    {
        Position = FVector3f(Front-S, 0, Top);
        Tangent = FVector3f(-1,0,0);
    }
    else if (S < Run + PI*Radius)
    {
        const float A = (S-Run)/Radius;
        Position = FVector3f(Rear-Radius*FMath::Sin(A),0,Top-Radius+Radius*FMath::Cos(A));
        Tangent = FVector3f(-FMath::Cos(A),0,-FMath::Sin(A));
    }
    else if (S < 2*Run + PI*Radius)
    {
        Position = FVector3f(Rear+S-Run-PI*Radius,0,Top-2*Radius);
        Tangent = FVector3f(1,0,0);
    }
    else
    {
        const float A = (S-2*Run-PI*Radius)/Radius;
        Position = FVector3f(Front+Radius*FMath::Sin(A),0,Top-Radius-Radius*FMath::Cos(A));
        Tangent = FVector3f(FMath::Cos(A),0,FMath::Sin(A));
    }
}
}
