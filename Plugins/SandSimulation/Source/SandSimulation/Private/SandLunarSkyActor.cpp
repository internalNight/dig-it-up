#include "SandLunarSkyActor.h"

#include "SandLevelSettings.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr float CelestialRadiusCentimeters = 400000.0f;
constexpr float StarRadiusCentimeters = 385000.0f;
constexpr float SatelliteRadiusCentimeters = 320000.0f;

struct FSkyMesh
{
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
};

void UploadSkyMesh(UProceduralMeshComponent* Component, FSkyMesh&& Mesh,
    const int32 SectionIndex = 0)
{
    Component->CreateMeshSection_LinearColor(
        SectionIndex,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UVs,Mesh.Colors,
        TArray<FProcMeshTangent>(),false,false);
}

void AppendSphereTriangle(FSkyMesh& Mesh, const FVector& A, const FVector& B,
    const FVector& C, const float Radius)
{
    const int32 First = Mesh.Vertices.Num();
    for (const FVector& Normal : {A,B,C})
    {
        Mesh.Vertices.Add(Normal*Radius);
        Mesh.Normals.Add(Normal);
        Mesh.UVs.Add(FVector2D::ZeroVector);
        Mesh.Colors.Add(FLinearColor::White);
    }
    Mesh.Indices.Append({First,First+1,First+2});
}

void AppendBox(FSkyMesh& Mesh, const FVector& HalfExtent,
    const FLinearColor& Color)
{
    static const FVector Signs[8] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    static const int32 BoxTriangles[] = {
        0,2,1,0,3,2,4,5,6,4,6,7,
        0,1,5,0,5,4,1,2,6,1,6,5,
        2,3,7,2,7,6,3,0,4,3,4,7};
    const int32 First = Mesh.Vertices.Num();
    for (const FVector& Sign : Signs)
    {
        Mesh.Vertices.Add(Sign*HalfExtent);
        Mesh.Normals.Add(Sign.GetSafeNormal());
        Mesh.UVs.Add(FVector2D::ZeroVector);
        Mesh.Colors.Add(Color);
    }
    for (const int32 Index : BoxTriangles)
    {
        Mesh.Indices.Add(First+Index);
    }
}

void AppendPanel(FSkyMesh& Mesh, const float Y0, const float Y1,
    const float HalfHeight, const FLinearColor& Color)
{
    const int32 First = Mesh.Vertices.Num();
    Mesh.Vertices.Append({
        FVector(0,Y0,-HalfHeight),FVector(0,Y1,-HalfHeight),
        FVector(0,Y1,HalfHeight),FVector(0,Y0,HalfHeight)});
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Mesh.Normals.Add(FVector(-1,0,0));
        Mesh.UVs.Add(FVector2D::ZeroVector);
        Mesh.Colors.Add(Color);
    }
    Mesh.Indices.Append({First,First+2,First+1,First,First+3,First+2});
}

void AppendStar(FSkyMesh& Mesh, const FVector& Direction,
    const float AngularDiameterDegrees, const FLinearColor& Color)
{
    FVector Right = FVector::CrossProduct(Direction,FVector::UpVector);
    if (!Right.Normalize())
    {
        Right = FVector::RightVector;
    }
    const FVector Up = FVector::CrossProduct(Right,Direction).GetSafeNormal();
    const float HalfSize = StarRadiusCentimeters * FMath::Tan(
        0.5f*FMath::DegreesToRadians(AngularDiameterDegrees));
    const FVector Center = Direction*StarRadiusCentimeters;
    const int32 First = Mesh.Vertices.Num();
    Mesh.Vertices.Append({
        Center+Up*HalfSize,Center+Right*HalfSize,
        Center-Up*HalfSize,Center-Right*HalfSize});
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Mesh.Normals.Add(-Direction);
        Mesh.UVs.Add(FVector2D::ZeroVector);
        Mesh.Colors.Add(Color);
    }
    Mesh.Indices.Append({First,First+1,First+2,First,First+2,First+3});
}
}

