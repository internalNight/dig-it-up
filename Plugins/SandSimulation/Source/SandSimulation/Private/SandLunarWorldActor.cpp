#include "SandLunarWorldActor.h"

#include "SandLevelSettings.h"
#include "SandLunarTerrain.h"
#include "SandCollapseSurfacePreviewActor.h"
#include "SandExcavatorPawn.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
struct FMeshSectionData
{
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
};

FVector SurfaceNormal(
    const float X,
    const float Y,
    const float SampleStep,
    const float BaseDepth,
    const float ActiveWidth)
{
    const float DX = (Sand::Lunar::MacroSurfaceHeightMeters(X + SampleStep, Y, BaseDepth, ActiveWidth) -
        Sand::Lunar::MacroSurfaceHeightMeters(X - SampleStep, Y, BaseDepth, ActiveWidth)) / (2.0f * SampleStep);
    const float DY = (Sand::Lunar::MacroSurfaceHeightMeters(X, Y + SampleStep, BaseDepth, ActiveWidth) -
        Sand::Lunar::MacroSurfaceHeightMeters(X, Y - SampleStep, BaseDepth, ActiveWidth)) / (2.0f * SampleStep);
    return FVector(-DX, -DY, 1.0f).GetSafeNormal();
}

void AppendQuad(
    FMeshSectionData& Mesh,
    const float X0,
    const float Y0,
    const float X1,
    const float Y1,
    const float SampleStep,
    const float BaseDepth,
    const float ActiveWidth,
    const FLinearColor Color,
    const bool bMacroColor = false)
{
    constexpr float UnitsPerMeter = 100.0f;
    const FVector2f XY[4] = {{X0,Y0},{X1,Y0},{X1,Y1},{X0,Y1}};
    const int32 First = Mesh.Vertices.Num();
    for (const FVector2f Point : XY)
    {
        const float Height = Sand::Lunar::MacroSurfaceHeightMeters(Point.X, Point.Y, BaseDepth, ActiveWidth);
        Mesh.Vertices.Add(FVector(Point.X, Point.Y, Height) * UnitsPerMeter);
        Mesh.Normals.Add(SurfaceNormal(Point.X, Point.Y, SampleStep, BaseDepth, ActiveWidth));
        Mesh.UVs.Add(FVector2D(Point.X, Point.Y) * 0.018f);
        if (bMacroColor)
        {
            const float MareBlend = 1.0f - Sand::Lunar::SmoothStep(175.0f,245.0f,
                FVector2f(Point.X + 250.0f,Point.Y + 35.0f).Size());
            const FLinearColor MareColor = FLinearColor::FromSRGBColor(FColor(72,75,82));
            const FLinearColor HighlandColor = FLinearColor::FromSRGBColor(FColor(126,128,134));
            Mesh.Colors.Add(FMath::Lerp(HighlandColor,MareColor,MareBlend));
        }
        else
        {
            const float Tone = 0.92f + 0.08f * FMath::PerlinNoise2D(
                FVector2D(Point.X + 11.7f,Point.Y - 3.2f) * 1.15f);
            Mesh.Colors.Add(Color * Tone);
        }
    }
    Mesh.Indices.Append({First, First + 2, First + 1, First, First + 3, First + 2});
}

void AppendHeightfieldChunk(
    FMeshSectionData& Mesh,
    const FIntPoint ChunkKey,
    const TArray<float>& Heights,
    const int32 Resolution,
    const float SpacingMeters,
    const float ChunkOriginMeters)
{
    if (Resolution < 2 || Heights.Num() != Resolution * Resolution)
    {
        return;
    }
    const float MinimumX = ChunkOriginMeters + ChunkKey.X *
        SpacingMeters * (Resolution - 1);
    const float MinimumY = ChunkOriginMeters + ChunkKey.Y *
        SpacingMeters * (Resolution - 1);
    const auto Height = [&Heights,Resolution](const int32 X,const int32 Y)
    {
        return Heights[FMath::Clamp(X,0,Resolution - 1) +
            FMath::Clamp(Y,0,Resolution - 1) * Resolution];
    };
    const auto Normal = [&Height,SpacingMeters](const int32 X,const int32 Y)
    {
        const float DX = (Height(X + 1,Y) - Height(X - 1,Y)) /
            (2.0f * SpacingMeters);
        const float DY = (Height(X,Y + 1) - Height(X,Y - 1)) /
            (2.0f * SpacingMeters);
        return FVector(-DX,-DY,1.0f).GetSafeNormal();
    };
    const FLinearColor RegolithColor(0.23f,0.24f,0.26f,1.0f);
    for (int32 Y = 0; Y < Resolution - 1; ++Y)
    {
        for (int32 X = 0; X < Resolution - 1; ++X)
        {
            const int32 First = Mesh.Vertices.Num();
            const FIntPoint Corners[4] = {{X,Y},{X + 1,Y},{X + 1,Y + 1},{X,Y + 1}};
            for (const FIntPoint Corner : Corners)
            {
                const float WorldX = MinimumX + Corner.X * SpacingMeters;
                const float WorldY = MinimumY + Corner.Y * SpacingMeters;
                const float WorldZ = Height(Corner.X,Corner.Y);
                Mesh.Vertices.Add(FVector(WorldX,WorldY,WorldZ) * 100.0f);
                Mesh.Normals.Add(Normal(Corner.X,Corner.Y));
                Mesh.UVs.Add(FVector2D(WorldX,WorldY) * 0.018f);
                const float Tone = 0.94f + 0.06f * FMath::PerlinNoise2D(
                    FVector2D(WorldX + 11.7f,WorldY - 3.2f) * 1.15f);
                Mesh.Colors.Add(RegolithColor * Tone);
            }
            Mesh.Indices.Append({First,First + 2,First + 1,First,First + 3,First + 2});
        }
    }
}

