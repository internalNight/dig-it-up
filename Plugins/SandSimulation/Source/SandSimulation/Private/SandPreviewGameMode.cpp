#include "SandPreviewGameMode.h"

#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SandCollapseSurfacePreviewActor.h"
#include "SandExcavatorPawn.h"
#include "SandRoadheaderPawn.h"
#include "SandMPMSolver.h"
#include "SandHUD.h"
#include "SandLevelSettings.h"
#include "SandFloorExposure.h"
#include "SandLunarTerrain.h"
#include "SandLunarWorldActor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "Misc/AutomationTest.h"

ASandPreviewGameMode::ASandPreviewGameMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = ASandHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
}

void ASandPreviewGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    if (GEngine != nullptr)
    {
        GEngine->SetMaxFPS(60.0f);
    }

    if(FParse::Param(FCommandLine::Get(),TEXT("SandSynchronous"))) {
        bChangedFixedClock=true; bPreviousFixedClock=FApp::UseFixedTimeStep(); PreviousFixedDelta=FApp::GetFixedDeltaTime();
        FApp::SetUseFixedTimeStep(true); FApp::SetFixedDeltaTime(Sand::MPM::CouplingStepSeconds());
    }
    const USandLevelSettings* LevelSettings = GetDefault<USandLevelSettings>();
    const bool bBench = FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"));
    const bool bLegacyAcceptance =
        FParse::Param(FCommandLine::Get(),TEXT("SandVictoryTest")) ||
        FParse::Param(FCommandLine::Get(),TEXT("SandBoundaryTest")) ||
        FParse::Param(FCommandLine::Get(),TEXT("SandBoomRaiseTest")) ||
        FParse::Param(FCommandLine::Get(),TEXT("SandSlopeCoastTest"));
    const bool bLunarWorld = LevelSettings->bLunarWorld &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandLegacyBox")) && !bBench && !bLegacyAcceptance;
    const float SandDepthCm = (bLunarWorld ? LevelSettings->SandDepthMeters : 1.5f) * 100.0f;
    const float ActiveWidthMeters = bLunarWorld ? LevelSettings->ActiveWidthMeters : 5.0f;
    World->SpawnActor<ASandCollapseSurfacePreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator);
    if (bLunarWorld)
    {
        World->SpawnActor<ASandLunarWorldActor>(FVector::ZeroVector, FRotator::ZeroRotator);
        UE_LOG(LogTemp, Display, TEXT("LUNAR_WORLD playable=%.1fm activeMPM=%.1fm macroTerrain=%.0fm source=NAC_DTM_NOBILE03 gravity=Earth-gameplay"),
            LevelSettings->LunarPlayableWidthMeters,ActiveWidthMeters,
            LevelSettings->LunarLandscapeSizeMeters);
    }

    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* FloorMaterial = LoadObject<UMaterialInterface>(
        nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (CubeMesh != nullptr)
    {
        const auto ConfigureContainerBox = [CubeMesh, FloorMaterial](
            UWorld* TargetWorld,
            const FVector& Location,
            const FVector& Scale,
            const FLinearColor& Color)
        {
            AStaticMeshActor* Box = TargetWorld->SpawnActor<AStaticMeshActor>(
                Location, FRotator::ZeroRotator);
            UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
            Mesh->SetStaticMesh(CubeMesh);
            Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetWorldScale3D(Scale);
            Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Mesh->SetCollisionProfileName(TEXT("BlockAll"));
            Mesh->SetCastShadow(true);
            if (FloorMaterial != nullptr)
            {
                Mesh->SetMaterial(0, FloorMaterial);
                if (UMaterialInstanceDynamic* Material =
                    Mesh->CreateAndSetMaterialInstanceDynamic(0))
                {
                    Material->SetVectorParameterValue(TEXT("Color"), Color);
                    Material->SetVectorParameterValue(TEXT("DiffuseColor"), Color);
                    Material->SetScalarParameterValue(TEXT("Roughness"), 0.92f);
                }
            }
        };

        AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(
            FVector(0.0, 0.0, -4.0),
            FRotator::ZeroRotator);
        Floor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
        Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        // The buried survey marker remains a local objective at the original
        // landing site; the streamed MPM boundary supplies the base elsewhere.
        const float FloorWidthMeters = ActiveWidthMeters;
        Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(
            FloorWidthMeters + 0.4f,FloorWidthMeters + 0.4f,0.08f));
        Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        if (FloorMaterial != nullptr)
        {
            UMaterialInterface* GoalMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_GoalFloor.M_GoalFloor"));
            Floor->GetStaticMeshComponent()->SetMaterial(0, GoalMaterial ? GoalMaterial : FloorMaterial);
            if (auto* Red = Floor->GetStaticMeshComponent()->CreateAndSetMaterialInstanceDynamic(0))
            {
                const FLinearColor MarkerColor = bLunarWorld
                    ? FLinearColor::FromSRGBColor(FColor(245,171,35))
                    : FLinearColor::FromSRGBColor(FColor(235,24,35));
                Red->SetVectorParameterValue(TEXT("Color"), MarkerColor);
                Red->SetVectorParameterValue(TEXT("DiffuseColor"), MarkerColor);
            }
        }

        // Four physical retaining walls cover the entire two-metre active sand
        // depth and extend 25 cm above the initial surface. The top remains open.
        if (!bLunarWorld)
        {
            const FLinearColor WallColor = FLinearColor::FromSRGBColor(FColor(65, 72, 84));
            const float WallZ = (SandDepthCm + 20.0f) * 0.5f;
            const float WallHeightMeters = (SandDepthCm + 30.0f) / 100.0f;
            ConfigureContainerBox(World, FVector(-256.0, 0.0, WallZ), FVector(0.12,5.24,WallHeightMeters), WallColor);
            ConfigureContainerBox(World, FVector(256.0, 0.0, WallZ), FVector(0.12,5.24,WallHeightMeters), WallColor);
            ConfigureContainerBox(World, FVector(0.0, -256.0, WallZ), FVector(5.24,0.12,WallHeightMeters), WallColor);
            ConfigureContainerBox(World, FVector(0.0, 256.0, WallZ), FVector(5.24,0.12,WallHeightMeters), WallColor);
        }
    }

    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
        FVector(0.0, 0.0, 200.0),
        bLunarWorld ? FRotator(-11.0, -32.0, 0.0) : FRotator(-48.0, -32.0, 0.0));
    // Directional lights use lux. A solar-scale value prevents auto exposure
    // from lifting the grey surface toward white while retaining hard lunar
    // shadow contrast.
    Sun->GetLightComponent()->SetIntensity(bLunarWorld ? 75000.0f : 2.8f);
    if (bLunarWorld)
    {
        Sun->GetLightComponent()->SetLightColor(FLinearColor(1.0f,0.965f,0.90f));
    }
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    if (UDirectionalLightComponent* SunComponent =
        Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
    {
        SunComponent->SetAtmosphereSunLight(!bLunarWorld);
    }

    // The lightweight Entry map has no authored environment. Spawn a real
    // atmosphere and movable skylight so the playable build has an outdoor
    // horizon instead of a black void, without loading a heavyweight level.
#if !PLATFORM_ANDROID
    // Mobile SkyAtmosphere requires a sky mesh/material that this procedural
    // level does not provide. Avoid the on-screen mobile renderer error.
    if (!bLunarWorld)
    {
        World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
    }
#endif
    ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(
        FVector(0.0, 0.0, 250.0), FRotator::ZeroRotator);
    SkyLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    SkyLight->GetLightComponent()->SetIntensity(bLunarWorld ? 0.08f : 0.65f);
    SkyLight->GetLightComponent()->RecaptureSky();

    if (bLunarWorld)
    {
        // The lightweight runtime map otherwise clears to editor grey. An
        // inward-facing, non-colliding black sphere provides the airless lunar
        // sky without an atmosphere or fog layer.
        UStaticMesh* SkySphereMesh = LoadObject<UStaticMesh>(nullptr,
            TEXT("/Engine/BasicShapes/Cube.Cube"));
        UMaterialInterface* SkyMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Materials/M_LunarSky.M_LunarSky"));
        if (SkySphereMesh != nullptr && SkyMaterial != nullptr)
        {
            AStaticMeshActor* SkySphere = World->SpawnActor<AStaticMeshActor>(
                FVector::ZeroVector,FRotator::ZeroRotator);
            UStaticMeshComponent* SkyMesh = SkySphere->GetStaticMeshComponent();
            SkyMesh->SetStaticMesh(SkySphereMesh);
            SkyMesh->SetWorldScale3D(FVector(10000.0f));
            SkyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            SkyMesh->SetCastShadow(false);
            SkyMesh->SetMaterial(0,SkyMaterial);
            UE_LOG(LogTemp,Display,TEXT("LUNAR_SKY material=%s shape=inward-two-sided-cube"),
                *SkyMaterial->GetPathName());
        }
    }

    if (!bLunarWorld)
    {
        AExponentialHeightFog* HorizonFog = World->SpawnActor<AExponentialHeightFog>(
            FVector(0.0, 0.0, -100.0), FRotator::ZeroRotator);
        HorizonFog->GetComponent()->SetFogDensity(0.0025f);
        HorizonFog->GetComponent()->SetFogHeightFalloff(0.18f);
        HorizonFog->GetComponent()->SetFogInscatteringColor(
            FLinearColor::FromSRGBColor(FColor(155, 184, 207)));
        HorizonFog->GetComponent()->SetStartDistance(700.0f);
    }

    APointLight* Fill = World->SpawnActor<APointLight>(FVector(40.0, -80.0, 180.0), FRotator::ZeroRotator);
    Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Fill->GetLightComponent()->SetIntensity(bLunarWorld ? 16.0f : 120.0f);

    if (APlayerController* PlayerController = World->GetFirstPlayerController())
    {
        PlayerController->bShowMouseCursor=true;
        PlayerController->bEnableClickEvents=true;
        PlayerController->bEnableTouchEvents=true;
        PlayerController->SetInputMode(FInputModeGameAndUI());
#if PLATFORM_ANDROID
        const bool bTouchExcavator = true;
#else
        const bool bTouchExcavator = FParse::Param(FCommandLine::Get(), TEXT("SandTouchPreview"));
#endif
        if (bTouchExcavator)
        {
            // This game draws its own left stick and three independent levers.
            PlayerController->ActivateTouchInterface(nullptr);
            SelectVehicle(false);
        }
        const bool bRoad=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheader")) || FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"));
        if(!bTouchExcavator && bRoad) SelectVehicle(true);
        else if(!bTouchExcavator && (FParse::Param(FCommandLine::Get(),TEXT("SandExcavator")) ||
            FParse::Param(FCommandLine::Get(),TEXT("SandAutopilot")) ||
            FParse::Param(FCommandLine::Get(),TEXT("SandWindowTest")) ||
            FParse::Param(FCommandLine::Get(),TEXT("SandVictoryTest")) ||
            FParse::Param(FCommandLine::Get(),TEXT("SandBoundaryTest")) ||
            FParse::Param(FCommandLine::Get(),TEXT("SandBoomRaiseTest")) ||
            FParse::Param(FCommandLine::Get(),TEXT("SandSlopeCoastTest")))) SelectVehicle(false);
    }

}

