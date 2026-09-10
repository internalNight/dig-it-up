#include "SandRoadheaderPawn.h"
#include "SandMPMSolver.h"
#include "SandMachineKinematics.h"
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
#include "Engine/GameViewportClient.h"
#include "Containers/Ticker.h"
#include "UObject/ConstructorHelpers.h"

using namespace Sand::Machine;

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
    for(int32 I=2; I<7+DrumCount+BladeCount; ++I)
    {
        auto* M=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("MachinePart%d"),I));
        M->SetupAttachment(ChassisBody);
        M->SetStaticMesh(Cube.Object);
        M->SetMaterial(0,(I>=7 && I<7+DrumCount) ? Orange.Object : Steel.Object);
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
    DriveForce=2100;
}

void ASandRoadheaderPawn::BeginPlay()
{
    Super::BeginPlay();
    bBench=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"));
    bAutoTest=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderTest"));
    bStopTest=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderStopped"));
    bRunning=bAutoTest && !bStopTest;
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
    if(bBench)
    {
        Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,0);
        for(int32 I=25;I<(int32)T.ColliderCount;++I)
        {
            auto* V=NewObject<UStaticMeshComponent>(this);
            V->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            V->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            V->RegisterComponent();
            V->AttachToComponent(ChassisBody,FAttachmentTransformRules::KeepWorldTransform);
            MachineVisuals.Add(V);
        }
    }
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
    UpdateMachineVisuals();
}

void ASandRoadheaderPawn::Tick(float Dt)
{
    if(!bBench) Super::Tick(Dt);
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC) return;
    if(PC->WasInputKeyJustPressed(EKeys::Escape)) PC->ConsoleCommand(TEXT("quit"));
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
        HeadPitch=FMath::Clamp(HeadPitch+Input*Dt*12,-25.0f,18.0f);
        ToolMount->SetRelativeRotation(FRotator(HeadPitch,0,0));
    }
    UpdateMachineVisuals();
}