void UploadSection(UProceduralMeshComponent* Component, const int32 Section, FMeshSectionData&& Mesh, const bool Collision)
{
    Component->CreateMeshSection_LinearColor(
        Section, Mesh.Vertices, Mesh.Indices, Mesh.Normals, Mesh.UVs, Mesh.Colors,
        TArray<FProcMeshTangent>(), Collision, false);
}

void AppendAngularRock(
    FMeshSectionData& Mesh,
    FRandomStream& Random,
    const FVector& CenterCentimeters,
    const FVector& HalfExtentCentimeters,
    const FQuat& Rotation,
    const FLinearColor& BaseColor,
    TArray<FVector>* OutConvexVertices = nullptr)
{
    constexpr float Phi = 1.61803398875f;
    const FVector BaseVertices[12] =
    {
        {-1,Phi,0},{1,Phi,0},{-1,-Phi,0},{1,-Phi,0},
        {0,-1,Phi},{0,1,Phi},{0,-1,-Phi},{0,1,-Phi},
        {Phi,0,-1},{Phi,0,1},{-Phi,0,-1},{-Phi,0,1}
    };
    static const int32 Faces[20][3] =
    {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    FVector UnitVertices[12];
    float RadiusNoise[12];
    FVector RockVertices[12];
    for (int32 VertexIndex = 0; VertexIndex < 12; ++VertexIndex)
    {
        RadiusNoise[VertexIndex] = Random.FRandRange(0.78f, 1.18f);
        UnitVertices[VertexIndex] = BaseVertices[VertexIndex].GetSafeNormal();
        const FVector UnitVertex = UnitVertices[VertexIndex];
        const FVector Shaped = FVector(
            UnitVertex.X * HalfExtentCentimeters.X,
            UnitVertex.Y * HalfExtentCentimeters.Y,
            UnitVertex.Z * HalfExtentCentimeters.Z) * RadiusNoise[VertexIndex];
        RockVertices[VertexIndex] = CenterCentimeters + Rotation.RotateVector(Shaped);
        if (OutConvexVertices != nullptr)
        {
            OutConvexVertices->Add(RockVertices[VertexIndex]);
        }
    }
    const auto MakePoint = [&CenterCentimeters,&HalfExtentCentimeters,&Rotation](
        const FVector UnitDirection,const float Noise)
    {
        return CenterCentimeters + Rotation.RotateVector(FVector(
            UnitDirection.X * HalfExtentCentimeters.X,
            UnitDirection.Y * HalfExtentCentimeters.Y,
            UnitDirection.Z * HalfExtentCentimeters.Z) * Noise);
    };
    for (const int32* Face : Faces)
    {
        const FVector A = RockVertices[Face[0]];
        const FVector B = RockVertices[Face[1]];
        const FVector C = RockVertices[Face[2]];
        const FVector AB = MakePoint(
            (UnitVertices[Face[0]] + UnitVertices[Face[1]]).GetSafeNormal(),
            0.5f * (RadiusNoise[Face[0]] + RadiusNoise[Face[1]]));
        const FVector BC = MakePoint(
            (UnitVertices[Face[1]] + UnitVertices[Face[2]]).GetSafeNormal(),
            0.5f * (RadiusNoise[Face[1]] + RadiusNoise[Face[2]]));
        const FVector CA = MakePoint(
            (UnitVertices[Face[2]] + UnitVertices[Face[0]]).GetSafeNormal(),
            0.5f * (RadiusNoise[Face[2]] + RadiusNoise[Face[0]]));
        const FVector Triangles[4][3] =
        {
            {A,AB,CA},{AB,B,BC},{CA,BC,C},{AB,BC,CA}
        };
        for (const FVector* Triangle : Triangles)
        {
            const FVector Normal = FVector::CrossProduct(
                Triangle[1] - Triangle[0],Triangle[2] - Triangle[0]).GetSafeNormal();
            const int32 First = Mesh.Vertices.Num();
            const float Tone = Random.FRandRange(0.80f, 1.20f);
            const FLinearColor FaceColor(
                FMath::Min(1.0f, BaseColor.R * Tone),
                FMath::Min(1.0f, BaseColor.G * Tone),
                FMath::Min(1.0f, BaseColor.B * Tone), 1.0f);
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const FVector Vertex = Triangle[Corner];
                Mesh.Vertices.Add(Vertex);
                Mesh.Normals.Add(Normal);
                Mesh.UVs.Add(FVector2D(Vertex.X,Vertex.Y) * 0.01f);
                Mesh.Colors.Add(FaceColor);
            }
            Mesh.Indices.Append({First,First + 1,First + 2});
        }
    }
}
}