ASandLunarSkyActor::ASandLunarSkyActor()
{
    PrimaryActorTick.bCanEverTick = true;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SkyRoot"));
    SetRootComponent(SceneRoot);
    EarthMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Earth"));
    SunMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SunDisc"));
    StarMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StarsAndMilkyWay"));
    SatelliteMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SurveyOrbiter"));
    for (UProceduralMeshComponent* Component : {
        EarthMesh,SunMesh,StarMesh,SatelliteMesh})
    {
        Component->SetupAttachment(SceneRoot);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCastShadow(false);
    }
}

void ASandLunarSkyActor::Configure(const FVector& DirectionToSun)
{
    SunDirection = DirectionToSun.GetSafeNormal();
    BuildSky();
}

void ASandLunarSkyActor::BeginPlay()
{
    Super::BeginPlay();
    BuildSky();
}

void ASandLunarSkyActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this,0))
    {
        SetActorLocation(Camera->GetCameraLocation());
        if (!bLoggedVisualTestProjection &&
            FParse::Param(FCommandLine::Get(),TEXT("SandSkyVisualTest")))
        {
            if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this,0))
            {
                FVector2D EarthScreen;
                const bool bEarthInFront = Controller->ProjectWorldLocationToScreen(
                    EarthMesh->GetComponentLocation(),EarthScreen,true);
                UE_LOG(LogTemp,Display,
                    TEXT("LUNAR_SKY_VISUAL_TEST camera=(%.0f,%.0f,%.0f) rotation=(%.1f,%.1f) earthScreen=(%.1f,%.1f) inFront=%d"),
                    Camera->GetCameraLocation().X,Camera->GetCameraLocation().Y,
                    Camera->GetCameraLocation().Z,Camera->GetCameraRotation().Pitch,
                    Camera->GetCameraRotation().Yaw,EarthScreen.X,EarthScreen.Y,
                    bEarthInFront ? 1 : 0);
                bLoggedVisualTestProjection = true;
            }
        }
    }
    const float Period = GetDefault<USandLevelSettings>()->LunarSatelliteOrbitPeriodSeconds;
    SatelliteOrbitPhaseRadians = FMath::Fmod(
        SatelliteOrbitPhaseRadians+2.0f*PI*DeltaSeconds/FMath::Max(1.0f,Period),
        2.0f*PI);
    UpdateSatellite();
}

void ASandLunarSkyActor::BuildSky()
{
    CelestialBaseMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Materials/M_LunarCelestial.M_LunarCelestial"));
    if (CelestialBaseMaterial == nullptr)
    {
        CelestialBaseMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    }
    EarthMesh->ClearAllMeshSections();
    SunMesh->ClearAllMeshSections();
    StarMesh->ClearAllMeshSections();
    SatelliteMesh->ClearAllMeshSections();
    BuildEarth();
    BuildSun();
    BuildStars();
    BuildSatellite();
    UpdateSatellite();
    UE_LOG(LogTemp,Display,
        TEXT("LUNAR_CELESTIAL_SKY earth=%.2fdeg sun=%.2fdeg stars=%d satellitePeriod=%.0fs material=%s"),
        GetDefault<USandLevelSettings>()->LunarEarthAngularDiameterDegrees,
        GetDefault<USandLevelSettings>()->LunarSunAngularDiameterDegrees,
        GetDefault<USandLevelSettings>()->LunarStarCount,
        GetDefault<USandLevelSettings>()->LunarSatelliteOrbitPeriodSeconds,
        CelestialBaseMaterial ? *CelestialBaseMaterial->GetPathName() : TEXT("none"));
}

