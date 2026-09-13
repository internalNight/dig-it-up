#include "SandRoadheaderPawn.h"
#include "SandMPMSolver.h"
#include "SandMachineKinematics.h"
#include "SandMachineDrive.h"
#include "SandMachineGeometry.h"
#include "SandTraction.h"
#include "SandLevelSettings.h"
#include "SandSurfacePreviewActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/GameViewportClient.h"
#include "Containers/Ticker.h"
#include "UObject/ConstructorHelpers.h"

using namespace Sand::Machine;

float ASandRoadheaderPawn::ConveyorLoop() const { return 2*(ConveyorFront-Rear)+2*PI*Radius; }

float ASandRoadheaderPawn::GetFeedFraction() const
{
    if(bAdaptiveFeed) return bDepthRelief?0.f:FMath::Clamp((.9f-ControlLoadRatio)/.3f,0.f,1.f);
    return FParse::Param(FCommandLine::Get(),TEXT("SandFeedControl"))?FeedFraction(MeasuredDraftN,DrumLoad):1.f;
}

ASandRoadheaderPawn::ASandRoadheaderPawn()
{
    ToolMount=CreateDefaultSubobject<USceneComponent>(TEXT("ConveyorMount"));
    ToolMount->SetupAttachment(ChassisBody);
    ToolMount->SetRelativeLocation(FVector(0,0,16));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Steel(TEXT("/Engine/TemplateResources/MI_Template_BaseGray_03.MI_Template_BaseGray_03"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Orange(TEXT("/Engine/TemplateResources/MI_Template_BaseOrange.MI_Template_BaseOrange"));
    // First two entries are existing track colliders; all other physical boxes
    // have a matching visible part. No invisible intake/capture volume.
    for(int32 I=2; I<64; ++I)
    {
        auto* M=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("MachinePart%d"),I));
        M->SetupAttachment(ChassisBody);
        M->SetStaticMesh(Cube.Object);
        M->SetMaterial(0,(I>=7 && I<7+HeadElementCount()) ? Orange.Object : Steel.Object);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MachineVisuals.Add(M);
    }
    VisibleMaterialPoints=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ActualMPMMaterialPoints"));
    VisibleMaterialPoints->SetupAttachment(ChassisBody);
    VisibleMaterialPoints->SetAbsolute(true,true,true);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    VisibleMaterialPoints->SetStaticMesh(Sphere.Object);
    VisibleMaterialPoints->SetMaterial(0,Steel.Object);
    VisibleMaterialPoints->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisibleMaterialPoints->SetCanEverAffectNavigation(false);
    // Prototype carrier, 120 N maximum longitudinal drive force.
    // UE forces use kg*cm/s^2. This is a tuning assumption, not measured hardware.
    DriveForce=12000;
    SteeringTorque=120000;
}