ASandLunarWorldActor::ASandLunarWorldActor()
{
    PrimaryActorTick.bCanEverTick = true;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    MacroTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MacroTerrain"));
    MacroTerrain->SetupAttachment(SceneRoot);
    MacroTerrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MacroTerrain->SetCastShadow(true);
    DistantTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DistantTerrain"));
    DistantTerrain->SetupAttachment(SceneRoot);
    DistantTerrain->SetRelativeLocation(FVector(0.0f,0.0f,-2.0f));
    DistantTerrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DistantTerrain->SetCastShadow(false);
    TransitionTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransitionTerrain"));
    TransitionTerrain->SetupAttachment(SceneRoot);
    // The analytic collar sits just below the live granular top.  The live
    // top now reaches the resident edge while only its closure walls are
    // culled, so this underlay cannot create an inner square intersection.
    TransitionTerrain->SetRelativeLocation(FVector(0.0f,0.0f,-4.0f));
    // The vehicle and tools always remain inside the following physical MPM
    // window. Collision on this visual collar only forced a 40k-triangle Chaos
    // recook at every shift and was a major hitch source.
    TransitionTerrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TransitionTerrain->SetCastShadow(false);
    CachedDeformationTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(
        TEXT("CachedDeformationTerrain"));
    CachedDeformationTerrain->SetupAttachment(SceneRoot);
    CachedDeformationTerrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // This proxy is swapped chunk-by-chunk. Let the continuous macro terrain
    // carry the large-scale shadow so a streamed proxy edge cannot flash a
    // square, physically implausible shadow during a window commit.
    CachedDeformationTerrain->SetCastShadow(false);
    TrackMarks = CreateDefaultSubobject<UProceduralMeshComponent>(
        TEXT("PersistentTrackMarks"));
    TrackMarks->SetupAttachment(SceneRoot);
    TrackMarks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TrackMarks->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded())
    {
        MacroTerrain->SetMaterial(0,VertexColorMaterial.Object);
        DistantTerrain->SetMaterial(0,VertexColorMaterial.Object);
        CachedDeformationTerrain->SetMaterial(0,VertexColorMaterial.Object);
    }
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> WindowedTerrainMaterial(
        TEXT("/Game/Materials/M_LunarTerrainMaskV2.M_LunarTerrainMaskV2"));
    UMaterialInterface* SharedTerrainMaterial = WindowedTerrainMaterial.Succeeded()
        ? WindowedTerrainMaterial.Object : VertexColorMaterial.Object;
    // A common world-space material prevents the resident MPM surface,
    // persisted deformation proxy and analytic distance field from reading as
    // three differently shaded square layers.
    MacroTerrain->SetMaterial(0,SharedTerrainMaterial);
    DistantTerrain->SetMaterial(0,SharedTerrainMaterial);
    TransitionTerrain->SetMaterial(0,SharedTerrainMaterial);
    CachedDeformationTerrain->SetMaterial(0,SharedTerrainMaterial);
    TrackMarks->SetMaterial(0,SharedTerrainMaterial);

    StaticRocks = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StaticLunarRocks"));
    StaticRocks->SetupAttachment(SceneRoot);
    StaticRocks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StaticRocks->SetCastShadow(true);
    if (VertexColorMaterial.Succeeded())
    {
        StaticRocks->SetMaterial(0,VertexColorMaterial.Object);
    }
}

void ASandLunarWorldActor::BeginPlay()
{
    Super::BeginPlay();
    BuildMacroTerrain();
    BuildDistantTerrain();
    BuildTransitionTerrain();
    BuildCachedDeformationTerrain();
    BuildRocks();
    for (TActorIterator<ASandCollapseSurfacePreviewActor> It(GetWorld()); It; ++It)
    {
        SandSurface = *It;
        break;
    }
}

void ASandLunarWorldActor::SetOverviewMode(const bool bEnabled)
{
    if (bOverviewMode == bEnabled)
    {
        return;
    }
    bOverviewMode = bEnabled;
    if (SandSurface.IsValid())
    {
        SandSurface->SetActorHiddenInGame(bOverviewMode);
    }
    CachedDeformationTerrain->SetVisibility(!bOverviewMode,true);
    UpdateTransitionVisibility();
    UE_LOG(LogTemp,Display,TEXT("LUNAR_OVERVIEW mode=%d liveSurfaceVisible=%d"),
        bOverviewMode ? 1 : 0,bOverviewMode ? 0 : 1);
}

void ASandLunarWorldActor::CacheDeformationChunk(
    const FIntPoint ChunkKey,
    TArray<float>&& HeightMeters,
    const int32 Resolution,
    const float SpacingMeters)
{
    if (Resolution < 2 || HeightMeters.Num() != Resolution * Resolution ||
        SpacingMeters <= 0.0f)
    {
        return;
    }
    CachedDeformationResolution = Resolution;
    CachedDeformationSpacingMeters = SpacingMeters;
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    constexpr float ChunkWidth = 5.0f;
    const float ChunkOrigin = -0.5f * Settings->ActiveWidthMeters;
    float MinimumDelta = TNumericLimits<float>::Max();
    float MaximumDelta = -TNumericLimits<float>::Max();
    int32 DepressedSampleCount = 0;
    for (int32 Y = 0; Y < Resolution; ++Y)
    {
        for (int32 X = 0; X < Resolution; ++X)
        {
            const float WorldX = ChunkOrigin + ChunkKey.X * ChunkWidth +
                X * SpacingMeters;
            const float WorldY = ChunkOrigin + ChunkKey.Y * ChunkWidth +
                Y * SpacingMeters;
            const float Reference = Sand::Lunar::ActiveSurfaceHeightMeters(
                WorldX,WorldY,Settings->SandDepthMeters,
                Settings->LunarPlayableWidthMeters);
            const float Delta = HeightMeters[X + Y * Resolution] - Reference;
            MinimumDelta = FMath::Min(MinimumDelta,Delta);
            MaximumDelta = FMath::Max(MaximumDelta,Delta);
            DepressedSampleCount += Delta < -0.005f ? 1 : 0;
        }
    }
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_TRACE_CAPTURE key=(%d,%d) minDelta=%.3fm maxDelta=%.3fm depressed=%d/%d"),
        ChunkKey.X,ChunkKey.Y,MinimumDelta,MaximumDelta,
        DepressedSampleCount,HeightMeters.Num());
    CachedDeformationHeights.Add(ChunkKey,MoveTemp(HeightMeters));
}

void ASandLunarWorldActor::SetActiveWindowCenterMeters(const FVector2f NewCenterMeters)
{
    if (ActiveWindowCenterMeters.Equals(NewCenterMeters,0.01f))
    {
        return;
    }
    ActiveWindowCenterMeters = NewCenterMeters;
    BuildCachedDeformationTerrain();
    UpdateTransitionVisibility();
}

