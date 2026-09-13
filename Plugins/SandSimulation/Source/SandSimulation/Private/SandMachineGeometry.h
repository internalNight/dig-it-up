#pragma once
#include "SandMPMSolver.h"

namespace Sand::Machine {
inline float PointPenetrationDepth(const FVector3f& P,const MPM::FToolOrientedBoxState& C)
{
    const auto D=P-C.CenterMeters;
    const FVector3f L(FVector3f::DotProduct(D,C.AxisX),FVector3f::DotProduct(D,C.AxisY),FVector3f::DotProduct(D,C.AxisZ));
    if(C.Shape==1) return FMath::Max(0.f,FMath::Min(C.HalfExtentsMeters.X-FMath::Sqrt(L.X*L.X+L.Z*L.Z),C.HalfExtentsMeters.Y-FMath::Abs(L.Y)));
    return FMath::Max(0.f,FMath::Min3(C.HalfExtentsMeters.X-FMath::Abs(L.X),C.HalfExtentsMeters.Y-FMath::Abs(L.Y),C.HalfExtentsMeters.Z-FMath::Abs(L.Z)));
}
// SAT overlap depth of two solid contact boxes. Connection pairs (hub/flight)
// must be excluded by the caller; independent moving solids must not overlap.
inline float BoxOverlapDepth(const MPM::FToolOrientedBoxState& A,const MPM::FToolOrientedBoxState& B)
{
    const FVector3f Ax[3]={A.AxisX,A.AxisY,A.AxisZ}, Bx[3]={B.AxisX,B.AxisY,B.AxisZ};
    TArray<FVector3f,TInlineAllocator<15>> Directions;
    for(int I=0;I<3;++I) { Directions.Add(Ax[I]); Directions.Add(Bx[I]); }
    for(int I=0;I<3;++I) for(int J=0;J<3;++J) Directions.Add(FVector3f::CrossProduct(Ax[I],Bx[J]));
    float Depth=1.e9f;
    for(auto N:Directions) {
        if(!N.Normalize()) continue;
        float RA=0,RB=0;
        for(int I=0;I<3;++I) { RA+=A.HalfExtentsMeters[I]*FMath::Abs(FVector3f::DotProduct(Ax[I],N)); RB+=B.HalfExtentsMeters[I]*FMath::Abs(FVector3f::DotProduct(Bx[I],N)); }
        const float Overlap=RA+RB-FMath::Abs(FVector3f::DotProduct(B.CenterMeters-A.CenterMeters,N));
        if(Overlap<=0) return 0;
        Depth=FMath::Min(Depth,Overlap);
    }
    return Depth;
}
}