void ASandRoadheaderPawn::BuildPhysicalTool(Sand::MPM::FToolColliderState& Tool,float Seconds)
{
    using namespace Sand::MPM;
    Tool=FToolColliderState{};
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
    Box(FVector3f(-.15f,0,0),FVector3f(.47f,.22f,.015f));
    Box(FVector3f(-.15f,-.235f,.065f),FVector3f(.47f,.015f,.08f));
    Box(FVector3f(-.15f,.235f,.065f),FVector3f(.47f,.015f,.08f));
    Box(FVector3f(.51f,0,.01f),FVector3f(.10f,.23f,.10f));
    Box(FVector3f(.51f,0,.01f),FVector3f(.07f,.255f,.07f));
    for(int32 I=0;I<DrumCount;++I)
    {
        auto& C=Tool.AddCollider(); C.Motion=1; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.Phase=DrumAngle+I*2*PI/DrumCount; C.Speed=-DrumOmega;
        C.HalfExtentsMeters=FVector3f(.055f,.23f,.022f);
        C=SampleMachineCollider(C,Seconds);
    }
    for(int32 I=0;I<BladeCount;++I)
    {
        auto& C=Tool.AddCollider(); C.Motion=2; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.Phase=ChainDistance+I*Loop/BladeCount; C.Speed=ChainSpeed;
        C.HalfExtentsMeters=FVector3f(.018f,.205f,.028f);
        C=SampleMachineCollider(C,Seconds);
    }
    // Motion is physical contact only; never enable the excavator carrier.
    for(uint32 I=0;I<Tool.ColliderCount;++I)
    {
        auto& C=Tool.Colliders[I];
        const FVector3f V=bBench?FVector3f::ZeroVector:FVector3f(ChassisBody->GetPhysicsLinearVelocityAtPoint(FVector(C.CenterMeters)*100)/100.0);
        C.BaseVelocity=V;
        C.LinearVelocityMetersPerSecond+=V;
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
    for(int32 I=2;I<(int32)T.ColliderCount && I-2<MachineVisuals.Num();++I)
    {
        const auto& C=T.Colliders[I];
        FMatrix M=FMatrix::Identity;
        const FVector X(C.AxisX),Y(C.AxisY),Z(C.AxisZ);
        M.SetAxes(&X,&Y,&Z);
        auto* V=MachineVisuals[I-2].Get();
        V->SetWorldLocationAndRotation(FVector(C.CenterMeters)*100,FQuat(M));
        V->SetWorldScale3D(FVector(C.HalfExtentsMeters)*2);
    }
}

void ASandRoadheaderPawn::CompletePhysicalStep(const Sand::MPM::FToolInteractionResult& R,
    float Dt,const TArray<Sand::MPM::FParticleData>& Particles)
{
    Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,Dt*0.5f);
    const FVector3f DrumCenter(ToolMount->GetComponentTransform().TransformPosition(FVector(51,0,1))/100.0);
    const FVector3f Axis(ToolMount->GetRightVector());
    float DrumImpulse=0, ChainImpulse=0;
    for(int32 I=7;I<7+DrumCount;++I)
    {
        const FVector3f Torque=R.ColliderAngular[I]+FVector3f::CrossProduct(T.Colliders[I].CenterMeters-DrumCenter,R.ColliderLinear[I]);
        DrumImpulse-=FVector3f::DotProduct(Torque,Axis);
    }
    for(int32 I=7+DrumCount;I<7+DrumCount+BladeCount;++I)
        ChainImpulse+=FVector3f::DotProduct(R.ColliderLinear[I],T.Colliders[I].AxisX);
    DrumLoad=DrumImpulse/FMath::Max(Dt,1.e-6f);
    ChainLoad=ChainImpulse/FMath::Max(Dt,1.e-6f);
    // Advance angles using exactly the speed submitted to the completed GPU step.
    DrumAngle=FMath::Fmod(DrumAngle-DrumOmega*Dt,2*PI);
    ChainDistance=FMath::Fmod(ChainDistance+ChainSpeed*Dt,Loop);
    // Bounded motor torque/force, with inertial load response and a holding brake.
    const float TargetOmega=bRunning?Direction*5.0f:0;
    const float TargetChain=bRunning?Direction*.45f:0;
    const float MotorTorque=FMath::Clamp((TargetOmega-DrumOmega)*6.0f,-24.0f,24.0f);
    const float MotorForce=FMath::Clamp((TargetChain-ChainSpeed)*240.0f,-140.0f,140.0f);
    DrumOmega=FMath::Clamp(DrumOmega+(MotorTorque*Dt-DrumImpulse)/2.0f,-6.0f,6.0f);
    ChainSpeed=FMath::Clamp(ChainSpeed+(MotorForce*Dt-ChainImpulse)/25.0f,-.6f,.6f);
    if(!bRunning)
    {
        // Finite Coulomb holding brakes: dissipate momentum, never drive sand.
        DrumOmega=FMath::Sign(DrumOmega)*FMath::Max(0.0f,FMath::Abs(DrumOmega)-150.0f*Dt/2.0f);
        ChainSpeed=FMath::Sign(ChainSpeed)*FMath::Max(0.0f,FMath::Abs(ChainSpeed)-500.0f*Dt/25.0f);
    }
    PhysicalTime+=Dt;
    UpdateMachineVisuals();
    if(!Particles.IsEmpty())
    {
        // Surface reconstruction may hide sparse conveyed material. Show the
        // actual continuum sample positions near the machine as instanced dots.
        // They have no collision, mass or independent particle simulation.
        VisibleMaterialPoints->ClearInstances();
        TArray<FTransform> PointTransforms;
        if(TransportHistory.Num()!=Particles.Num()) TransportHistory.Init(0,Particles.Num());
        for(int32 I=0;I<Particles.Num();++I)
        {
            const auto& P=Particles[I];
            const FVector L=ToolMount->GetComponentTransform().InverseTransformPosition(FVector(FVector3f(P.PositionAndMass))*100);
            if(L.X>-75 && L.X<76 && FMath::Abs(L.Y)<30 && L.Z>0 && L.Z<40)
            {
                const float Diameter=0.65f*FMath::Pow(P.VelocityAndVolume.W,1.0f/3.0f);
                PointTransforms.Add(FTransform(FQuat::Identity,FVector(FVector3f(P.PositionAndMass))*100,FVector(Diameter)));
            }
            if(L.X<25 && L.X>-55 && FMath::Abs(L.Y)<20.5 && L.Z>1.5 && L.Z<15) TransportHistory[I]|=1;
            if((TransportHistory[I]&1) && !(TransportHistory[I]&2) && L.X<-65)
            {
                TransportHistory[I]|=2;
                DeliveredMass+=P.PositionAndMass.W;
            }
        }
        VisibleMaterialPoints->AddInstances(PointTransforms,false,true,false);
    }
    if(!Particles.IsEmpty() && PhysicalTime>=NextReport)
    {
        NextReport=PhysicalTime+1;
        if(FParse::Param(FCommandLine::Get(),TEXT("SandMachineCapture")))
        {
            const FString Name=FString::Printf(TEXT("MachineFrames/frame_%03d.png"),FMath::RoundToInt(PhysicalTime));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name,true,false);
        }
        double Mass=0, RearMass=0; int32 NonFinite=0,Carried=0;
        for(const auto& P:Particles)
        {
            Mass+=P.PositionAndMass.W;
            if(P.PositionAndMass.ContainsNaN() || P.VelocityAndVolume.ContainsNaN()) ++NonFinite;
            if(P.BucketLocalAndCarried.W>.5f) ++Carried;
            const FVector Local=ToolMount->GetComponentTransform().InverseTransformPosition(FVector(FVector3f(P.PositionAndMass))*100);
            if(Local.X < -65) RearMass+=P.PositionAndMass.W;
        }
        if(InitialMass<0) InitialMass=Mass;
        UE_LOG(LogTemp,Display,TEXT("ROADHEADER t=%.2f rpm=%.2f chain=%.3f torque=%.2f mass=%.6f massError=%.9f rearMass=%.6f delivered=%.6f nonfinite=%d carried=%d"),
            PhysicalTime,GetDrumRPM(),ChainSpeed,DrumLoad,Mass,Mass-InitialMass,RearMass,DeliveredMass,NonFinite,Carried);
        if(bAutoTest && PhysicalTime>=16)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/(bStopTest?TEXT("RoadheaderStopped.png"):bInternalCamera?TEXT("RoadheaderInside.png"):TEXT("RoadheaderBench.png")),true,false);
            bAutoTest=false;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){ FPlatformMisc::RequestExit(false); return false; }),1.0f);
        }
    }
}