void ASandRoadheaderPawn::BeginPlay()
{
    Super::BeginPlay();
    bWorkingLayout=FParse::Param(FCommandLine::Get(),TEXT("SandWorkingLayout"));
    if(bWorkingLayout) StartHeightCm=30;
    if(FParse::Param(FCommandLine::Get(),TEXT("SandDirectReaction")) && !FParse::Param(FCommandLine::Get(),TEXT("SandSynchronous")))
        UE_LOG(LogTemp,Fatal,TEXT("SandDirectReaction requires SandSynchronous"));
    float MachineMass=ChassisBody->GetMass();
    FParse::Value(FCommandLine::Get(),TEXT("SandMachineMassKg="),MachineMass);
    ChassisBody->SetMassOverrideInKg(NAME_None,FMath::Clamp(MachineMass,1.f,500.f),true);
    if(bWorkingLayout || FParse::Param(FCommandLine::Get(),TEXT("SandWideTracks"))) {
        LeftTrackCollider->SetRelativeLocation(FVector(0,-30,-3.5));
        RightTrackCollider->SetRelativeLocation(FVector(0,30,-3.5));
        LeftTrackVisual->SetRelativeLocation(FVector(0,-30,-3.5));
        RightTrackVisual->SetRelativeLocation(FVector(0,30,-3.5));
    }
    float DriveN=DriveForce/100;
    FParse::Value(FCommandLine::Get(),TEXT("SandDriveForceN="),DriveN);
    DriveForce=100*FMath::Clamp(DriveN,1.f,2000.f);
    UE_LOG(LogTemp,Display,TEXT("MACHINE_MASS kg=%.2f driveLimitN=%.2f"),MachineMass,DriveForce/100);
    bBench=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"));
    bFeedRig=FParse::Param(FCommandLine::Get(),TEXT("SandFeedRig"));
    if(FParse::Param(FCommandLine::Get(),TEXT("SandIntakeV3"))) IntakeOffset=FVector3f(.13f,0,-.12f);
    bRaisedDrum=FParse::Param(FCommandLine::Get(),TEXT("SandRaisedDrum"));
    if(bRaisedDrum) IntakeOffset=FVector3f(0,0,.24f);
    bTransferApron=FParse::Param(FCommandLine::Get(),TEXT("SandTransferApron"));
    bApproach=FParse::Param(FCommandLine::Get(),TEXT("SandApproach"));
    bAdaptiveFeed=FParse::Param(FCommandLine::Get(),TEXT("SandAdaptiveFeed"));
    if(bTransferApron) { IntakeOffset=FVector3f(.05f,0,0); ConveyorFront=.23f; }
    if(bWorkingLayout) { IntakeOffset=FVector3f(.20f,0,.09f); ConveyorFront=.32f; bTransferApron=false; bRaisedDrum=false; }
    FParse::Value(FCommandLine::Get(),TEXT("SandRigSpeed="),RigSpeed);
    FParse::Value(FCommandLine::Get(),TEXT("SandTestDuration="),TestDuration);
    FParse::Value(FCommandLine::Get(),TEXT("SandStopAt="),StopAtSeconds);
    FParse::Value(FCommandLine::Get(),TEXT("SandFeedEnd="),FeedEndSeconds);
    bAutoTest=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderTest"));
    bStopTest=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderStopped"));
    bRunning=bAutoTest && !bStopTest;
    float RPM=DrumTargetOmega*30/PI;
    FParse::Value(FCommandLine::Get(),TEXT("SandDrumRPM="),RPM);
    DrumTargetOmega=FMath::Clamp(RPM,1.f,bWorkingLayout?90.f:40.f)*PI/30;
    bDebugPoints=FParse::Param(FCommandLine::Get(),TEXT("SandDebugPoints"));
    bChainStopped=FParse::Param(FCommandLine::Get(),TEXT("SandChainStopped"));
    bCutTest=bAutoTest && FParse::Param(FCommandLine::Get(),TEXT("SandCutTest"));
    bHoldTest=bAutoTest && FParse::Param(FCommandLine::Get(),TEXT("SandHoldTest"));
    bCutTest|=bHoldTest;
    FParse::Value(FCommandLine::Get(),TEXT("SandHead="),HeadType);
    FParse::Value(FCommandLine::Get(),TEXT("SandSoilBench="),SoilCase);
    FParse::Value(FCommandLine::Get(),TEXT("SandBladeAngle="),BladeAngle);
    FParse::Value(FCommandLine::Get(),TEXT("SandBladeDepth="),BladeDepth);
    FParse::Value(FCommandLine::Get(),TEXT("SandBladeSpeed="),BladeSpeed);
    if(bWorkingLayout) checkf(HeadType==TEXT("Paddle") || HeadType==TEXT("BucketWheel"),TEXT("Working layout supports transverse Paddle or BucketWheel"));
    if(HeadType!=TEXT("Paddle") && HeadType!=TEXT("Chevron") && HeadType!=TEXT("Spoke") && HeadType!=TEXT("Helix") && HeadType!=TEXT("BucketWheel"))
    {
        UE_LOG(LogTemp,Warning,TEXT("Unknown SandHead; using Paddle")); HeadType=TEXT("Paddle");
    }
    bPresetPitch=FParse::Value(FCommandLine::Get(),TEXT("SandHeadPitch="),HeadPitch);
    HeadPitch=FMath::Clamp(HeadPitch,-25.0f,18.0f);
    TargetPitch=HeadPitch;

    InitialChassisLocation=ChassisBody->GetComponentLocation();
    ConveyorSurface=GetWorld()->SpawnActorDeferred<ASandSurfacePreviewActor>(ASandSurfacePreviewActor::StaticClass(),FTransform::Identity,this);
    ConveyorSurface->bBuildOnBeginPlay=false;
    ConveyorSurface->bAllowAutomaticCapture=false;
    ConveyorSurface->VoxelSizeMeters=0.015f;
    ConveyorSurface->KernelRadiusMeters=0.06f;
    ConveyorSurface->IsoDensity=0.32f;
    ConveyorSurface->FinishSpawning(FTransform::Identity);
    ConveyorSurface->SetActorHiddenInGame(true);
    VisibleMaterialPoints->SetVisibility(bDebugPoints);
    BoomPivot->SetVisibility(false,true);
    for(auto* M : {ChassisVisual.Get(),UpperDeckVisual.Get(),CabVisual.Get(),WindshieldVisual.Get(),CounterweightVisual.Get()})
        M->SetVisibility(false);
    // Open top and separated tracks expose the working channel from both views.
    CameraBoom->TargetArmLength=235;
    CameraBoom->SetRelativeRotation(FRotator(-32,-55,0));
    if(bBench)
    {
        ToolMount->SetRelativeLocation(FVector(0,0,4));
        ChassisBody->SetSimulatePhysics(false);
        ChassisBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if(bFeedRig) { ChassisBody->SetSimulatePhysics(false); ChassisBody->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
    float HeightCm=bWorkingLayout?20.f:ToolMount->GetRelativeLocation().Z;
    FParse::Value(FCommandLine::Get(),TEXT("SandHeadHeightCm="),HeightCm);
    WorkingHeightCm=FMath::Clamp(HeightCm,-10.0f,50.0f);
    ToolMount->SetRelativeLocation(FVector(0,0,bApproach?StartHeightCm:WorkingHeightCm));
    ToolMount->SetRelativeRotation(FRotator(HeadPitch,0,0));
    LastControlLocation=InitialChassisLocation;
    // Initial level-bed reference; subsequent depth follows measured travel,
    // never a prescribed grain path. Manual Q/E overrides automatic approach.
    ReferenceHeadZ=ToolMount->GetComponentTransform().TransformPosition(FVector(51,0,1)+FVector(IntakeOffset)*100).Z/100
        +(FMath::Max(WorkingHeightCm,20.f)-ToolMount->GetRelativeLocation().Z)*ChassisBody->GetUpVector().Z/100;
    DesiredHeadZ=ReferenceHeadZ;
    UE_LOG(LogTemp,Display,TEXT("HEAD_CONFIG type=%s pitchDeg=%.2f mountHeightCm=%.2f fixedBench=%d"),*HeadType,HeadPitch,HeightCm,bBench);
    auto* HeadMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/TemplateResources/MI_Template_BaseOrange.MI_Template_BaseOrange"));
    for(int32 I=7;I<7+HeadElementCount();++I) MachineVisuals[I-2]->SetMaterial(0,HeadMaterial);
    if(FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderInside")))
    {
        bInternalCamera=true;
        CameraBoom->bDoCollisionTest=false;
        CameraBoom->bEnableCameraLag=false;
        CameraBoom->TargetArmLength=0;
        CameraBoom->SetRelativeLocation(FVector(-65,0,43));
        CameraBoom->SetRelativeRotation(FRotator(-16,0,0));
        FollowCamera->FieldOfView=78;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("SandHeadInspect")))
    {
        CameraBoom->bDoCollisionTest=false;
        CameraBoom->bEnableCameraLag=false;
        CameraBoom->TargetArmLength=210;
        CameraBoom->SetRelativeLocation(FVector(35,0,15));
        CameraBoom->SetRelativeRotation(FRotator(-22,135,0));
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("SandGeometryAudit"))) {
        const auto OriginalRotation=ToolMount->GetRelativeRotation();
        const auto OriginalLocation=ToolMount->GetRelativeLocation();
        ToolMount->SetRelativeLocation(FVector(0,0,WorkingHeightCm));
        ToolMount->SetRelativeRotation(FRotator(TargetPitch,0,0));
        DrumOmega=1.8f; ChainSpeed=.3f;
        TMap<FIntPoint,float> Depths;
        for(int32 S=0;S<1200;++S) {
            Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,S/60.f);
            const int32 FirstChain=7+HeadElementCount(), EndChain=FirstChain+BladeCount;
            for(int32 I=2;I<(int32)T.ColliderCount;++I) for(int32 J=0;J<(int32)T.ColliderCount;++J) {
                if(!(I>=(bWorkingLayout?5:7) && I<EndChain) && T.Colliders[I].ChainDriveRatio==0) continue;
                if(I==J) continue;
                if(T.Colliders[I].ChainDriveRatio!=0 && T.Colliders[J].ChainDriveRatio!=0) continue;
                if(J==5 || J==6 || (J>=7 && J<FirstChain)) continue;
                if(I>=FirstChain && J>=FirstChain && J<EndChain) continue;
                if(I<FirstChain && J<2) continue;
                const float D=BoxOverlapDepth(T.Colliders[I],T.Colliders[J]);
                if(D>.001f) Depths.FindOrAdd(FIntPoint(I,J))=FMath::Max(D,Depths.FindRef(FIntPoint(I,J)));
            }
        }
        for(const auto& Entry:Depths) UE_LOG(LogTemp,Display,TEXT("GEOMETRY_OVERLAP a=%d b=%d depthMm=%.3f"),Entry.Key.X,Entry.Key.Y,1000*Entry.Value);
        UE_LOG(LogTemp,Display,TEXT("GEOMETRY_AUDIT independentPairsOver1mm=%d"),Depths.Num());
        Sand::MPM::FToolColliderState Geometry; BuildPhysicalTool(Geometry,0);
        FString GeometryCSV=TEXT("id,motion,shape,x,y,z,hx,hy,hz,axx,axy,axz,ayx,ayy,ayz,azx,azy,azz\n");
        for(uint32 I=0;I<Geometry.ColliderCount;++I) {
            const auto& C=Geometry.Colliders[I];
            GeometryCSV+=FString::Printf(TEXT("%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),I,C.Motion,C.Shape,C.CenterMeters.X,C.CenterMeters.Y,C.CenterMeters.Z,
                C.HalfExtentsMeters.X,C.HalfExtentsMeters.Y,C.HalfExtentsMeters.Z,C.AxisX.X,C.AxisX.Y,C.AxisX.Z,C.AxisY.X,C.AxisY.Y,C.AxisY.Z,C.AxisZ.X,C.AxisZ.Y,C.AxisZ.Z);
        }
        FFileHelper::SaveStringToFile(GeometryCSV,*(FPaths::ProjectSavedDir()/TEXT("TransportGeometry.csv")));
        DrumOmega=ChainSpeed=0; ToolMount->SetRelativeRotation(OriginalRotation); ToolMount->SetRelativeLocation(OriginalLocation);
        checkf(Depths.IsEmpty(),TEXT("Independent machine solids overlap; fix assembly before transport testing"));
    }
    UpdateMachineVisuals();
}

void ASandRoadheaderPawn::Tick(float Dt)
{
    if(!bBench && !bFeedRig) Super::Tick(Dt);
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC) return;
    if(PC->WasInputKeyJustPressed(EKeys::Escape)) PC->ConsoleCommand(TEXT("quit"));
    if(PC->WasInputKeyJustPressed(EKeys::P))
    {
        bDebugPoints=!bDebugPoints;
        VisibleMaterialPoints->SetVisibility(bDebugPoints);
    }
    if(PC->WasInputKeyJustPressed(EKeys::T)) bRunning=!bRunning;
    if(PC->WasInputKeyJustPressed(EKeys::G)) Direction=-Direction;
    if(PC->WasInputKeyJustPressed(EKeys::C))
    {
        bInternalCamera=!bInternalCamera;
        CameraBoom->bDoCollisionTest=!bInternalCamera;
        CameraBoom->bEnableCameraLag=!bInternalCamera;
        CameraBoom->TargetArmLength=bInternalCamera?0:235;
        CameraBoom->SetRelativeLocation(bInternalCamera?FVector(-65,0,43):FVector(0,0,28));
        CameraBoom->SetRelativeRotation(bInternalCamera?FRotator(-16,0,0):FRotator(-32,-55,0));
        FollowCamera->FieldOfView=bInternalCamera?78:58;
    }
    if(!bBench)
    {
        const float Input=(PC->IsInputKeyDown(EKeys::Q)?1.0f:0.0f)-(PC->IsInputKeyDown(EKeys::E)?1.0f:0.0f);
        if(Input!=0) bApproach=false;
        if(bAdaptiveFeed && PC->WasInputKeyJustPressed(EKeys::R)) bApproach=true;
        HeightInput=Input;
        if(!bTransferApron && !bRaisedDrum && !bWorkingLayout && !bApproach) HeadPitch=FMath::Clamp(HeadPitch+Input*Dt*12,-25.0f,18.0f);
        if(bCutTest && !bPresetPitch && !FParse::Param(FCommandLine::Get(),TEXT("SandFeedControl")))
            HeadPitch=-FMath::Min(15.0f,PhysicalTime*4.0f);
        if(!bPresetPitch && FParse::Param(FCommandLine::Get(),TEXT("SandFeedControl"))) {
            // Load-limited depth command. Lift under overload; approach slowly.
            if(FMath::Abs(MeasuredDraftN)>65 || FMath::Abs(DrumLoad)>180) HeadPitch=FMath::Min(18.f,HeadPitch+Dt*5);
            else if(bCutTest) HeadPitch=FMath::Max(-15.f,HeadPitch-Dt*.8f);
        }
        ToolMount->SetRelativeRotation(FRotator(HeadPitch,0,0));
    }
    UpdateMachineVisuals();
}

