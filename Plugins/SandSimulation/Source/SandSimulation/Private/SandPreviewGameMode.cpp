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
    const float SandDepthCm = GetDefault<USandLevelSettings>()->SandDepthMeters * 100.0f;
    World->SpawnActor<ASandCollapseSurfacePreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator);

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
        Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(5.4, 5.4, 0.08));
        Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        if (FloorMaterial != nullptr)
        {
            UMaterialInterface* GoalMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_GoalFloor.M_GoalFloor"));
            Floor->GetStaticMeshComponent()->SetMaterial(0, GoalMaterial ? GoalMaterial : FloorMaterial);
            if (auto* Red = Floor->GetStaticMeshComponent()->CreateAndSetMaterialInstanceDynamic(0))
            {
                const FLinearColor RedColor = FLinearColor::FromSRGBColor(FColor(235,24,35));
                Red->SetVectorParameterValue(TEXT("Color"), RedColor);
                Red->SetVectorParameterValue(TEXT("DiffuseColor"), RedColor);
            }
        }

        // Four physical retaining walls cover the entire two-metre active sand
        // depth and extend 25 cm above the initial surface. The top remains open.
        const FLinearColor WallColor = FLinearColor::FromSRGBColor(FColor(65, 72, 84));
        const float WallZ = (SandDepthCm + 20.0f) * 0.5f;
        const float WallHeightMeters = (SandDepthCm + 30.0f) / 100.0f;
        ConfigureContainerBox(World, FVector(-256.0, 0.0, WallZ), FVector(0.12,5.24,WallHeightMeters), WallColor);
        ConfigureContainerBox(World, FVector(256.0, 0.0, WallZ), FVector(0.12,5.24,WallHeightMeters), WallColor);
        ConfigureContainerBox(World, FVector(0.0, -256.0, WallZ), FVector(5.24,0.12,WallHeightMeters), WallColor);
        ConfigureContainerBox(World, FVector(0.0, 256.0, WallZ), FVector(5.24,0.12,WallHeightMeters), WallColor);
    }

    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
        FVector(0.0, 0.0, 200.0),
        FRotator(-48.0, -32.0, 0.0));
    Sun->GetLightComponent()->SetIntensity(2.8f);
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    if (UDirectionalLightComponent* SunComponent =
        Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
    {
        SunComponent->SetAtmosphereSunLight(true);
    }

    // The lightweight Entry map has no authored environment. Spawn a real
    // atmosphere and movable skylight so the playable build has an outdoor
    // horizon instead of a black void, without loading a heavyweight level.
    World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
    ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(
        FVector(0.0, 0.0, 250.0), FRotator::ZeroRotator);
    SkyLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    SkyLight->GetLightComponent()->SetIntensity(0.65f);
    SkyLight->GetLightComponent()->RecaptureSky();

    AExponentialHeightFog* HorizonFog = World->SpawnActor<AExponentialHeightFog>(
        FVector(0.0, 0.0, -100.0), FRotator::ZeroRotator);
    HorizonFog->GetComponent()->SetFogDensity(0.0025f);
    HorizonFog->GetComponent()->SetFogHeightFalloff(0.18f);
    HorizonFog->GetComponent()->SetFogInscatteringColor(
        FLinearColor::FromSRGBColor(FColor(155, 184, 207)));
    HorizonFog->GetComponent()->SetStartDistance(700.0f);

    APointLight* Fill = World->SpawnActor<APointLight>(FVector(40.0, -80.0, 180.0), FRotator::ZeroRotator);
    Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Fill->GetLightComponent()->SetIntensity(120.0f);

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
    FVector Position=Bench?FVector(0,0,36):Fixture?FVector(-210,-160,20.5):FVector(-100,0,GetDefault<USandLevelSettings>()->SandDepthMeters*100+8);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn=GetWorld()->SpawnActor<ASandExcavatorPawn>(bRoadheader?ASandRoadheaderPawn::StaticClass():ASandExcavatorPawn::StaticClass(),Position,FRotator::ZeroRotator,Params);
    if(!Pawn) return;
    if(FParse::Param(FCommandLine::Get(),TEXT("SandSynchronous")))
        for(TActorIterator<ASandCollapseSurfacePreviewActor> It(GetWorld());It;++It) Pawn->AddTickPrerequisiteActor(*It);
    PC->Possess(Pawn);
    PC->bShowMouseCursor=false;
    PC->SetInputMode(FInputModeGameOnly());
    bSelectingVehicle=false;
    UE_LOG(LogTemp,Display,TEXT("Selected vehicle: %s"),bRoadheader?TEXT("Roadheader"):TEXT("Excavator"));

}

void ASandPreviewGameMode::CheckExposedFloor(const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
    if (bSelectingVehicle || FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"))) return;
    if (Vertices.IsEmpty() || Indices.IsEmpty() || FirstExposureTime >= 0.0f || GetWorld()->GetTimeSeconds()-LastExposureCheck < 0.25f) { return; }
    LastExposureCheck = GetWorld()->GetTimeSeconds();
    FVector2D Center;
    if (Sand::Goal::FindOpening(Vertices,Indices,GetDefault<USandLevelSettings>()->ExposedSideCentimeters,Center,&InitialFloorCoverage))
    {
        FirstExposureTime = GetWorld()->GetTimeSeconds();
        UE_LOG(LogTemp,Display,TEXT("DIG IT UP: red floor exposed at (%.1f,%.1f), countdown %.1f seconds, time %.3f"),
            Center.X,Center.Y,GetDefault<USandLevelSettings>()->VictoryDelaySeconds,FirstExposureTime);
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
