#include "SandLunarWorldActor.h"

#include "SandLevelSettings.h"
#include "SandLunarTerrain.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
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
            Mesh.Colors.Add(Color);
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
}

ASandLunarWorldActor::ASandLunarWorldActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    MacroTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MacroTerrain"));
    MacroTerrain->SetupAttachment(SceneRoot);
    MacroTerrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MacroTerrain->SetCastShadow(true);
    TransitionTerrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransitionTerrain"));
    TransitionTerrain->SetupAttachment(SceneRoot);
    TransitionTerrain->SetRelativeLocation(FVector(0.0f,0.0f,1.0f));
    TransitionTerrain->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    TransitionTerrain->SetCastShadow(true);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded())
    {
        MacroTerrain->SetMaterial(0,VertexColorMaterial.Object);
        TransitionTerrain->SetMaterial(0,VertexColorMaterial.Object);
    }

    Rocks = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LunarRocks"));
    Rocks->SetupAttachment(SceneRoot);
    Rocks->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Rocks->SetCastShadow(true);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMesh.Succeeded())
    {
        Rocks->SetStaticMesh(SphereMesh.Object);
    }
}

void ASandLunarWorldActor::BeginPlay()
{
    Super::BeginPlay();
    // BasicShapeMaterial exposes a runtime Color parameter. The existing
    // regolith material has baked colour, so it cannot distinguish mare and
    // highland sections without authoring another binary asset.
    UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BaseMaterial != nullptr)
    {
        HighlandMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        const FLinearColor HighlandColor = FLinearColor::FromSRGBColor(FColor(126,128,134));
        HighlandMaterial->SetVectorParameterValue(TEXT("Color"), HighlandColor);
        HighlandMaterial->SetVectorParameterValue(TEXT("DiffuseColor"), HighlandColor);
        HighlandMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.94f);
        Rocks->SetMaterial(0, HighlandMaterial);
    }

    BuildMacroTerrain();
    BuildTransitionTerrain();
    BuildRocks();
}

void ASandLunarWorldActor::BuildMacroTerrain()
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float BaseDepth = Settings->SandDepthMeters;
    const float ActiveWidth = Settings->ActiveWidthMeters;
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
            if (FMath::Max(FMath::Abs(Center.X), FMath::Abs(Center.Y)) < 11.0f)
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
    const float ActiveWidth = Settings->ActiveWidthMeters;
    constexpr float HalfRing = 12.0f;
    constexpr float Step = 0.5f;
    // Slight overlap hides the independent marching-cubes edge without
    // covering the playable top surface.
    const float HoleHalf = 0.5f * ActiveWidth - 0.80f;
    FMeshSectionData Ring;
    for (float Y = -HalfRing; Y < HalfRing - 0.1f; Y += Step)
    {
        for (float X = -HalfRing; X < HalfRing - 0.1f; X += Step)
        {
            const FVector2f Center(X + 0.5f * Step, Y + 0.5f * Step);
            if (FMath::Max(FMath::Abs(Center.X), FMath::Abs(Center.Y)) < HoleHalf)
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
    if (Rocks->GetStaticMesh() == nullptr)
    {
        return;
    }
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float BaseDepth = Settings->SandDepthMeters;
    const float ActiveWidth = Settings->ActiveWidthMeters;
    const float HalfWorld = 0.5f * Settings->LunarLandscapeSizeMeters;
    FRandomStream Random(0x4c524f43); // "LROC", deterministic across runs.
    const int32 RockCount = FMath::Clamp(Settings->LunarRockCount, 0, 320);
    for (int32 Index = 0; Index < RockCount; ++Index)
    {
        FVector2f P;
        const bool bLocalRock = Index < FMath::Min(10, RockCount);
        if (bLocalRock)
        {
            do
            {
                P = FVector2f(Random.FRandRange(-0.45f * ActiveWidth, 0.45f * ActiveWidth),
                    Random.FRandRange(-0.45f * ActiveWidth, 0.45f * ActiveWidth));
            }
            while (P.Size() < 1.7f);
        }
        else
        {
            do
            {
                P = FVector2f(Random.FRandRange(-0.92f * HalfWorld, 0.92f * HalfWorld),
                    Random.FRandRange(-0.92f * HalfWorld, 0.92f * HalfWorld));
            }
            while (FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)) < 0.65f * ActiveWidth);
        }
        const float Height = bLocalRock
            ? Sand::Lunar::ActiveSurfaceHeightMeters(P.X,P.Y,BaseDepth,ActiveWidth)
            : Sand::Lunar::MacroSurfaceHeightMeters(P.X, P.Y, BaseDepth, ActiveWidth);
        const float Radius = bLocalRock
            ? Random.FRandRange(0.05f,0.18f)
            : Random.FRandRange(0.25f, Index % 13 == 0 ? 2.4f : 1.1f);
        // The Engine sphere has a 0.5 m radius at unit scale.
        const FVector Scale(2.0f * Radius, 1.4f * Radius * Random.FRandRange(0.65f,1.25f),
            1.2f * Radius * Random.FRandRange(0.55f,1.10f));
        const FRotator Rotation(Random.FRandRange(-18.0f,18.0f),Random.FRandRange(0.0f,360.0f),Random.FRandRange(-20.0f,20.0f));
        Rocks->AddInstance(FTransform(Rotation, FVector(P.X,P.Y,Height + 0.45f * Radius) * 100.0f, Scale), true);
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