void ASandRoadheaderPawn::BuildPhysicalTool(Sand::MPM::FToolColliderState& Tool,float Seconds)
{
    using namespace Sand::MPM;
    Tool=FToolColliderState{};
    if(!SoilCase.IsEmpty()) {
        Tool.AddCollider().CenterMeters=FVector3f(0,0,-10);
        Tool.AddCollider().CenterMeters=FVector3f(0,0,-10);
        if(SoilCase==TEXT("Blade")) {
            auto& C=Tool.AddCollider();
            const float A=FMath::DegreesToRadians(BladeAngle);
            C.AxisX=FVector3f(FMath::Sin(A),0,-FMath::Cos(A));
            C.AxisY=FVector3f(0,1,0); C.AxisZ=FVector3f(FMath::Cos(A),0,FMath::Sin(A));
            C.CenterMeters=FVector3f(.94f-BladeSpeed*FMath::Clamp(PhysicalTime-1.f,0.f,7.f),0,.42f-(BladeDepth+.02f)*FMath::Min(PhysicalTime,1.f))+.15f*C.AxisZ;
            C.HalfExtentsMeters=FVector3f(.0125f,.14f,.15f);
            C.LinearVelocityMetersPerSecond=FVector3f(PhysicalTime>=1 && PhysicalTime<8 ? -BladeSpeed:0,0,0);
            if(PhysicalTime<1) C.LinearVelocityMetersPerSecond.Z=-(BladeDepth+.02f);
            C.SeparationSpeedLimit=.05f;
        }
        return;
    }
    const FTransform Mount=ToolMount->GetComponentTransform();
    const FVector3f Origin(Mount.GetLocation()/100.0);
    const FQuat4f Rotation(Mount.GetRotation());
    auto Box=[&](FVector3f Pos,FVector3f Half)
    {
        auto& C=Tool.AddCollider();
        C.CenterMeters=Origin+Rotation.RotateVector(Pos);
        C.AxisX=Rotation.GetAxisX(); C.AxisY=Rotation.GetAxisY(); C.AxisZ=Rotation.GetAxisZ();
        C.HalfExtentsMeters=Half;
        return &C;
    };
    for(auto* Track : {LeftTrackCollider.Get(),RightTrackCollider.Get()})
    {
        auto& C=Tool.AddCollider(); const auto X=Track->GetComponentTransform();
        C.CenterMeters=FVector3f(X.GetLocation()/100.0);
        C.AxisX=FVector3f(X.GetUnitAxis(EAxis::X)); C.AxisY=FVector3f(X.GetUnitAxis(EAxis::Y)); C.AxisZ=FVector3f(X.GetUnitAxis(EAxis::Z));
        C.HalfExtentsMeters=FVector3f(Track->GetScaledBoxExtent()/100.0);
    }
    // A horizontal tray, open at both ends; returning slats pass below the floor.
    Box(FVector3f((ConveyorFront+Rear)/2,0,0),FVector3f((ConveyorFront-Rear)/2,.22f,.015f));
    Box(FVector3f(-.15f,-.235f,.065f),FVector3f(.47f,.015f,.08f));
    Box(FVector3f(-.15f,.235f,.065f),FVector3f(.47f,.015f,.08f));
    const bool Axial=HeadType==TEXT("Spoke") || HeadType==TEXT("Helix");
    Box(Axial?FVector3f(.59f,0,.16f):FVector3f(.51f,0,.01f)+IntakeOffset,Axial?FVector3f(.24f,.035f,.035f):FVector3f(.10f,.23f,.10f));
    Box(Axial?FVector3f(.59f,0,.16f):FVector3f(.51f,0,.01f)+IntakeOffset,Axial?FVector3f(.24f,.035f,.035f):FVector3f(.07f,bWorkingLayout?.23f:.255f,.07f));
    if(bWorkingLayout) for(int32 I=5;I<7;++I) {
        // The hub is part of the rotating assembly, never a stationary square
        // stator intersecting its own flights. Its contact contributes torque.
        auto& C=Tool.Colliders[I]; C.Motion=3; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.RotorCenter=FVector3f(.51f,0,.01f)+IntakeOffset; C.RotorOffset=FVector3f::ZeroVector;
        C.Phase=DrumAngle; C.Speed=-DrumOmega; C=SampleMachineCollider(C,Seconds);
    }
    for(int32 I=0;I<HeadElementCount();++I)
    {
        auto& C=Tool.AddCollider(); C.Motion=1; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.Phase=DrumAngle+I*2*PI/DrumCount; C.Speed=-DrumOmega;
        C.OrbitRadius=bWorkingLayout?.185f:.145f;
        C.HalfExtentsMeters=FVector3f(.055f,bWorkingLayout?.205f:.23f,.022f);
        if(HeadType==TEXT("BucketWheel")) {
            C.Motion=3; C.Phase=DrumAngle;
            const int32 Parts=bWorkingLayout?3:2, Part=I%Parts;
            const float A=(I/Parts)*2*PI/(bWorkingLayout?6:8);
            const FQuat4f Around(FVector3f(0,1,0),A);
            FVector3f Offset;
            if(bWorkingLayout) {
                // U-shaped pocket: radial floor and BOTH radial retaining walls.
                // The inner wall stops early inward loss while passing the crown.
                Offset=Part==0?FVector3f(.165f,0,0):FVector3f(Part==1?.222f:.105f,0,.035f);
                C.HalfExtentsMeters=Part==0?FVector3f(.075f,.205f,.022f):FVector3f(.018f,.205f,.05f);
            } else {
                Offset=Part?FVector3f(.18f,0,.04f):FVector3f(.145f,0,0);
                C.HalfExtentsMeters=Part?FVector3f(.02f,.23f,.05f):FVector3f(.06f,.23f,.02f);
            }
            C.RotorOffset=Around.RotateVector(Offset);
            C.RotorOrientation=Around;
            if(bWorkingLayout && I>=18) {
                // Three intersecting radial rectangles form each closed side
                // disc. Six pockets and their side discs share one shaft.
                C.RotorOffset=FVector3f(0,(I-18)/3==0?-.22f:.22f,0);
                C.RotorOrientation=FQuat4f(FVector3f(0,1,0),((I-18)%3)*PI/3);
                C.HalfExtentsMeters=FVector3f(.24f,.015f,.12f);
            }
        }
        else if(HeadType!=TEXT("Paddle"))
        {
            C.Motion=3; C.Phase=DrumAngle;
            if(HeadType==TEXT("Chevron"))
            {
                // Four staggered pairs of short, oppositely skewed paddles.
                const float Side=I%2 ? 1.0f:-1.0f;
                const float A=(I/2)*2*PI/4+Side*.20f;
                const FQuat4f Around(FVector3f(0,1,0),A);
                C.RotorOffset=Around.RotateVector(FVector3f(.145f,Side*.115f,0));
                C.RotorOrientation=Around*FQuat4f(FVector3f(1,0,0),Side*.40f);
                C.HalfExtentsMeters=FVector3f(.055f,.10f,.022f);
            }
            else
            {
                C.RotorAxis=FVector3f(1,0,0);
                const float A=I*2*PI/HeadElementCount();
                const FQuat4f Around(C.RotorAxis,A);
                if(HeadType==TEXT("Spoke"))
                {
                    C.RotorOffset=Around.RotateVector(FVector3f(0,.125f,0));
                    C.RotorOrientation=Around;
                    C.HalfExtentsMeters=FVector3f(.025f,.09f,.025f);
                }
                else
                {
                    // One turn assembled from inclined plates; deliberately
                    // labelled segmented helix, not a continuous auger flight.
                    // Negative pitch matches the plate normal and negative
                    // rotation: the screw's axial phase travels toward -X.
                    C.RotorOffset=Around.RotateVector(FVector3f(.225f-I*.015f,.12f,0));
                    C.RotorOrientation=Around*FQuat4f(FVector3f(0,1,0),-.445f);
                    C.HalfExtentsMeters=FVector3f(.014f,.085f,.033f);
                }
            }
        }
        if(!Axial) C.RotorCenter+=IntakeOffset;
        C=SampleMachineCollider(C,Seconds);
    }
    for(int32 I=0;I<BladeCount;++I)
    {
        auto& C=Tool.AddCollider(); C.Motion=2; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.ChainFront=ConveyorFront;
        C.Phase=ChainDistance+I*ConveyorLoop()/BladeCount; C.Speed=ChainSpeed;
        C.HalfExtentsMeters=FVector3f(.018f,.205f,bWorkingLayout?.014f:.028f);
        if(bWorkingLayout) { C.SurfaceOffset=.014f; C.ChainDriveRatio=1; }
        C=SampleMachineCollider(C,Seconds);
    }
    // Enclose the sides and top of the drum, leaving the front intake and
    // rear-to-tray route open. Guides return thrown material by solid contact.
    Box(FVector3f(.50f,-.26f,.07f)+IntakeOffset,FVector3f(bWorkingLayout?.27f:.22f,.02f,bWorkingLayout?.22f:.18f));
    Box(FVector3f(.50f,.26f,.07f)+IntakeOffset,FVector3f(bWorkingLayout?.27f:.22f,.02f,bWorkingLayout?.22f:.18f));
    Box(FVector3f(.48f,0,Axial?.40f:bWorkingLayout?.305f:.265f)+IntakeOffset,FVector3f(.24f,.24f,.015f));
    if(Axial) {
        // Catch material below the axial head and bridge the previous open gap.
        // A sloping shoe rises toward the rear tray; no particle capture.
        auto* Shoe=Box(FVector3f(.50f,0,-.065f),FVector3f(.24f,.23f,.015f));
        const FQuat4f Tilt=Rotation*FQuat4f(FVector3f(0,1,0),.35f);
        Shoe->AxisX=Tilt.GetAxisX(); Shoe->AxisY=Tilt.GetAxisY(); Shoe->AxisZ=Tilt.GetAxisZ();
        // Move axial rotation above the shoe so the lower blade clears it.
        for(int32 I=7;I<7+HeadElementCount();++I) {
            auto& C=Tool.Colliders[I]; C.RotorCenter=FVector3f(.59f,0,.16f);
            C=SampleMachineCollider(C,Seconds);
        }
    }
    if(bTransferApron) {
        // Tangential receiving shoe below the front sprocket; all boundaries
        // are visible solids. The flight turns upward over this shoe.
        auto* Apron=Box(FVector3f(.29f,0,-.12f),FVector3f(.0814f,.22f,.0125f));
        const FQuat4f Q=Rotation*FQuat4f(FVector3f(0,1,0),-FMath::Atan2(.11f,.12f));
        Apron->AxisX=Q.GetAxisX(); Apron->AxisY=Q.GetAxisY(); Apron->AxisZ=Q.GetAxisZ();
    }
    if(bWorkingLayout) {
        // Gravity transfer chute outside both swept moving envelopes. Rear
        // edge is above the front flights, so sand falls onto their upper run.
        auto* Chute=Box(FVector3f(.40f,0,.16f),FVector3f(.0781f,.22f,.0125f));
        const FQuat4f Q=Rotation*FQuat4f(FVector3f(0,1,0),-FMath::Atan2(.12f,.10f));
        Chute->AxisX=Q.GetAxisX(); Chute->AxisY=Q.GetAxisY(); Chute->AxisZ=Q.GetAxisZ();
    }
    if(bWorkingLayout || FParse::Param(FCommandLine::Get(),TEXT("SandReturnGuard"))) {
        // Sealed return channel: real visible housing keeps ground out of the
        // counter-moving lower flights. Upper run and rear discharge stay open.
        Box(FVector3f((ConveyorFront+Rear)/2,0,-.145f),FVector3f((ConveyorFront-Rear)/2,.235f,.015f));
        for(float Y : {-.235f,.235f}) Box(FVector3f((ConveyorFront+Rear)/2,Y,-.085f),FVector3f((ConveyorFront-Rear)/2+(bWorkingLayout?.10f:.16f),.015f,.085f));
        for(int32 End=0;End<(bWorkingLayout?1:2);++End) for(int32 I=0;I<3;++I) {
            const float A=-PI/2+(I+.5f)*PI/6;
            const float Sign=End==0?1.f:-1.f;
            const float CX=End==0?ConveyorFront:Rear;
            auto* Guard=Box(FVector3f(CX+Sign*.13f*FMath::Cos(A),0,Top-Radius+.13f*FMath::Sin(A)),FVector3f(.038f,.235f,.015f));
            Guard->AxisX=Rotation.RotateVector(FVector3f(-Sign*FMath::Sin(A),0,FMath::Cos(A)));
            Guard->AxisY=Rotation.GetAxisY();
            Guard->AxisZ=FVector3f::CrossProduct(Guard->AxisX,Guard->AxisY);
        }
    }
    if(bWorkingLayout) {
        // Continuous moving belt: straight skins and real cylindrical end drums.
        // Contact has finite Coulomb friction and contributes to motor load.
        auto& Upper=Tool.Colliders[2]; Upper.CenterMeters=Origin+Rotation.RotateVector(FVector3f((ConveyorFront+Rear)/2,0,.03f));
        Upper.HalfExtentsMeters=FVector3f((ConveyorFront-Rear)/2,.205f,.015f);
        Upper.Motion=4; Upper.MotionRotation=Rotation; Upper.Speed=-ChainSpeed; Upper.ChainDriveRatio=-1;
        Upper=SampleMachineCollider(Upper,Seconds);
        auto* Lower=Box(FVector3f((ConveyorFront+Rear)/2,0,-.07f),FVector3f((ConveyorFront-Rear)/2,.205f,.015f));
        Lower->Motion=4; Lower->MotionRotation=Rotation; Lower->Speed=ChainSpeed; Lower->ChainDriveRatio=1;
        *Lower=SampleMachineCollider(*Lower,Seconds);
        for(float X : {Rear,ConveyorFront}) {
            auto* Roller=Box(FVector3f(X,0,Top-Radius),FVector3f(Radius,.205f,Radius));
            Roller->Shape=1; Roller->Motion=3; Roller->MotionOrigin=Origin; Roller->MotionRotation=Rotation;
            Roller->RotorCenter=FVector3f(X,0,Top-Radius); Roller->RotorOffset=FVector3f::ZeroVector;
            Roller->Phase=RollerAngle; Roller->Speed=-ChainSpeed/Radius; Roller->ChainDriveRatio=-1/Radius;
            *Roller=SampleMachineCollider(*Roller,Seconds);
        }
    }
    // Motion is physical contact only; never enable the excavator carrier.
    for(uint32 I=0;I<Tool.ColliderCount;++I)
    {
        auto& C=Tool.Colliders[I];
        const FVector3f V=bFeedRig?FVector3f(PhysicalTime>=RigStart() && PhysicalTime<RigStart()+16?RigSpeed:0,0,0):bBench?FVector3f::ZeroVector:FVector3f(ChassisBody->GetPhysicsLinearVelocityAtPoint(FVector(C.CenterMeters)*100)/100.0);
        C.SeparationSpeedLimit=0.15f;
        C.BaseVelocity=V+(I>=2?FVector3f(GetActorUpVector())*LiftSpeedMps:FVector3f::ZeroVector);
        C.LinearVelocityMetersPerSecond+=C.BaseVelocity;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("SandLiftGuide")) && !Axial) {
        // Real visible containment above the exposed cutting quadrant. The
        // paddles push sand against this curved guide until rear/top release.
        const int32 GuideCount=5;
        for(int32 I=0;I<GuideCount;++I) {
            const float A=bWorkingLayout?(-PI/6+(I+.5f)*(2*PI/3)/GuideCount):(I+.5f)*(PI/2)/5;
            const float GuideRadius=bWorkingLayout?.285f:.25f;
            auto* Guide=Box(FVector3f(.51f+GuideRadius*FMath::Cos(A),0,.01f+GuideRadius*FMath::Sin(A))+IntakeOffset,FVector3f(bWorkingLayout?.061f:.043f,.25f,.0125f));
            Guide->AxisX=Rotation.RotateVector(FVector3f(-FMath::Sin(A),0,FMath::Cos(A)));
            Guide->AxisY=Rotation.GetAxisY();
            Guide->AxisZ=Rotation.RotateVector(FVector3f(-FMath::Cos(A),0,-FMath::Sin(A)));
            Guide->BaseVelocity=bFeedRig?FVector3f(PhysicalTime>=RigStart() && PhysicalTime<RigStart()+16?RigSpeed:0,0,0):FVector3f(ChassisBody->GetPhysicsLinearVelocity()/100);
            Guide->BaseVelocity+=FVector3f(GetActorUpVector())*LiftSpeedMps;
            Guide->LinearVelocityMetersPerSecond=Guide->BaseVelocity;
            Guide->SeparationSpeedLimit=.15f;
        }
    }
    Tool.bBucketInteriorEnabled=false;
    if(bBench)
    {
        auto& Shelf=Tool.AddCollider();
        Shelf.CenterMeters=FVector3f(.85f,0,.285f);
        Shelf.HalfExtentsMeters=FVector3f(.2f,.25f,.015f);
        for(float Y : {-0.225f,0.225f})
        {
            auto& Wall=Tool.AddCollider();
            Wall.CenterMeters=FVector3f(.85f,Y,.475f);
            Wall.HalfExtentsMeters=FVector3f(.2f,.025f,.175f);
        }
        auto& Feed=Tool.AddCollider();
        Feed.CenterMeters=FVector3f(FMath::Max(.73f,1.08f-PhysicalTime*.035f),0,.475f);
        Feed.HalfExtentsMeters=FVector3f(.025f,.20f,.175f);
        Feed.LinearVelocityMetersPerSecond=FVector3f(PhysicalTime<10 ? -.035f:0,0,0);
    
    }
}

