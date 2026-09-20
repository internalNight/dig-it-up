#pragma once

#include "CoreMinimal.h"

namespace Sand::Lunar
{
#include "LunarNobile03Height.inl"

inline float SmoothStep(const float A, const float B, const float X)
{
    const float T = FMath::Clamp((X - A) / FMath::Max(B - A, UE_SMALL_NUMBER), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

inline float CraterOffsetMeters(
    const FVector2f Position,
    const FVector2f Center,
    const float Radius,
    const float Depth,
    const float RimHeight)
{
    const float R = (Position - Center).Size() / FMath::Max(Radius, 0.01f);
    float Bowl = 0.0f;
    if (R < 0.82f)
    {
        const float T = R / 0.82f;
        Bowl = -Depth * FMath::Square(1.0f - T * T);
    }
    const float RimDistance = (R - 1.0f) / 0.13f;
    const float Rim = RimHeight * FMath::Exp(-RimDistance * RimDistance);
    const float Ejecta = R > 1.0f && R < 2.8f
        ? 0.12f * RimHeight * (1.0f - SmoothStep(1.0f, 2.8f, R)) / FMath::Max(R * R, 1.0f)
        : 0.0f;
    return Bowl + Rim + Ejecta;
}

/**
 * Playable granular surface. This is a compact composite of mare-like lowland,
 * highland undulation and small impact craters; it is not a literal geodetic site.
 */
inline float ActiveSurfaceHeightMeters(
    const float X,
    const float Y,
    const float BaseDepthMeters,
    const float WidthMeters)
{
    const float Half = FMath::Max(0.5f * WidthMeters, 0.5f);
    const float NX = FMath::Clamp(X / Half, -1.0f, 1.0f);
    const float NY = FMath::Clamp(Y / Half, -1.0f, 1.0f);
    const float MareToHighland = SmoothStep(-0.45f, 0.70f, NX);
    float Height = BaseDepthMeters - 0.10f + 0.24f * MareToHighland;
    Height += 0.035f * FMath::Sin(2.7f * NX + 0.8f * NY);
    Height += 0.025f * FMath::Sin(5.1f * NY - 1.3f * NX);
    Height += CraterOffsetMeters(FVector2f(X, Y), FVector2f(-2.0f, 1.45f), 1.15f, 0.22f, 0.075f);
    Height += CraterOffsetMeters(FVector2f(X, Y), FVector2f(1.65f, -1.55f), 0.72f, 0.13f, 0.045f);
    Height += CraterOffsetMeters(FVector2f(X, Y), FVector2f(2.65f, 2.20f), 0.42f, 0.07f, 0.025f);
    return FMath::Clamp(Height, FMath::Max(0.45f, BaseDepthMeters - 0.30f), BaseDepthMeters + 0.32f);
}

inline float SampleNobile03ElevationMeters(const float U, const float V)
{
    const float X = FMath::Clamp(U, 0.0f, 1.0f) * (Nobile03HeightResolution - 1);
    const float Y = FMath::Clamp(V, 0.0f, 1.0f) * (Nobile03HeightResolution - 1);
    const int32 X0 = FMath::FloorToInt(X);
    const int32 Y0 = FMath::FloorToInt(Y);
    const int32 X1 = FMath::Min(X0 + 1, Nobile03HeightResolution - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, Nobile03HeightResolution - 1);
    const auto Decode = [](const int32 SampleX, const int32 SampleY)
    {
        const float Q = Nobile03HeightSamples[SampleX + SampleY * Nobile03HeightResolution] / 65535.0f;
        return FMath::Lerp(Nobile03MinimumElevationMeters, Nobile03MaximumElevationMeters, Q);
    };
    return FMath::Lerp(
        FMath::Lerp(Decode(X0, Y0), Decode(X1, Y0), X - X0),
        FMath::Lerp(Decode(X0, Y1), Decode(X1, Y1), X - X0),
        Y - Y0);
}

/** Real LROC relief plus a clearly documented gameplay composite of mare and craters. */
inline float MacroSurfaceHeightMeters(
    const float X,
    const float Y,
    const float BaseDepthMeters,
    const float ActiveWidthMeters)
{
    const float U = X / Nobile03GroundSizeMeters + 0.5f;
    const float V = Y / Nobile03GroundSizeMeters + 0.5f;
    const float SourceCenter = SampleNobile03ElevationMeters(0.5f, 0.5f);
    const float SourceRelief = SampleNobile03ElevationMeters(U, V) - SourceCenter;
    float Height = BaseDepthMeters + SourceRelief;

    // A dark, smoother mare-like basin makes a useful visual/gameplay contrast.
    const float MareRadius = FVector2f(X + 250.0f, Y + 35.0f).Size();
    const float MareBlend = 1.0f - SmoothStep(135.0f, 245.0f, MareRadius);
    Height = FMath::Lerp(Height, BaseDepthMeters - 13.0f + 0.16f * SourceRelief, MareBlend);

    Height += 10.0f * SmoothStep(80.0f, 360.0f, X);
    const FVector2f P(X, Y);
    Height += CraterOffsetMeters(P, FVector2f(-145.0f, 105.0f), 58.0f, 13.0f, 4.5f);
    Height += CraterOffsetMeters(P, FVector2f(155.0f, -120.0f), 86.0f, 21.0f, 7.0f);
    Height += CraterOffsetMeters(P, FVector2f(250.0f, 185.0f), 34.0f, 7.0f, 2.4f);
    Height += CraterOffsetMeters(P, FVector2f(-315.0f, -175.0f), 28.0f, 5.0f, 1.8f);
    Height += CraterOffsetMeters(P, FVector2f(-13.0f, 9.0f), 5.2f, 1.15f, 0.42f);

    // Blend the real-data terrain into the MPM boundary so the central solver
    // reads as one continuous lunar surface instead of another square box.
    const float HalfActive = 0.5f * ActiveWidthMeters;
    const float EdgeX = FMath::Clamp(X, -HalfActive, HalfActive);
    const float EdgeY = FMath::Clamp(Y, -HalfActive, HalfActive);
    const float ActiveEdge = ActiveSurfaceHeightMeters(EdgeX, EdgeY, BaseDepthMeters, ActiveWidthMeters);
    const float DistanceFromCore = FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - HalfActive;
    const float MacroBlend = SmoothStep(0.0f, 10.0f, DistanceFromCore);
    return FMath::Lerp(ActiveEdge, Height, MacroBlend);
}

inline bool IsMareRegion(const float X, const float Y)
{
    return FVector2f(X + 250.0f, Y + 35.0f).Size() < 220.0f;
}
}