void ASandPreviewGameMode::SelectVehicle(bool bRoadheader)
{
    if(!bSelectingVehicle) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    if(!PC) return;
    const bool Bench=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"));
    const bool Fixture=FParse::Param(FCommandLine::Get(),TEXT("SandVictoryTest"));
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const bool Lunar = Settings->bLunarWorld && !FParse::Param(FCommandLine::Get(),TEXT("SandLegacyBox")) && !Bench && !Fixture &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandBoundaryTest")) &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandBoomRaiseTest")) &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandSlopeCoastTest"));
    const float SpawnX = -1.0f;
    const float SpawnHeight = Lunar
        ? Sand::Lunar::ActiveSurfaceHeightMeters(SpawnX,0.0f,
            Settings->SandDepthMeters,Settings->LunarPlayableWidthMeters)
        : 1.5f;
    FVector Position=Bench?FVector(0,0,36):Fixture?FVector(-210,-160,20.5):FVector(SpawnX*100,0,SpawnHeight*100+8);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn=GetWorld()->SpawnActor<ASandExcavatorPawn>(bRoadheader?ASandRoadheaderPawn::StaticClass():ASandExcavatorPawn::StaticClass(),Position,FRotator::ZeroRotator,Params);
    if(!Pawn) return;
    if(FParse::Param(FCommandLine::Get(),TEXT("SandSynchronous")))
        for(TActorIterator<ASandCollapseSurfacePreviewActor> It(GetWorld());It;++It) Pawn->AddTickPrerequisiteActor(*It);
    PC->Possess(Pawn);
    PC->bShowMouseCursor=false;
    PC->SetInputMode(FInputModeGameOnly());