void ASandRoadheaderPawn::UpdateMachineVisuals()
{
    Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,0);
    for(int32 J=0;J<MachineVisuals.Num();++J) MachineVisuals[J]->SetVisibility(J+2<(int32)T.ColliderCount);
    for(int32 I=2;I<(int32)T.ColliderCount && I-2<MachineVisuals.Num();++I)
    {
        const auto& C=T.Colliders[I];
        FMatrix M=FMatrix::Identity;
        const FVector X(C.AxisX),Y(C.AxisY),Z(C.AxisZ);
        const FVector CylinderY=Z,CylinderZ=-Y;
        M.SetAxes(&X,C.Shape==1?&CylinderY:&Y,C.Shape==1?&CylinderZ:&Z);
        auto* V=MachineVisuals[I-2].Get();
        static UStaticMesh* CubeMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        static UStaticMesh* CylinderMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        V->SetStaticMesh(C.Shape==1?CylinderMesh:CubeMesh);
        // Explicit cutaway inspection: hide casing and feeder visuals only.
        // Their contact geometry remains active; never use this as an open-case test.
        const bool Cutaway=FParse::Param(FCommandLine::Get(),TEXT("SandHeadInspect"));
        V->SetVisibility(!(Cutaway && I>=7+HeadElementCount()+BladeCount));
        V->SetWorldLocationAndRotation(FVector(C.CenterMeters)*100,FQuat(M));
        V->SetWorldScale3D(C.Shape==1?FVector(C.HalfExtentsMeters.X,C.HalfExtentsMeters.X,C.HalfExtentsMeters.Y)*2:FVector(C.HalfExtentsMeters)*2);
    }
}

