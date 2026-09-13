#pragma once
#include "CoreMinimal.h"
namespace Sand::Machine {
// Finite planar contact budget. A first contact model, not a calibrated
// Bekker/Janosi track model. Forces in N, speeds in m/s, mass in kg.
inline FVector2f TractionForce(float Mass,float Normal,float Mu,float Target,
    FVector2f Velocity,float DriveLimit,float Response=.25f,FVector2f External=FVector2f::ZeroVector) {
    if(Normal<=0 || Mass<=0) return FVector2f::ZeroVector;
    FVector2f F(FMath::Clamp(Mass*(Target-Velocity.X)/Response-External.X,-DriveLimit,DriveLimit),
        -Mass*Velocity.Y/Response-External.Y);
    const float Budget=FMath::Max(0.f,Mu*Normal);
    if(F.Size()>Budget) F*=Budget/F.Size();
    return F;
}
inline float FeedFraction(float Draft,float Torque) {
    return FMath::Clamp(FMath::Min((65-FMath::Abs(Draft))/40.f,(180-FMath::Abs(Torque))/100.f),0.f,1.f);
}
}