void ASandLunarWorldActor::BuildMacroTerrain()
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float BaseDepth = Settings->SandDepthMeters;
    const float ActiveWidth = Settings->LunarPlayableWidthMeters;
    const float WorldSize = Settings->LunarLandscapeSizeMeters;
    const int32 Resolution = FMath::Clamp(Settings->LunarLandscapeResolution, 33, 257);
    const float Step = WorldSize / (Resolution - 1);
    const float Minimum = -0.5f * WorldSize;
    FMeshSectionData Terrain;
    Terrain.Vertices.Reserve(Resolution * Resolution * 4);

    for (int32 Y = 0; Y < Resolution - 1; ++Y)
    {
        for (int32 X = 0; X < Resolution - 1; ++X)
        {
            const float X0 = Minimum + X * Step;
            const float Y0 = Minimum + Y * Step;
            const float X1 = X0 + Step;
            const float Y1 = Y0 + Step;
            const FVector2f Center(0.5f * (X0 + X1), 0.5f * (Y0 + Y1));
            // The permanent detailed preview reaches 55 m from the origin.
            // Leave a small underlap so the 8 m macro grid can never expose a
            // horizon-facing crack at the edge of the 100 m playable field.
            if (FMath::Max(FMath::Abs(Center.X), FMath::Abs(Center.Y)) < 52.0f)
            {
                continue;
            }
            AppendQuad(Terrain, X0, Y0, X1, Y1, Step, BaseDepth, ActiveWidth,
                FLinearColor::White,true);
        }
    }
    UploadSection(MacroTerrain, 0, MoveTemp(Terrain), true);
}

void ASandLunarWorldActor::BuildDistantTerrain()
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float NearSize = Settings->LunarLandscapeSizeMeters;
    const float WorldSize = FMath::Max(Settings->LunarHorizonSizeMeters,NearSize);
    const int32 Resolution = FMath::Clamp(Settings->LunarHorizonResolution,33,257);
    const float Step = WorldSize / (Resolution - 1);
    const int32 NearResolution = FMath::Clamp(Settings->LunarLandscapeResolution,33,257);
    const float NormalSampleStep = NearSize / (NearResolution - 1);
    const float Minimum = -0.5f * WorldSize;
    const float NearHalf = 0.5f * NearSize;
    const float HiddenEdge = FMath::Max(0.0f,NearHalf - Step);
    FMeshSectionData Terrain;
    for (int32 Y = 0; Y < Resolution - 1; ++Y)
    {
        for (int32 X = 0; X < Resolution - 1; ++X)
        {
            const float X0 = Minimum + X * Step;
            const float Y0 = Minimum + Y * Step;
            const float X1 = X0 + Step;
            const float Y1 = Y0 + Step;
            const bool bInsideNearTerrain = X0 >= -HiddenEdge && X1 <= HiddenEdge &&
                Y0 >= -HiddenEdge && Y1 <= HiddenEdge;
            if (bInsideNearTerrain)
            {
                continue;
            }
            AppendQuad(Terrain,X0,Y0,X1,Y1,NormalSampleStep,Settings->SandDepthMeters,
                Settings->LunarPlayableWidthMeters,FLinearColor::White,true);
        }
    }
    const int32 TriangleCount = Terrain.Indices.Num() / 3;
    UploadSection(DistantTerrain,0,MoveTemp(Terrain),false);
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_HORIZON size=%.0fm resolution=%d triangles=%d collision=0"),
        WorldSize,Resolution,TriangleCount);
}

void ASandLunarWorldActor::BuildTransitionTerrain()
{
    if (!TransitionVertices.IsEmpty())
    {
        UpdateTransitionVisibility();
        return;
    }
    const double BuildStartSeconds = FPlatformTime::Seconds();
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float BaseDepth = Settings->SandDepthMeters;
    const float ActiveWidth = Settings->LunarPlayableWidthMeters;
    // This mesh is the always-resident visual truth for the complete 100 m
    // playable field.  It is built once on a shared 0.5 m grid, so sub-metre
    // craters and channels are already visible in the distance. Window shifts
    // only change vertex alpha; they never reconstruct geometry.
    constexpr float HalfExtent = 55.0f;
    constexpr float Step = 0.5f;
    const int32 Resolution = FMath::RoundToInt(2.0f * HalfExtent / Step) + 1;
    TransitionVertices.Reserve(Resolution * Resolution);
    TransitionNormals.Reserve(Resolution * Resolution);
    TransitionUVs.Reserve(Resolution * Resolution);
    TransitionColors.Reserve(Resolution * Resolution);
    TArray<int32> Indices;
    Indices.Reserve((Resolution - 1) * (Resolution - 1) * 6);
    const FLinearColor RegolithColor(0.23f,0.24f,0.26f,1.0f);
    for (int32 Y = 0; Y < Resolution; ++Y)
    {
        for (int32 X = 0; X < Resolution; ++X)
        {
            const float WorldX = -HalfExtent + X * Step;
            const float WorldY = -HalfExtent + Y * Step;
            const float Height = Sand::Lunar::MacroSurfaceHeightMeters(
                WorldX,WorldY,BaseDepth,ActiveWidth);
            TransitionVertices.Add(FVector(WorldX,WorldY,Height) * 100.0f);
            TransitionNormals.Add(SurfaceNormal(
                WorldX,WorldY,Step,BaseDepth,ActiveWidth));
            TransitionUVs.Add(FVector2D(WorldX,WorldY) * 0.018f);
            const float Tone = 0.92f + 0.08f * FMath::PerlinNoise2D(
                FVector2D(WorldX + 11.7f,WorldY - 3.2f) * 1.15f);
            TransitionColors.Add(RegolithColor * Tone);
        }
    }
    for (int32 Y = 0; Y < Resolution - 1; ++Y)
    {
        for (int32 X = 0; X < Resolution - 1; ++X)
        {
            const int32 A = X + Y * Resolution;
            const int32 B = A + 1;
            const int32 D = A + Resolution;
            const int32 C = D + 1;
            Indices.Append({A,C,B,A,D,C});
        }
    }
    TransitionTerrain->CreateMeshSection_LinearColor(
        0,TransitionVertices,Indices,TransitionNormals,TransitionUVs,TransitionColors,
        TArray<FProcMeshTangent>(),false,false);
    UpdateTransitionVisibility();
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_PREVIEW_BUILD size=110m step=0.5m vertices=%d triangles=%d collision=0 cpuMs=%.2f"),
        TransitionVertices.Num(),Indices.Num()/3,
        1000.0*(FPlatformTime::Seconds()-BuildStartSeconds));
}

