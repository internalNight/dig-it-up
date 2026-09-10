#pragma once
#include "CoreMinimal.h"

namespace Sand::Goal
{
// Sample vertical visibility of the red floor from the actual 3D surface.
// Initial seam pixels can be excluded without forbidding excavation near walls.
inline bool FindOpening(const TArray<FVector>& Vertices, const TArray<int32>& Indices,
    float RequiredSideCm, FVector2D& OutCenter, TArray<uint8>* InitialCoverage = nullptr)
{
    constexpr float Min = -250.0f, Step = 2.5f;
    constexpr int32 N = 200;
    TArray<uint8> Blocked;
    Blocked.Init(0, N * N);
    for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
    {
        const FVector& A = Vertices[Indices[I]];
        const FVector& B = Vertices[Indices[I+1]];
        const FVector& C = Vertices[Indices[I+2]];
        if (FMath::Max3(A.Z,B.Z,C.Z) <= 0.2) { continue; }
        const double Denom = (B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
        if (FMath::Abs(Denom) < 1.e-8) { continue; }
        const int32 X0 = FMath::Clamp(FMath::FloorToInt((FMath::Min3(A.X,B.X,C.X)-Min)/Step),0,N-1);
        const int32 X1 = FMath::Clamp(FMath::FloorToInt((FMath::Max3(A.X,B.X,C.X)-Min)/Step),0,N-1);
        const int32 Y0 = FMath::Clamp(FMath::FloorToInt((FMath::Min3(A.Y,B.Y,C.Y)-Min)/Step),0,N-1);
        const int32 Y1 = FMath::Clamp(FMath::FloorToInt((FMath::Max3(A.Y,B.Y,C.Y)-Min)/Step),0,N-1);
        for (int32 Y=Y0; Y<=Y1; ++Y) for (int32 X=X0; X<=X1; ++X)
        {
            if (Blocked[X+Y*N]) { continue; }
            const double PX = Min+(X+0.5)*Step, PY = Min+(Y+0.5)*Step;
            const double U = ((B.Y-C.Y)*(PX-C.X)+(C.X-B.X)*(PY-C.Y))/Denom;
            const double V = ((C.Y-A.Y)*(PX-C.X)+(A.X-C.X)*(PY-C.Y))/Denom;
            if (U>=-1.e-6 && V>=-1.e-6 && U+V<=1.000001 &&
                U*A.Z+V*B.Z+(1-U-V)*C.Z > 0.2) { Blocked[X+Y*N]=1; }
        }
    }
    if (InitialCoverage && InitialCoverage->IsEmpty())
    {
        *InitialCoverage = Blocked;
        return false;
    }
    // Largest empty square dynamic program: disconnected specks cannot add up to a win.
    const int32 Required = FMath::Clamp(FMath::CeilToInt(RequiredSideCm/Step),1,N);
    TArray<int32> Squares;
    Squares.Init(0,N*N);
    for (int32 Y=0; Y<N; ++Y) for (int32 X=0; X<N; ++X)
    {
        if (Blocked[X+Y*N] || (InitialCoverage && !(*InitialCoverage)[X+Y*N])) { continue; }
        const int32 Size = (X>0 && Y>0) ? 1+FMath::Min3(Squares[X-1+Y*N],
            Squares[X+(Y-1)*N],Squares[X-1+(Y-1)*N]) : 1;
        Squares[X+Y*N]=Size;
        if (Size>=Required)
        {
            OutCenter = FVector2D(Min+(X+1-Required*0.5f)*Step,Min+(Y+1-Required*0.5f)*Step);
            return true;
        }
    }
    return false;
}
}
