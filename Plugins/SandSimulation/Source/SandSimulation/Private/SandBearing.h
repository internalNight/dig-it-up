#pragma once
#include "CoreMinimal.h"
namespace Sand::Machine {
// Height of bulk material connected to the sandbox floor. Sparse airborne
// samples must not turn into an elevated bearing surface under a track.
// This is a density envelope for the existing support model, not a pressure solve.
inline bool ConnectedBearingHeight(const TArray<float>& HeightsCm,float SpacingCm,float AreaCm2,float& HeightCm) {
    if(SpacingCm<=0 || HeightsCm.IsEmpty()) return false;
    TArray<int32> Counts; Counts.Init(0,256);
    for(float H:HeightsCm) {
        const int32 B=FMath::FloorToInt(H/SpacingCm);
        if(B>=0 && B<Counts.Num()) ++Counts[B];
    }
    const float Minimum=.35f*AreaCm2/FMath::Square(SpacingCm);
    int32 Last=-1;
    for(int32 I=0;I<Counts.Num();++I) {
        const int32 Required=FMath::CeilToInt(FMath::Max(2.f,Minimum));
        if(Counts[I]>=Required) { Last=I; continue; }
        // MPM samples may leave one under-populated vertical bin inside an
        // otherwise continuous bulk. Bridge only that single sampling-scale
        // gap; two weak bins still represent a real void and stop support.
        if(I+1<Counts.Num() && Counts[I+1]>=Required && Counts[I]+Counts[I+1]>=2*Required) {
            Last=I;
            continue;
        }
        break;
    }
    if(Last<0) return false;
    HeightCm=(Last+1)*SpacingCm;
    return true;
}
}