UMaterialInstanceDynamic* ASandLunarSkyActor::MakeCelestialMaterial(
    const FLinearColor& Tint, const float ExposureGain)
{
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(
        CelestialBaseMaterial,this);
    if (Material)
    {
        Material->SetVectorParameterValue(TEXT("Tint"),Tint);
        Material->SetScalarParameterValue(TEXT("ExposureGain"),ExposureGain);
    }
    return Material;
}

void ASandLunarSkyActor::BuildEarth()
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const float Radius = CelestialRadiusCentimeters*FMath::Tan(
        0.5f*FMath::DegreesToRadians(Settings->LunarEarthAngularDiameterDegrees));
    constexpr int32 LatitudeSegments = 36;
    constexpr int32 LongitudeSegments = 72;
    TStaticArray<FSkyMesh,5> Sections;
    for (int32 LatitudeIndex = 0; LatitudeIndex < LatitudeSegments; ++LatitudeIndex)
    {
        const float Latitude0 = -0.5f*PI+PI*LatitudeIndex/LatitudeSegments;
        const float Latitude1 = -0.5f*PI+PI*(LatitudeIndex+1)/LatitudeSegments;
        for (int32 LongitudeIndex = 0; LongitudeIndex < LongitudeSegments; ++LongitudeIndex)
        {
            const float Longitude0 = 2.0f*PI*LongitudeIndex/LongitudeSegments;
            const float Longitude1 = 2.0f*PI*(LongitudeIndex+1)/LongitudeSegments;
            const auto NormalAt = [](const float Latitude, const float Longitude)
            {
                return FVector(FMath::Cos(Latitude)*FMath::Cos(Longitude),
                    FMath::Cos(Latitude)*FMath::Sin(Longitude),FMath::Sin(Latitude));
            };
            const FVector A = NormalAt(Latitude0,Longitude0);
            const FVector B = NormalAt(Latitude1,Longitude0);
            const FVector C = NormalAt(Latitude0,Longitude1);
            const FVector D = NormalAt(Latitude1,Longitude1);
            const FVector Normal = (A+B+C+D).GetSafeNormal();
            const float Latitude = FMath::Asin(Normal.Z);
            const float Longitude = FMath::Atan2(Normal.Y,Normal.X);
            const float ContinentalNoise =
                0.65f*FMath::PerlinNoise2D(FVector2D(Longitude*0.85f,Latitude*1.65f))+
                0.35f*FMath::PerlinNoise2D(FVector2D(Longitude*2.2f+4.1f,Latitude*3.7f));
            const bool bLand = ContinentalNoise > 0.10f && FMath::Abs(Latitude) < 1.32f;
            const float CloudNoise = FMath::PerlinNoise2D(
                FVector2D(Longitude*3.1f-2.3f,Latitude*5.2f+0.7f));
            const float Daylight = FMath::Max(0.0f,
                static_cast<float>(FVector::DotProduct(Normal,SunDirection)));
            int32 Section = 0;
            if (Daylight >= 0.06f)
            {
                if (FMath::Abs(Latitude) > 1.20f || CloudNoise > 0.40f)
                {
                    Section = 4;
                }
                else if (!bLand)
                {
                    Section = 1;
                }
                else
                {
                    Section = Normal.Z > 0.0f ? 2 : 3;
                }
            }
            AppendSphereTriangle(Sections[Section],A,B,C,Radius);
            AppendSphereTriangle(Sections[Section],C,B,D,Radius);
        }
    }
    EarthMesh->SetRelativeLocation(EarthDirection.GetSafeNormal()*CelestialRadiusCentimeters);
    const FLinearColor Tints[5] = {
        FLinearColor(0.001f,0.003f,0.008f),FLinearColor(0.018f,0.16f,0.48f),
        FLinearColor(0.12f,0.34f,0.09f),FLinearColor(0.38f,0.22f,0.07f),
        FLinearColor(0.78f,0.88f,1.0f)};
    for (int32 Section = 0; Section < 5; ++Section)
    {
        UploadSkyMesh(EarthMesh,MoveTemp(Sections[Section]),Section);
        EarthMesh->SetMaterial(Section,MakeCelestialMaterial(Tints[Section],75000.0f));
    }
}

