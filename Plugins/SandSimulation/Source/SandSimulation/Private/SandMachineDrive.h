#pragma once
#include "CoreMinimal.h"

namespace Sand::Machine
{
// Backward-Euler velocity servo with a finite torque/force limit. Inertia may
// include the reflected inertia of the geared drive. Impulse is exerted ON sand.
inline float DrivenSpeed(float Old, float Target, float SandImpulse, float Dt,
    float Inertia, float Gain, float Limit)
{
    const float Free = (Inertia*Old + Dt*Gain*Target - SandImpulse)/(Inertia+Dt*Gain);
    const float Effort = FMath::Clamp(Gain*(Target-Free),-Limit,Limit);
    return (Inertia*Old+Dt*Effort-SandImpulse)/Inertia;
}
inline float BrakeSpeed(float Speed, float Dt, float Inertia, float Effort)
{
    return FMath::Sign(Speed)*FMath::Max(0.0f,FMath::Abs(Speed)-Effort*Dt/Inertia);
}
}