void ASandLunarWorldActor::UpdateTransitionVisibility()
{
    if (TransitionVertices.IsEmpty() ||
        TransitionColors.Num() != TransitionVertices.Num())
    {
        return;
    }
    const double UpdateStartSeconds = FPlatformTime::Seconds();
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    constexpr float ChunkWidth = 5.0f;
    constexpr float CachedOverlap = 0.28f;
    const float ChunkOrigin = -0.5f * Settings->ActiveWidthMeters;
    // Keep one metre of the analytic preview beneath the live MPM boundary.
    // The overlap prevents a grazing-angle crack without hiding the working
    // surface around the tracks and bucket.
    const float LiveHoleHalf = 0.5f * Settings->ActiveWidthMeters - 1.0f;
    int32 HiddenVertices = 0;
    for (int32 Index = 0; Index < TransitionVertices.Num(); ++Index)
    {
        const FVector2f Point(
            TransitionVertices[Index].X / 100.0f,
            TransitionVertices[Index].Y / 100.0f);
        bool bHide = !bOverviewMode && FMath::Max(
            FMath::Abs(Point.X - ActiveWindowCenterMeters.X),
            FMath::Abs(Point.Y - ActiveWindowCenterMeters.Y)) < LiveHoleHalf;
        if (!bHide && !bOverviewMode)
        {
            const FIntPoint BaseKey(
                FMath::FloorToInt((Point.X - ChunkOrigin) / ChunkWidth),
                FMath::FloorToInt((Point.Y - ChunkOrigin) / ChunkWidth));
            for (int32 OffsetY = -1; OffsetY <= 0 && !bHide; ++OffsetY)
            {
                for (int32 OffsetX = -1; OffsetX <= 0 && !bHide; ++OffsetX)
                {
                    const FIntPoint Key = BaseKey + FIntPoint(OffsetX,OffsetY);
                    if (!CachedDeformationHeights.Contains(Key))
                    {
                        continue;
                    }
                    const float MinimumX = ChunkOrigin + Key.X * ChunkWidth;
                    const float MinimumY = ChunkOrigin + Key.Y * ChunkWidth;
                    bHide = Point.X >= MinimumX - CachedOverlap &&
                        Point.X <= MinimumX + ChunkWidth + CachedOverlap &&
                        Point.Y >= MinimumY - CachedOverlap &&
                        Point.Y <= MinimumY + ChunkWidth + CachedOverlap;
                }
            }
        }
        TransitionColors[Index].A = bHide ? 0.0f : 1.0f;
        HiddenVertices += bHide ? 1 : 0;
    }
    TransitionTerrain->UpdateMeshSection_LinearColor(
        0,TransitionVertices,TransitionNormals,TransitionUVs,TransitionColors,
        TArray<FProcMeshTangent>(),false);
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_PREVIEW_MASK center=(%.1f,%.1f)m cached=%d hiddenVertices=%d cpuMs=%.2f"),
        ActiveWindowCenterMeters.X,ActiveWindowCenterMeters.Y,
        CachedDeformationHeights.Num(),HiddenVertices,
        1000.0*(FPlatformTime::Seconds()-UpdateStartSeconds));
}

void ASandLunarWorldActor::BuildCachedDeformationTerrain()
{
    const double BuildStartSeconds = FPlatformTime::Seconds();
    if (CachedDeformationResolution < 2 || CachedDeformationSpacingMeters <= 0.0f)
    {
        CachedDeformationTerrain->ClearMeshSection(0);
        return;
    }
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    constexpr float ChunkWidth = 5.0f;
    const float ChunkOrigin = -0.5f * Settings->ActiveWidthMeters;
    const int32 ResidentChunkCount = FMath::Max(1,
        FMath::RoundToInt(Settings->ActiveWidthMeters / ChunkWidth));
    const FIntPoint MinimumActiveKey(
        FMath::RoundToInt(ActiveWindowCenterMeters.X / ChunkWidth),
        FMath::RoundToInt(ActiveWindowCenterMeters.Y / ChunkWidth));
    FMeshSectionData Deformation;
    int32 VisibleChunkCount = 0;
    for (const TPair<FIntPoint,TArray<float>>& Pair : CachedDeformationHeights)
    {
        const bool bActive = Pair.Key.X >= MinimumActiveKey.X &&
            Pair.Key.X < MinimumActiveKey.X + ResidentChunkCount &&
            Pair.Key.Y >= MinimumActiveKey.Y &&
            Pair.Key.Y < MinimumActiveKey.Y + ResidentChunkCount;
        if (!bActive)
        {
            ++VisibleChunkCount;
            AppendHeightfieldChunk(Deformation,Pair.Key,Pair.Value,
                CachedDeformationResolution,CachedDeformationSpacingMeters,
                ChunkOrigin);
        }
    }
    const int32 TriangleCount = Deformation.Indices.Num() / 3;
    UploadSection(CachedDeformationTerrain,0,MoveTemp(Deformation),false);
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_TRACE_PROXY storedChunks=%d visibleChunks=%d triangles=%d cpuMs=%.2f"),
        CachedDeformationHeights.Num(),VisibleChunkCount,TriangleCount,
        1000.0*(FPlatformTime::Seconds()-BuildStartSeconds));
}