#if !PLATFORM_ANDROID
    if (FParse::Param(FCommandLine::Get(), TEXT("SandTouchPreview")))
    {
        // Keep the pointer available when a desktop mouse emulates one touch.
        PC->bShowMouseCursor = true;
        PC->SetInputMode(FInputModeGameAndUI());
    }
#endif
    bSelectingVehicle=false;
    UE_LOG(LogTemp,Display,TEXT("Selected vehicle: %s"),bRoadheader?TEXT("Roadheader"):TEXT("Excavator"));

}

void ASandPreviewGameMode::CheckExposedFloor(const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
    if (bSelectingVehicle || FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"))) return;
    if (Vertices.IsEmpty() || Indices.IsEmpty() || FirstExposureTime >= 0.0f || GetWorld()->GetTimeSeconds()-LastExposureCheck < 0.25f) { return; }
    LastExposureCheck = GetWorld()->GetTimeSeconds();
    FVector2D Center;
    const USandLevelSettings* Settings = GetDefault<USandLevelSettings>();
    const bool Lunar = Settings->bLunarWorld && !FParse::Param(FCommandLine::Get(),TEXT("SandLegacyBox")) &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandVictoryTest")) &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandBoundaryTest")) &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandBoomRaiseTest")) &&
        !FParse::Param(FCommandLine::Get(),TEXT("SandSlopeCoastTest"));
    const float ActiveWidthCm = Lunar
        ? Settings->ActiveWidthMeters * 100.0f : 500.0f;
    if (Lunar)
    {
        FBox MeshBounds(ForceInit);
        for (const FVector& Vertex : Vertices)
        {
            MeshBounds += Vertex;
        }
        const FVector2D MeshCenter(MeshBounds.GetCenter());
        if (MeshCenter.GetAbsMax() > 0.15f * ActiveWidthCm)
        {
            // A streamed window away from the landing site has no survey
            // marker. Its absent off-window cells must not count as a hole.
            return;
        }
    }
    if (Sand::Goal::FindOpening(Vertices,Indices,Settings->ExposedSideCentimeters,Center,&InitialFloorCoverage,ActiveWidthCm))
    {
        FirstExposureTime = GetWorld()->GetTimeSeconds();
        UE_LOG(LogTemp,Display,TEXT("DIG IT UP: %s exposed at (%.1f,%.1f), countdown %.1f seconds, time %.3f"),
            Lunar ? TEXT("survey marker") : TEXT("red floor"),Center.X,Center.Y,Settings->VictoryDelaySeconds,FirstExposureTime);
    }
}