void ASandLunarSkyActor::BuildSun()
{
    const float Radius = CelestialRadiusCentimeters*FMath::Tan(
        0.5f*FMath::DegreesToRadians(
            GetDefault<USandLevelSettings>()->LunarSunAngularDiameterDegrees));
    constexpr int32 LatitudeSegments = 12;
    constexpr int32 LongitudeSegments = 24;
    FSkyMesh Mesh;
    for (int32 LatitudeIndex = 0; LatitudeIndex <= LatitudeSegments; ++LatitudeIndex)
    {
        const float Latitude = -0.5f*PI+PI*LatitudeIndex/LatitudeSegments;
        for (int32 LongitudeIndex = 0; LongitudeIndex <= LongitudeSegments; ++LongitudeIndex)
        {
            const float Longitude = 2.0f*PI*LongitudeIndex/LongitudeSegments;
            const FVector Normal(
                FMath::Cos(Latitude)*FMath::Cos(Longitude),
                FMath::Cos(Latitude)*FMath::Sin(Longitude),
                FMath::Sin(Latitude));
            Mesh.Vertices.Add(Normal*Radius);
            Mesh.Normals.Add(Normal);
            Mesh.UVs.Add(FVector2D::ZeroVector);
            Mesh.Colors.Add(FLinearColor(1.0f,0.73f,0.32f));
        }
    }
    for (int32 LatitudeIndex = 0; LatitudeIndex < LatitudeSegments; ++LatitudeIndex)
    {
        for (int32 LongitudeIndex = 0; LongitudeIndex < LongitudeSegments; ++LongitudeIndex)
        {
            const int32 A = LatitudeIndex*(LongitudeSegments+1)+LongitudeIndex;
            const int32 B = A+LongitudeSegments+1;
            Mesh.Indices.Append({A,B,A+1,A+1,B,B+1});
        }
    }
    SunMesh->SetRelativeLocation(SunDirection*CelestialRadiusCentimeters);
    UploadSkyMesh(SunMesh,MoveTemp(Mesh));
    SunMesh->SetMaterial(0,MakeCelestialMaterial(FLinearColor(1.0f,0.72f,0.28f),1500000.0f));
}

void ASandLunarSkyActor::BuildStars()
{
    FSkyMesh Mesh;
    FRandomStream Random(0x4d494c4b); // MILK
    const int32 StarCount = FMath::Clamp(
        GetDefault<USandLevelSettings>()->LunarStarCount,0,1600);
    const int32 BandCount = FMath::RoundToInt(0.70f*StarCount);
    for (int32 Index = 0; Index < StarCount; ++Index)
    {
        FVector Direction;
        const bool bMilkyWay = Index < BandCount;
        if (bMilkyWay)
        {
            const float Longitude = Random.FRandRange(0.0f,2.0f*PI);
            const float Latitude = FMath::Clamp(
                0.13f*(Random.FRand()+Random.FRand()+Random.FRand()-1.5f),
                -0.20f,0.20f);
            Direction = FRotator(27.0f,-18.0f,0.0f).RotateVector(FVector(
                FMath::Cos(Latitude)*FMath::Cos(Longitude),
                FMath::Cos(Latitude)*FMath::Sin(Longitude),
                FMath::Sin(Latitude)));
        }
        else
        {
            const float Z = Random.FRandRange(0.03f,1.0f);
            const float Azimuth = Random.FRandRange(0.0f,2.0f*PI);
            const float Horizontal = FMath::Sqrt(1.0f-Z*Z);
            Direction = FVector(
                Horizontal*FMath::Cos(Azimuth),Horizontal*FMath::Sin(Azimuth),Z);
        }
        if (Direction.Z < 0.02f)
        {
            Direction *= -1.0f;
        }
        const float Brightness = bMilkyWay
            ? Random.FRandRange(0.28f,0.64f) : Random.FRandRange(0.38f,0.82f);
        const float Warmth = Random.FRand();
        const FLinearColor Color = FMath::Lerp(
            FLinearColor(0.62f,0.72f,1.0f),
            FLinearColor(1.0f,0.76f,0.50f),Warmth)*Brightness;
        const float AngularSize = Random.FRandRange(
            bMilkyWay ? 0.028f : 0.035f,bMilkyWay ? 0.065f : 0.085f);
        AppendStar(Mesh,Direction.GetSafeNormal(),AngularSize,Color);
    }
    UploadSkyMesh(StarMesh,MoveTemp(Mesh));
    StarMesh->SetMaterial(0,MakeCelestialMaterial(FLinearColor(0.72f,0.82f,1.0f),350000.0f));
}