void ASandLunarWorldActor::BuildRocks()
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float BaseDepth = Settings->SandDepthMeters;
    const float ResidentWidth = Settings->ActiveWidthMeters;
    const float ActiveWidth = Settings->LunarPlayableWidthMeters;
    const float HalfWorld = 0.5f * Settings->LunarLandscapeSizeMeters;
    FRandomStream Random(0x4c524f43); // "LROC", deterministic across runs.
    const int32 RockCount = FMath::Clamp(Settings->LunarRockCount, 0, 320);
    constexpr int32 DynamicRockCount = 10;
    FMeshSectionData FarRockMesh;
    const FLinearColor DarkRockColor =
        FLinearColor::FromSRGBColor(FColor(96,100,106));
    UMaterialInterface* VertexColorMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    UMaterialInterface* RockMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Materials/M_LunarRock.M_LunarRock"));
    if (RockMaterial == nullptr)
    {
        RockMaterial = VertexColorMaterial;
    }
    StaticRocks->SetMaterial(0,RockMaterial);
    for (int32 Index = 0; Index < RockCount; ++Index)
    {
        FVector2f P;
        const bool bLocalRock = Index < FMath::Min(DynamicRockCount, RockCount);
        if (bLocalRock)
        {
            do
            {
                P = FVector2f(Random.FRandRange(-0.42f * ResidentWidth, 0.42f * ResidentWidth),
                    Random.FRandRange(-0.42f * ResidentWidth, 0.42f * ResidentWidth));
            }
            while (P.Size() < 1.55f || (P - FVector2f(-1.0f,0.0f)).Size() < 1.0f);
        }
        else
        {
            do
            {
                P = FVector2f(Random.FRandRange(-0.92f * HalfWorld, 0.92f * HalfWorld),
                    Random.FRandRange(-0.92f * HalfWorld, 0.92f * HalfWorld));
            }
            while (FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)) < 0.65f * ResidentWidth);
        }
        const float Height = bLocalRock
            ? Sand::Lunar::ActiveSurfaceHeightMeters(P.X,P.Y,BaseDepth,ActiveWidth)
            : Sand::Lunar::MacroSurfaceHeightMeters(P.X, P.Y, BaseDepth, ActiveWidth);
        const float Radius = bLocalRock
            ? Random.FRandRange(0.10f,0.28f)
            : Random.FRandRange(0.25f, Index % 13 == 0 ? 2.6f : 1.2f);
        const FVector HalfExtentCentimeters(
            100.0f * Radius * Random.FRandRange(0.82f,1.16f),
            100.0f * Radius * Random.FRandRange(0.76f,1.12f),
            100.0f * Radius * Random.FRandRange(0.62f,1.02f));
        const FRotator Rotation(Random.FRandRange(-18.0f,18.0f),Random.FRandRange(0.0f,360.0f),Random.FRandRange(-20.0f,20.0f));
        if (!bLocalRock)
        {
            AppendAngularRock(FarRockMesh,Random,
                FVector(P.X,P.Y,Height + 0.42f * Radius) * 100.0f,
                HalfExtentCentimeters,Rotation.Quaternion(),DarkRockColor);
            continue;
        }

        UProceduralMeshComponent* Rock = NewObject<UProceduralMeshComponent>(
            this,*FString::Printf(TEXT("MovableLunarRock_%02d"),Index));
        AddInstanceComponent(Rock);
        Rock->SetupAttachment(SceneRoot);
        Rock->SetMobility(EComponentMobility::Movable);
        Rock->SetCastShadow(true);
        Rock->bUseComplexAsSimpleCollision = false;
        Rock->bUseAsyncCooking = false;
        if (RockMaterial != nullptr)
        {
            Rock->SetMaterial(0,RockMaterial);
        }
        FMeshSectionData RockMesh;
        TArray<FVector> ConvexVertices;
        AppendAngularRock(RockMesh,Random,FVector::ZeroVector,HalfExtentCentimeters,
            FQuat::Identity,DarkRockColor,&ConvexVertices);
        UploadSection(Rock,0,MoveTemp(RockMesh),false);
        Rock->AddCollisionConvexMesh(ConvexVertices);
        Rock->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
        Rock->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Rock->SetWorldLocation(FVector(P.X,P.Y,Height) * 100.0f +
            FVector(0.0f,0.0f,0.72f * HalfExtentCentimeters.Z));
        Rock->SetWorldRotation(Rotation);
        Rock->SetLinearDamping(0.35f);
        Rock->SetAngularDamping(0.90f);
        Rock->RegisterComponent();
        const float VolumeCubicMeters = 3.1f *
            FMath::Pow(Radius,3.0f);
        Rock->SetMassOverrideInKg(NAME_None,
            FMath::Clamp(2700.0f * VolumeCubicMeters,8.0f,220.0f),true);
        Rock->SetSimulatePhysics(true);
        DynamicRocks.Add(Rock);
        DynamicRockSupportRadiiCentimeters.Add(
            FMath::Max(HalfExtentCentimeters.X,HalfExtentCentimeters.Y));
        DynamicRockHalfHeightsCentimeters.Add(HalfExtentCentimeters.Z);
    }
    UploadSection(StaticRocks,0,MoveTemp(FarRockMesh),false);
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_ROCKS dynamicRigid=%d staticContext=%d shape=irregular-convex color=charcoal"),
        DynamicRocks.Num(),FMath::Max(0,RockCount-DynamicRocks.Num()));
}