bool ASandRoadheaderPawn::IsConveyorRegion(const FVector3f& Position) const
{
    const FVector L=ToolMount->GetComponentTransform().InverseTransformPosition(FVector(Position)*100);
    return L.X>-72 && L.X<32 && FMath::Abs(L.Y)<23 && L.Z>1.5 && L.Z<20;
}

void ASandRoadheaderPawn::CompletePhysicalStep(const Sand::MPM::FToolInteractionResult& R,
    float Dt,const TArray<Sand::MPM::FParticleData>& Particles)
{
    if(!SoilCase.IsEmpty()) { CompleteSoilStep(R,Dt,Particles); return; }
    Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,Dt*0.5f);
    Sand::MPM::FToolColliderState EndTool;
    if(!Particles.IsEmpty()) {
        // Use the same initial collider and complete motion law as the solver.
        // BuildPhysicalTool(Dt) samples rotor phase before assigning base velocity
        // and is not a substitute for the translated end-of-step pose.
        BuildPhysicalTool(EndTool,0);
        for(uint32 I=0;I<EndTool.ColliderCount;++I) EndTool.Colliders[I]=Sand::MPM::SampleMachineCollider(EndTool.Colliders[I],Dt);
    }
    const bool Axial=HeadType==TEXT("Spoke") || HeadType==TEXT("Helix");
    const FVector3f DrumCenter(ToolMount->GetComponentTransform().TransformPosition(Axial?FVector(59,0,16):FVector(51,0,1)+FVector(IntakeOffset)*100)/100.0);
    const FVector3f Axis((HeadType==TEXT("Spoke") || HeadType==TEXT("Helix"))?ToolMount->GetForwardVector():ToolMount->GetRightVector());
    float DrumImpulse=0, ChainImpulse=0;
    for(int32 I=bWorkingLayout?5:7;I<7+HeadElementCount();++I)
    {
        const FVector3f Torque=R.ColliderAngular[I]+FVector3f::CrossProduct(T.Colliders[I].CenterMeters-DrumCenter,R.ColliderLinear[I]);
        DrumImpulse-=FVector3f::DotProduct(Torque,Axis);
    }
    if(bWorkingLayout) {
        for(uint32 I=0;I<T.ColliderCount;++I) if(T.Colliders[I].ChainDriveRatio!=0) {
            auto Unit=T.Colliders[I];
            Unit.Phase+=Unit.Speed*Dt*.5f; Unit.Speed=Unit.ChainDriveRatio; Unit.BaseVelocity=FVector3f::ZeroVector;
            Unit=Sand::MPM::SampleMachineCollider(Unit,0);
            ChainImpulse+=FVector3f::DotProduct(R.ColliderLinear[I],Unit.LinearVelocityMetersPerSecond)
                +FVector3f::DotProduct(R.ColliderAngular[I],Unit.AngularVelocityRadiansPerSecond);
        }
    } else for(int32 I=7+HeadElementCount();I<7+HeadElementCount()+BladeCount;++I)
        ChainImpulse+=FVector3f::DotProduct(R.ColliderLinear[I],T.Colliders[I].AxisX);
    DrumLoad=DrumImpulse/FMath::Max(Dt,1.e-6f);
    FVector HeadReaction=FVector::ZeroVector;
    for(int32 I=7;I<7+HeadElementCount();++I) HeadReaction-=FVector(R.ColliderLinear[I])/Dt;
    MeasuredDraftN=FVector::DotProduct(HeadReaction,GetActorForwardVector());
    ChainLoad=ChainImpulse/FMath::Max(Dt,1.e-6f);
    // Advance angles using exactly the speed submitted to the completed GPU step.
    DrumAngle=FMath::Fmod(DrumAngle-DrumOmega*Dt,2*PI);
    ChainDistance=FMath::Fmod(ChainDistance+ChainSpeed*Dt,ConveyorLoop());
    RollerAngle=FMath::Fmod(RollerAngle-ChainSpeed*Dt/Radius,2*PI);
    // Low-speed, high-torque geared drives. An anti-rollback clutch prevents
    // uncommanded reversal under overload; excess load can still stall the drum.
    if(StopAtSeconds>=0 && PhysicalTime>=StopAtSeconds) { bRunning=false; StopAtSeconds=-1; }
    DrumOmega=DrivenSpeed(DrumOmega,bRunning?Direction*DrumTargetOmega:0,DrumImpulse,Dt,12,180,240);
    ChainSpeed=DrivenSpeed(ChainSpeed,bRunning && !bChainStopped?Direction*.30f:0,ChainImpulse,Dt,50,1000,500);
    if(bRunning) { DrumOmega=Direction*FMath::Max(0.0f,Direction*DrumOmega); }
    if(bRunning && !bChainStopped) { ChainSpeed=Direction*FMath::Max(0.0f,Direction*ChainSpeed); }
    if(!bRunning) DrumOmega=BrakeSpeed(DrumOmega,Dt,12,300);
    if(!bRunning || bChainStopped) ChainSpeed=BrakeSpeed(ChainSpeed,Dt,50,1000);
    DrumOmega=FMath::Clamp(DrumOmega,-1.2f*DrumTargetOmega,1.2f*DrumTargetOmega);
    ChainSpeed=FMath::Clamp(ChainSpeed,-.4f,.4f);
    PhysicalTime+=Dt;
    if(bRunning) MotorTime+=Dt;
    if(bAdaptiveFeed) {
        FVector Reaction=FVector::ZeroVector;
        for(uint32 I=2;I<T.ColliderCount;++I) Reaction-=FVector(R.ColliderLinear[I])/Dt;
        const float Budget=FMath::Max(25.f,FMath::Min(DriveForce/100,bFeedRig?DriveForce/100:GetTractionBudgetN()));
        const float Load=FVector2D(Reaction.X,Reaction.Y).Size()/Budget;
        // Sensor smoothing only. The complete contact reaction is still
        // applied, uncapped, to the chassis in the coupling callback.
        ControlLoadRatio=FMath::Lerp(ControlLoadRatio,Load,1-FMath::Exp(-Dt/.15f));
        const float ForwardSpeed=FVector::DotProduct(ChassisBody->GetPhysicsLinearVelocity(),GetActorForwardVector())/100;
        if(ControlLoadRatio>.85f || ForwardSpeed<-.02f) bDepthRelief=true;
        else if(ControlLoadRatio<.55f && ForwardSpeed>-.005f) bDepthRelief=false;
    }
    if(bApproach || bTransferApron || bRaisedDrum || bWorkingLayout) {
        const float OldHeight=ToolMount->GetRelativeLocation().Z;
        float NewHeight=bApproach?FMath::Max(WorkingHeightCm,StartHeightCm-FMath::Max(0.f,MotorTime-1.f)*2.5f):FMath::Clamp(OldHeight+HeightInput*Dt*4.f,-6.f,StartHeightCm);
        if(bAdaptiveFeed && bApproach) {
            const FVector Position=ChassisBody->GetComponentLocation();
            if(bRunning) ControlAdvanceM+=float(FVector::DotProduct(Position-LastControlLocation,GetActorForwardVector())/100);
            LastControlLocation=Position;
            DesiredHeadZ=ReferenceHeadZ-(FMath::Max(20.f,WorkingHeightCm)-WorkingHeightCm)/100*FMath::Clamp(ControlAdvanceM/.48f,0.f,1.f);
            const float TargetHeight=FMath::Clamp(OldHeight+100*(DesiredHeadZ-DrumCenter.Z)/FMath::Max(.5f,float(ChassisBody->GetUpVector().Z)),-6.f,StartHeightCm);
            NewHeight=OldHeight;
            if(bRunning && MotorTime>1) NewHeight=bDepthRelief?FMath::Min(StartHeightCm,OldHeight+4.f*Dt)
                :FMath::FInterpConstantTo(OldHeight,TargetHeight,Dt,1.5f);
        }
        LiftSpeedMps=(NewHeight-OldHeight)/(100*Dt);
        ToolMount->SetRelativeLocation(FVector(0,0,NewHeight));
    }
    // Diagnostic carriage moves the visible machine, never sand particles.
    if(bFeedRig) ChassisBody->SetWorldLocation(InitialChassisLocation+FVector(100*RigSpeed*FMath::Clamp(PhysicalTime-RigStart(),0.f,16.f),0,0));
    UpdateMachineVisuals();
    if(!Particles.IsEmpty())
    {
        // Surface reconstruction may hide sparse conveyed material. Show the
        // actual continuum sample positions near the machine as instanced dots.
        // They have no collision, mass or independent particle simulation.
        VisibleMaterialPoints->ClearInstances();
        TArray<FTransform> PointTransforms;
        TArray<FVector3f> ConveyorPositions;
        if(TransportHistory.Num()!=Particles.Num()) { TransportHistory.Init(0,Particles.Num()); PreviousLocalPositions.Init(FVector3f(1.e6f),Particles.Num()); }
        RotorMass=TroughMass=0;
        for(int32 I=0;I<Particles.Num();++I)
        {
            const auto& P=Particles[I];
            const FVector L=ToolMount->GetComponentTransform().InverseTransformPosition(FVector(FVector3f(P.PositionAndMass))*100);
            if(IsConveyorRegion(FVector3f(P.PositionAndMass))) ConveyorPositions.Add(FVector3f(P.PositionAndMass));
            if(bDebugPoints && L.X>-75 && L.X<76 && FMath::Abs(L.Y)<30 && L.Z>0 && L.Z<40)
            {
                const float Diameter=0.65f*FMath::Pow(P.VelocityAndVolume.W,1.0f/3.0f);
                PointTransforms.Add(FTransform(FQuat::Identity,FVector(FVector3f(P.PositionAndMass))*100,FVector(Diameter)));
            }
            auto& History=TransportHistory[I];
            const double Mass=P.PositionAndMass.W;
            const FVector H=L-FVector(IntakeOffset)*100;
            const float EnvelopeCm=bWorkingLayout?28.f:23.f;
            const bool InRotor=FMath::Abs(H.X-51)<EnvelopeCm && FMath::Abs(H.Y)<24 && FMath::Abs(H.Z-1)<EnvelopeCm;
            const bool InTrough=L.X<25 && L.X> -55 && FMath::Abs(L.Y)<20.5 && L.Z>1.5 && L.Z<15;
            if(InRotor) { RotorMass+=Mass; if(!(History&4)) { History|=4; IntakeSeenMass+=Mass; } }
            if((History&4) && !(History&8) && P.PositionAndMass.Z>GetDefault<USandLevelSettings>()->SandDepthMeters+.03f) { History|=8; LiftedMass+=Mass; }
            if(InTrough) { TroughMass+=Mass; if(!(History&1)) { History|=1; TroughSeenMass+=Mass; } }
            if((History&1) && !(History&2) && PreviousLocalPositions[I].X>=-65 && L.X<-65 && FMath::Abs(L.Y)<24 && L.Z> -3 && L.Z<25)
            {
                History|=2; DeliveredMass+=Mass;
            }
            // Independently record upper-run release and later deposition.
            // A -65 cm plane alone misses material dropping immediately at -62.
            if((History&1) && !(History&32) && PreviousLocalPositions[I].X>=Rear*100 && L.X<Rear*100 && FMath::Abs(L.Y)<24 && L.Z> -3 && L.Z<25) {
                History|=32; TailCrossMass+=Mass;
            }
            if((History&32) && !(History&64) && L.X<Rear*100 && FMath::Abs(L.Y)<35 && L.Z< -20 && FVector3f(P.VelocityAndVolume).Length()<.15f) {
                History|=64; RearSettledMass+=Mass;
            }
            if((History&1) && !(History&18) && FMath::Abs(L.Y)>28 && L.X> -65) { History|=16; SideLossMass+=Mass; }
            PreviousLocalPositions[I]=FVector3f(L);
        }
        VisibleMaterialPoints->AddInstances(PointTransforms,false,true,false);
        ConveyorSurface->SetActorHiddenInGame(bDebugPoints || ConveyorPositions.IsEmpty());
        if(!bDebugPoints && !ConveyorPositions.IsEmpty())
        {
            // Weight by represented volume, not visible marker count. No extra
            // physics particles are created by this local surface reconstruction.
            ConveyorSurface->ParticleDensityWeight=Particles[0].VelocityAndVolume.W/FMath::Pow(.05f,3);
            FBox Bounds(ForceInit);
            const auto M=ToolMount->GetComponentTransform();
            for(float X:{-82.f,40.f}) for(float Y:{-32.f,32.f}) for(float Z:{-8.f,28.f})
                Bounds+=M.TransformPosition(FVector(X,Y,Z))/100;
            ConveyorSurface->GenerateSurfaceFromParticlePositions(MoveTemp(ConveyorPositions),FVector3f(Bounds.Min),FVector3f(Bounds.Max));
        }
    }
    if(!Particles.IsEmpty() && PhysicalTime>=NextReport)
    {
        NextReport=PhysicalTime+1;
        if(FParse::Param(FCommandLine::Get(),TEXT("SandMachineCapture")))
        {
            const FString Name=FString::Printf(TEXT("MachineFrames/frame_%03d.png"),FMath::RoundToInt(PhysicalTime));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name,true,false);
        }
        double Mass=0, RearMass=0,PenetratingMass=0; float MaxPenetration=0; int32 NonFinite=0,Carried=0,MaxPenCollider=-1;
        for(const auto& P:Particles)
        {
            Mass+=P.PositionAndMass.W;
            if(P.PositionAndMass.ContainsNaN() || P.VelocityAndVolume.ContainsNaN() || P.StressRow0AndCompaction.ContainsNaN() || P.StressRemainder.ContainsNaN()) ++NonFinite;
            if(P.BucketLocalAndCarried.W>.5f) ++Carried;
            const FVector Local=ToolMount->GetComponentTransform().InverseTransformPosition(FVector(FVector3f(P.PositionAndMass))*100);
            if(Local.X < -65) RearMass+=P.PositionAndMass.W;
            if(FMath::Abs(Local.X)<115 && FMath::Abs(Local.Y)<40 && FMath::Abs(Local.Z)<50) {
                float Pen=0;
                for(uint32 I=2;I<EndTool.ColliderCount;++I) {
                    const float D=PointPenetrationDepth(FVector3f(P.PositionAndMass),EndTool.Colliders[I]);
                    Pen=FMath::Max(Pen,D);
                    if(D>MaxPenetration) { MaxPenetration=D; MaxPenCollider=I; }
                }
                MaxPenetration=FMath::Max(MaxPenetration,Pen);
                if(Pen>.002f) PenetratingMass+=P.PositionAndMass.W;
            }
        }
        if(InitialMass<0) InitialMass=Mass;
        FVector3f HeadImpulse=FVector3f::ZeroVector;
        for(int32 I=7;I<7+HeadElementCount();++I) HeadImpulse+=R.ColliderLinear[I];
        const FVector Reaction=FVector(-HeadImpulse)/FMath::Max(Dt,1.e-6f);
        const FVector Velocity=ChassisBody->GetPhysicsLinearVelocity()/100.0;
        UE_LOG(LogTemp,Display,TEXT("HEAD_DIAGNOSTIC type=%s t=%.2f pitchDeg=%.2f rotorFxN=%.3f rotorFyN=%.3f rotorFzN=%.3f chassisSpeedMps=%.4f displacementM=%.4f horizontalDisplacementM=%.4f signedAdvanceM=%.4f chassisZm=%.4f mountCm=%.2f fixedBench=%d"),
            *HeadType,PhysicalTime,HeadPitch,Reaction.X,Reaction.Y,Reaction.Z,Velocity.Size(),
            FVector::Dist(ChassisBody->GetComponentLocation(),InitialChassisLocation)/100.0,
            FVector::Dist2D(ChassisBody->GetComponentLocation(),InitialChassisLocation)/100.0,
            (ChassisBody->GetComponentLocation().X-InitialChassisLocation.X)/100,ChassisBody->GetComponentLocation().Z/100,ToolMount->GetRelativeLocation().Z,bBench);
        UE_LOG(LogTemp,Display,TEXT("TOOL_CONTROL t=%.3f adaptive=%d loadRatio=%.3f relief=%d advanceM=%.4f desiredHeadZm=%.4f actualHeadZm=%.4f liftMps=%.4f"),PhysicalTime,bAdaptiveFeed,ControlLoadRatio,bDepthRelief,ControlAdvanceM,DesiredHeadZ,DrumCenter.Z,LiftSpeedMps);
        UE_LOG(LogTemp,Display,TEXT("FEED_CONTROL t=%.3f worldT=%.3f fraction=%.3f draftN=%.3f tractionBudgetN=%.3f targetMps=%.4f"),PhysicalTime,GetWorld()->GetTimeSeconds(),GetFeedFraction(),MeasuredDraftN,GetTractionBudgetN(),GetDriveTargetMps());
        UE_LOG(LogTemp,Display,TEXT("ROADHEADER t=%.2f rpm=%.2f chain=%.3f chainForceN=%.2f torque=%.2f mass=%.6f massError=%.9f rearMass=%.6f delivered=%.6f nonfinite=%d carried=%d penetratingKg=%.6f maxPenMm=%.3f maxPenCollider=%d"),
            PhysicalTime,GetDrumRPM(),ChainSpeed,ChainLoad,DrumLoad,Mass,Mass-InitialMass,RearMass,DeliveredMass,NonFinite,Carried,PenetratingMass,1000*MaxPenetration,MaxPenCollider);
        UE_LOG(LogTemp,Display,TEXT("MATERIAL_PATH t=%.2f rotorKg=%.6f intakeSeenKg=%.6f liftedKg=%.6f troughKg=%.6f troughSeenKg=%.6f outletKg=%.6f sideLossKg=%.6f tailCrossKg=%.6f rearSettledKg=%.6f rig=%d"),PhysicalTime,RotorMass,IntakeSeenMass,LiftedMass,TroughMass,TroughSeenMass,DeliveredMass,SideLossMass,TailCrossMass,RearSettledMass,bFeedRig);
        if(bAutoTest && PhysicalTime>=TestDuration)
        {
            FString Dump=TEXT("id,x,y,z,vx,vy,vz,mass,history\n");
            for(int32 I=0;I<Particles.Num();++I) {
                const auto& P=Particles[I];
                const FVector L=ToolMount->GetComponentTransform().InverseTransformPosition(FVector(FVector3f(P.PositionAndMass))*100)/100;
                const FVector V=ToolMount->GetComponentTransform().InverseTransformVectorNoScale(FVector(FVector3f(P.VelocityAndVolume)));
                if(FMath::Abs(L.X)<1.1 && FMath::Abs(L.Y)<.5 && FMath::Abs(L.Z)<.5)
                    Dump+=FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d\n"),I,L.X,L.Y,L.Z,V.X,V.Y,V.Z,P.PositionAndMass.W,TransportHistory[I]);
            }
            FFileHelper::SaveStringToFile(Dump,*(FPaths::ProjectSavedDir()/TEXT("TransportParticles.csv")));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(bStopTest?TEXT("RoadheaderStopped.png"):bInternalCamera?TEXT("RoadheaderInside.png"):TEXT("RoadheaderBench.png")),true,false);
            bAutoTest=false;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){ FPlatformMisc::RequestExit(false); return false; }),1.0f);
        }
    }
}