float ASandPreviewGameMode::GetVictoryCountdown() const
{
    return FirstExposureTime < 0.0f ? -1.0f : FMath::Max(0.0f,
        GetDefault<USandLevelSettings>()->VictoryDelaySeconds - (GetWorld()->GetTimeSeconds()-FirstExposureTime));
}

void ASandPreviewGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bHasWon && FirstExposureTime>=0.0f && GetVictoryCountdown()<=0.0f)
    {
        bHasWon = bVictoryVisible = true;
        UE_LOG(LogTemp,Display,TEXT("DIG IT UP: VICTORY at time %.3f"),GetWorld()->GetTimeSeconds());
        if (auto* PC=GetWorld()->GetFirstPlayerController())
        {
            PC->bShowMouseCursor=true;
            PC->bEnableClickEvents=true;
            PC->SetInputMode(FInputModeGameAndUI());
            PC->SetPause(true);
        }
        if (FParse::Param(FCommandLine::Get(),TEXT("SandVictoryTest")))
        {
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
            {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/EasyVictory.png"),true,false);
                return false;
            }),0.5f);
            if (FParse::Param(FCommandLine::Get(),TEXT("SandVictoryContinueTest")) ||
                FParse::Param(FCommandLine::Get(),TEXT("SandVictoryExitTest")))
            {
                const TWeakObjectPtr<ASandPreviewGameMode> WeakThis(this);
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
                {
                    if (WeakThis.IsValid())
                    {
                        if (auto* HUD=Cast<ASandHUD>(WeakThis->GetWorld()->GetFirstPlayerController()->GetHUD()))
                        { HUD->NotifyHitBoxClick(FParse::Param(FCommandLine::Get(),TEXT("SandVictoryExitTest")) ? TEXT("Exit") : TEXT("Continue")); }
                    }
                    return false;
                }),1.5f);
            }
        }
    }
}