void ASandLunarWorldActor::UpdateTrackMarks()
{
    if (!Excavator.IsValid())
    {
        for (TActorIterator<ASandExcavatorPawn> It(GetWorld()); It; ++It)
        {
            Excavator = *It;
            break;
        }
    }
    if (!Excavator.IsValid() || Excavator->ChassisBody == nullptr ||
        Excavator->LeftTrackCollider == nullptr ||
        Excavator->RightTrackCollider == nullptr)
    {
        return;
    }
    const float SpeedCentimetersPerSecond =
        Excavator->ChassisBody->GetPhysicsLinearVelocity().Size2D();
    if (SpeedCentimetersPerSecond < 5.0f)
    {
        return;
    }
    const auto GroundMark = [this](const UBoxComponent* Track)
    {
        const FTransform Transform = Track->GetComponentTransform();
        FVector Mark = Transform.GetLocation();
        float SurfaceHeightCentimeters = 0.0f;
        if (SandSurface.IsValid() &&
            SandSurface->SampleSandSurfaceHeightCentimeters(
                FVector2D(Mark.X,Mark.Y),6.0f,SurfaceHeightCentimeters))
        {
            Mark.Z = SurfaceHeightCentimeters + 1.2f;
        }
        else
        {
            const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
            Mark.Z = 100.0f * Sand::Lunar::ActiveSurfaceHeightMeters(
                Mark.X / 100.0f,Mark.Y / 100.0f,Settings->SandDepthMeters,
                Settings->LunarPlayableWidthMeters) + 1.2f;
        }
        return Mark;
    };
    const FVector Left = GroundMark(Excavator->LeftTrackCollider);
    const FVector Right = GroundMark(Excavator->RightTrackCollider);
    if (!bHasPreviousTrackMark)
    {
        PreviousLeftTrackMark = Left;
        PreviousRightTrackMark = Right;
        bHasPreviousTrackMark = true;
        return;
    }
    const float TravelDistance = 0.5f * (
        FVector::Dist2D(Left,PreviousLeftTrackMark) +
        FVector::Dist2D(Right,PreviousRightTrackMark));
    if (TravelDistance > 120.0f)
    {
        // A test teleport or external reposition is not a travelled path.
        PreviousLeftTrackMark = Left;
        PreviousRightTrackMark = Right;
        return;
    }
    if (TravelDistance < 12.0f)
    {
        return;
    }
    const int32 SegmentIndex = TrackMarkIndices.Num() / 12;
    // Compacted regolith should remain readable without becoming a black
    // graphic line at grazing angles. The mark is only moderately darker than
    // the 0.23/0.24/0.26 terrain and does not cast its own shadow.
    const FLinearColor DisturbedRegolith = SegmentIndex % 3 == 0
        ? FLinearColor(0.12f,0.125f,0.135f,1.0f)
        : FLinearColor(0.145f,0.150f,0.160f,1.0f);
    const auto AddStrip = [this,&DisturbedRegolith](
        const FVector Previous,const FVector Current)
    {
        FVector Direction = Current - Previous;
        Direction.Z = 0.0f;
        if (!Direction.Normalize())
        {
            return;
        }
        constexpr float HalfWidthCentimeters = 4.8f;
        const FVector Side = FVector::CrossProduct(FVector::UpVector,Direction) *
            HalfWidthCentimeters;
        const int32 First = TrackMarkVertices.Num();
        TrackMarkVertices.Append({
            Previous - Side,Previous + Side,Current + Side,Current - Side});
        TrackMarkNormals.Append({
            FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
        TrackMarkUVs.Append({
            FVector2D(0.0f,0.0f),FVector2D(1.0f,0.0f),
            FVector2D(1.0f,1.0f),FVector2D(0.0f,1.0f)});
        TrackMarkColors.Append({
            DisturbedRegolith,DisturbedRegolith,
            DisturbedRegolith,DisturbedRegolith});
        TrackMarkIndices.Append({
            First,First + 2,First + 1,First,First + 3,First + 2});
    };
    AddStrip(PreviousLeftTrackMark,Left);
    AddStrip(PreviousRightTrackMark,Right);
    PreviousLeftTrackMark = Left;
    PreviousRightTrackMark = Right;
    TrackMarks->CreateMeshSection_LinearColor(
        0,TrackMarkVertices,TrackMarkIndices,TrackMarkNormals,TrackMarkUVs,
        TrackMarkColors,TArray<FProcMeshTangent>(),false,false);
    if (SegmentIndex == 0 || SegmentIndex % 100 == 0)
    {
        UE_LOG(LogTemp,Display,
            TEXT("LUNAR_TRACK_HISTORY segments=%d vertices=%d persistent=1"),
            SegmentIndex + 1,TrackMarkVertices.Num());
    }
}

void ASandLunarWorldActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateTrackMarks();
    if (!SandSurface.IsValid())
    {
        for (TActorIterator<ASandCollapseSurfacePreviewActor> It(GetWorld()); It; ++It)
        {
            SandSurface = *It;
            break;
        }
    }
    if (!SandSurface.IsValid())
    {
        return;
    }
    const bool bRockTest = FParse::Param(FCommandLine::Get(),TEXT("SandRockTest"));
    RockTestElapsedSeconds += DeltaSeconds;
    for (int32 Index = 0; Index < DynamicRocks.Num(); ++Index)
    {
        UProceduralMeshComponent* Rock = DynamicRocks[Index];
        if (Rock == nullptr || !Rock->IsSimulatingPhysics())
        {
            continue;
        }
        float SurfaceHeightCentimeters = 0.0f;
        const FVector Location = Rock->GetComponentLocation();
        const bool bHasParticleSurface = SandSurface->SampleSandSurfaceHeightCentimeters(
            FVector2D(Location.X,Location.Y),DynamicRockSupportRadiiCentimeters[Index],
            SurfaceHeightCentimeters);
        if (!bHasParticleSurface)
        {
            const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
            const FVector2f PositionMeters(Location.X / 100.0f,Location.Y / 100.0f);
            if (FMath::Max(FMath::Abs(PositionMeters.X),FMath::Abs(PositionMeters.Y)) >=
                0.5f * Settings->LunarPlayableWidthMeters)
            {
                continue;
            }
            // During the first asynchronous density readback, hold rocks on
            // the same analytic surface used to seed the particles. As soon as
            // particle heights arrive this fallback relinquishes control.
            SurfaceHeightCentimeters = 100.0f * Sand::Lunar::ActiveSurfaceHeightMeters(
                PositionMeters.X,PositionMeters.Y,Settings->SandDepthMeters,
                Settings->LunarPlayableWidthMeters);
        }
        const float HalfHeight = DynamicRockHalfHeightsCentimeters[Index];
        const float Bottom = Location.Z - HalfHeight;
        const float TargetBottom = SurfaceHeightCentimeters - 0.24f * HalfHeight;
        const float ContactAlpha = FMath::Clamp(
            (SurfaceHeightCentimeters + 1.5f - Bottom) / 2.5f,0.0f,1.0f);
        if (ContactAlpha <= 0.0f)
        {
            continue;
        }
        const float Mass = Rock->GetMass();
        const FVector Velocity = Rock->GetPhysicsLinearVelocity();
        const float Compression = FMath::Max(0.0f,TargetBottom - Bottom);
        const float Weight = Mass * 980.0f;
        const float SpringForce = Mass * 72.0f * Compression;
        const float DampingForce = Mass * 7.5f * Velocity.Z;
        const float UpwardForce = FMath::Clamp(
            ContactAlpha * Weight + SpringForce - DampingForce,0.0f,4.0f * Weight);
        Rock->AddForce(FVector::UpVector * UpwardForce,NAME_None,false);

        const float EmbeddedFraction = FMath::Clamp(
            (SurfaceHeightCentimeters - Bottom) / FMath::Max(2.0f * HalfHeight,1.0f),0.0f,1.0f);
        const FVector HorizontalVelocity(Velocity.X,Velocity.Y,0.0f);
        Rock->AddForce(-HorizontalVelocity * Mass *
            FMath::Lerp(0.8f,4.5f,EmbeddedFraction),NAME_None,false);

        if (bRockTest && Index == 0)
        {
            if (!bRockTestImpulseApplied && RockTestElapsedSeconds >= 1.0f)
            {
                Rock->AddImpulse(FVector(3800.0f,900.0f,0.0f),NAME_None,false);
                bRockTestImpulseApplied = true;
                UE_LOG(LogTemp,Display,TEXT("ROCK_TEST impulseApplied massKg=%.2f"),Mass);
            }
            const int32 WholeSecond = FMath::FloorToInt(RockTestElapsedSeconds);
            if (WholeSecond > FMath::FloorToInt(RockTestLastLogSeconds))
            {
                RockTestLastLogSeconds = RockTestElapsedSeconds;
                UE_LOG(LogTemp,Display,
                    TEXT("ROCK_TEST t=%.1f positionCm=(%.1f,%.1f,%.1f) velocityCmS=(%.1f,%.1f,%.1f) bottomMinusSurfaceCm=%.2f"),
                    RockTestElapsedSeconds,Location.X,Location.Y,Location.Z,
                    Velocity.X,Velocity.Y,Velocity.Z,Bottom-SurfaceHeightCentimeters);
            }
        }
    }
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandLunarTerrainProfileTest,
    "SandSimulation.LunarWorld.TerrainProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandLunarTerrainProfileTest::RunTest(const FString& Parameters)
{
    constexpr float BaseDepth = 1.2f;
    constexpr float Width = 10.0f;
    float Minimum = TNumericLimits<float>::Max();
    float Maximum = -TNumericLimits<float>::Max();
    for (int32 Y = 0; Y <= 40; ++Y)
    {
        for (int32 X = 0; X <= 40; ++X)
        {
            const float H = Sand::Lunar::ActiveSurfaceHeightMeters(-5.0f + 0.25f * X, -5.0f + 0.25f * Y, BaseDepth, Width);
            TestTrue(TEXT("active lunar height is finite"), FMath::IsFinite(H));
            Minimum = FMath::Min(Minimum, H);
            Maximum = FMath::Max(Maximum, H);
        }
    }
    TestTrue(TEXT("active terrain stays above a movable base"), Minimum >= 0.45f);
    TestTrue(TEXT("active terrain has useful relief"), Maximum - Minimum > 0.22f);
    const float CraterCenter = Sand::Lunar::ActiveSurfaceHeightMeters(-2.0f,1.45f,BaseDepth,Width);
    const float CraterRim = Sand::Lunar::ActiveSurfaceHeightMeters(-0.85f,1.45f,BaseDepth,Width);
    TestTrue(TEXT("primary crater center is below its rim"), CraterCenter < CraterRim);
    TestTrue(TEXT("embedded LROC DTM has measured relief"),
        Sand::Lunar::Nobile03MaximumElevationMeters - Sand::Lunar::Nobile03MinimumElevationMeters > 100.0f);
    return true;
}
#endif