void ASandLunarSkyActor::BuildSatellite()
{
    FSkyMesh Body;
    FSkyMesh Panels;
    AppendBox(Body,FVector(90,80,50),FLinearColor::White);
    AppendPanel(Panels,-500,-90,115,FLinearColor::White);
    AppendPanel(Panels,90,500,115,FLinearColor::White);
    UploadSkyMesh(SatelliteMesh,MoveTemp(Body),0);
    UploadSkyMesh(SatelliteMesh,MoveTemp(Panels),1);
    SatelliteMesh->SetMaterial(0,MakeCelestialMaterial(
        FLinearColor(0.58f,0.39f,0.10f),90000.0f));
    SatelliteMesh->SetMaterial(1,MakeCelestialMaterial(
        FLinearColor(0.035f,0.16f,0.50f),110000.0f));
}

void ASandLunarSkyActor::UpdateSatellite()
{
    const float Inclination = FMath::DegreesToRadians(72.0f);
    FVector Direction(
        FMath::Cos(SatelliteOrbitPhaseRadians),
        FMath::Sin(SatelliteOrbitPhaseRadians)*FMath::Cos(Inclination),
        0.20f+FMath::Sin(SatelliteOrbitPhaseRadians)*FMath::Sin(Inclination));
    // Rotate the orbital plane so the initial pass can be discovered near the
    // default Earth-facing camera azimuth; the two-hour rate remains unchanged.
    Direction = FRotator(0.0f,-70.0f,0.0f).RotateVector(Direction);
    Direction.Normalize();
    SatelliteMesh->SetRelativeLocation(Direction*SatelliteRadiusCentimeters);
    SatelliteMesh->SetRelativeRotation(FRotationMatrix::MakeFromX(-Direction).Rotator());
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandLunarCelestialScaleTest,
    "SandSimulation.LunarWorld.CelestialScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandLunarCelestialScaleTest::RunTest(const FString& Parameters)
{
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    TestTrue(TEXT("Earth angular diameter matches the lunar-surface scale"),
        Settings->LunarEarthAngularDiameterDegrees >= 1.85f &&
        Settings->LunarEarthAngularDiameterDegrees <= 1.95f);
    TestTrue(TEXT("solar angular diameter is near one half degree"),
        Settings->LunarSunAngularDiameterDegrees >= 0.50f &&
        Settings->LunarSunAngularDiameterDegrees <= 0.56f);
    TestTrue(TEXT("Earth appears roughly 3.6 times wider than the Sun"),
        Settings->LunarEarthAngularDiameterDegrees /
            Settings->LunarSunAngularDiameterDegrees >= 3.4f &&
        Settings->LunarEarthAngularDiameterDegrees /
            Settings->LunarSunAngularDiameterDegrees <= 3.8f);
    TestTrue(TEXT("survey orbiter period remains near two hours"),
        Settings->LunarSatelliteOrbitPeriodSeconds >= 6900.0f &&
        Settings->LunarSatelliteOrbitPeriodSeconds <= 7500.0f);
    return true;
}
#endif
