#include "SandLunarWorldActor.h"

#include "SandLevelSettings.h"
#include "SandLunarTerrain.h"
#include "SandCollapseSurfacePreviewActor.h"

#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
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
            const FLinearColor MareColor = FLinearColor::FromSRGBColor(FColor(50,54,62));
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
    TransitionTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransitionTerrain"));
    TransitionTerrain->SetupAttachment(SceneRoot);
    TransitionTerrain->SetRelativeLocation(FVector(0.0f,0.0f,-1.0f));
    TransitionTerrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    TransitionTerrain->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded())
    {
        MacroTerrain->SetMaterial(0,VertexColorMaterial.Object);
        TransitionTerrain->SetMaterial(0,VertexColorMaterial.Object);
    }

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
    BuildTransitionTerrain();
    BuildRocks();
    for (TActorIterator<ASandCollapseSurfacePreviewActor> It(GetWorld()); It; ++It)
    {
        SandSurface = *It;
        break;
    }
}

void ASandLunarWorldActor::SetActiveWindowCenterMeters(const FVector2f NewCenterMeters)
{
    if (ActiveWindowCenterMeters.Equals(NewCenterMeters,0.01f))
    {
        return;
    }
    ActiveWindowCenterMeters = NewCenterMeters;
    BuildTransitionTerrain();
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
            if (FMath::Max(FMath::Abs(Center.X), FMath::Abs(Center.Y)) < 60.0f)
            {
                continue;
            }
            AppendQuad(Terrain, X0, Y0, X1, Y1, Step, BaseDepth, ActiveWidth,
                FLinearColor::White,true);
        }
    }
    UploadSection(MacroTerrain, 0, MoveTemp(Terrain), true);
}

void ASandLunarWorldActor::BuildTransitionTerrain()
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float BaseDepth = Settings->SandDepthMeters;
    const float ActiveWidth = Settings->LunarPlayableWidthMeters;
    const float ResidentWidth = Settings->ActiveWidthMeters;
    constexpr float HalfRing = 64.0f;
    constexpr float Step = 1.0f;
    // Slight overlap hides the independent marching-cubes edge without
    // covering the playable top surface.
    const float HoleHalf = 0.5f * ResidentWidth - 0.80f;
    FMeshSectionData Ring;
    for (float Y = -HalfRing; Y < HalfRing - 0.1f; Y += Step)
    {
        for (float X = -HalfRing; X < HalfRing - 0.1f; X += Step)
        {
            const FVector2f Center(X + 0.5f * Step, Y + 0.5f * Step);
            if (FMath::Max(
                FMath::Abs(Center.X - ActiveWindowCenterMeters.X),
                FMath::Abs(Center.Y - ActiveWindowCenterMeters.Y)) < HoleHalf)
            {
                continue;
            }
            AppendQuad(Ring, X, Y, X + Step, Y + Step, Step, BaseDepth, ActiveWidth,
                FLinearColor(0.23f,0.24f,0.26f));
        }
    }
    UploadSection(TransitionTerrain, 0, MoveTemp(Ring), true);
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

void ASandLunarWorldActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
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