void ASandPreviewGameMode::ContinueExploring()
{
    if (!bVictoryVisible) { return; }
    bVictoryVisible=false;
    if (auto* PC=GetWorld()->GetFirstPlayerController())
    {
        PC->SetPause(false);
        PC->bShowMouseCursor=false;
        PC->bEnableClickEvents=false;
        PC->SetInputMode(FInputModeGameOnly());
    }
    UE_LOG(LogTemp,Display,TEXT("DIG IT UP: free exploration resumed"));
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandFloorExposureTest,"SandSimulation.Goal.FloorExposure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSandFloorExposureTest::RunTest(const FString& Parameters)
{
    TArray<FVector> V;
    TArray<int32> T;
    const auto Quad=[&](double X0,double Y0,double X1,double Y1)
    {
        const int32 I=V.Num(); V.Append({FVector(X0,Y0,5),FVector(X1,Y0,5),FVector(X1,Y1,5),FVector(X0,Y1,5)});
        T.Append({I,I+1,I+2,I,I+2,I+3});
    };
    FVector2D Center;
    Quad(-250,-250,250,250);
    TArray<uint8> Initial;
    TestFalse(TEXT("Initial surface only establishes coverage"),Sand::Goal::FindOpening(V,T,10,Center,&Initial));
    TestFalse(TEXT("Covered floor cannot win"),Sand::Goal::FindOpening(V,T,10,Center));
    V.Reset(); T.Reset();
    Quad(-250,-250,-5,250); Quad(5,-250,250,250);
    Quad(-5,-250,5,-5); Quad(-5,5,5,250);
    TestTrue(TEXT("A connected 10 cm opening wins"),Sand::Goal::FindOpening(V,T,10,Center));
    TestTrue(TEXT("Newly excavated opening wins against initial coverage"),Sand::Goal::FindOpening(V,T,10,Center,&Initial));
    TestFalse(TEXT("Opening smaller than requested cannot win"),Sand::Goal::FindOpening(V,T,15,Center));
    Quad(-5,-5,5,5);
    TestFalse(TEXT("Reburied floor is covered"),Sand::Goal::FindOpening(V,T,10,Center));
    V.Reset(); T.Reset();
    Quad(-250,-240,250,250); Quad(-240,-250,250,-240);
    TestTrue(TEXT("A 10 cm corner excavation can win"),Sand::Goal::FindOpening(V,T,10,Center,&Initial));
    Initial.Reset();
    Sand::Goal::FindOpening(V,T,10,Center,&Initial);
    TestFalse(TEXT("Pre-existing seams do not count as excavation"),Sand::Goal::FindOpening(V,T,10,Center,&Initial));
    return true;
}
#endif

void ASandPreviewGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bChangedFixedClock) { FApp::SetUseFixedTimeStep(bPreviousFixedClock); FApp::SetFixedDeltaTime(PreviousFixedDelta); }
    Super::EndPlay(Reason);
}