void ASandRoadheaderPawn::CompleteSoilStep(const Sand::MPM::FToolInteractionResult& R,float Dt,const TArray<Sand::MPM::FParticleData>& Particles)
{
    if(!bAutoTest) return;
    const FVector F=-FVector(R.ColliderLinear[2])/Dt;
    const float Speed=PhysicalTime>=1 && PhysicalTime<8 ? BladeSpeed:0;
    BladeWork+=F.X*Speed*Dt;
    if(PhysicalTime>=1 && PhysicalTime<8) { ForceIntegral+=FMath::Abs(F.X)*Dt; CuttingSeconds+=Dt; }
    PhysicalTime+=Dt;
    if(bRunning) MotorTime+=Dt;
    if(bApproach || bTransferApron || bRaisedDrum || bWorkingLayout) {
        const float OldHeight=ToolMount->GetRelativeLocation().Z;
        const float NewHeight=bApproach?FMath::Max(WorkingHeightCm,StartHeightCm-FMath::Max(0.f,MotorTime-1.f)*2.5f):FMath::Clamp(OldHeight+HeightInput*Dt*4.f,-6.f,StartHeightCm);
        LiftSpeedMps=(NewHeight-OldHeight)/(100*Dt);
        ToolMount->SetRelativeLocation(FVector(0,0,NewHeight));
    }
    // Diagnostic carriage moves the visible machine, never sand particles.
    if(bFeedRig) ChassisBody->SetWorldLocation(InitialChassisLocation+FVector(100*RigSpeed*FMath::Clamp(PhysicalTime-RigStart(),0.f,16.f),0,0));
    UpdateMachineVisuals();
    if(Particles.IsEmpty() || PhysicalTime<NextReport) return;
    NextReport=PhysicalTime+.5f;
    double Mass=0,KE=0,COMZ=0; int32 Nonfinite=0;
    float MinX=1.e9f,MaxX=-1.e9f;
    for(const auto& P:Particles) {
        const float M=P.PositionAndMass.W;
        Mass+=M; KE+=.5*M*FVector3f(P.VelocityAndVolume).SquaredLength(); COMZ+=M*P.PositionAndMass.Z;
        MinX=FMath::Min(MinX,P.PositionAndMass.X); MaxX=FMath::Max(MaxX,P.PositionAndMass.X);
        if(P.PositionAndMass.ContainsNaN() || P.VelocityAndVolume.ContainsNaN() || P.StressRow0AndCompaction.ContainsNaN() || P.StressRemainder.ContainsNaN()) ++Nonfinite;
    }
    if(InitialMass<0) InitialMass=Mass;
    UE_LOG(LogTemp,Display,TEXT("SOIL_BENCH case=%s t=%.3f angle=%.1f depth=%.3f speed=%.3f FxN=%.4f FzN=%.4f workJ=%.5f meanAbsFxN=%.4f massKg=%.6f massError=%.9f kineticJ=%.5f comZ=%.5f spanX=%.5f nonfinite=%d"),
        *SoilCase,PhysicalTime,BladeAngle,BladeDepth,Speed,F.X,F.Z,BladeWork,ForceIntegral/FMath::Max(CuttingSeconds,.01),Mass,Mass-InitialMass,KE,COMZ/Mass,MaxX-MinX,Nonfinite);
    if(PhysicalTime>=8 && bAutoTest) {
        bAutoTest=false;
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("SoilBench.png"),true,false);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){ FPlatformMisc::RequestExit(false); return false; }),1.f);
    }
}
