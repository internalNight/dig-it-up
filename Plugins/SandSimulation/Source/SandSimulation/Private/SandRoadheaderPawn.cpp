#include "SandRoadheaderPawn.h"
#include "SandMPMSolver.h"
#include "SandMachineKinematics.h"
#include "SandMachineDrive.h"
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
    for(int32 I=2; I<28; ++I)
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
    // Prototype carrier, 120 N maximum longitudinal drive force.
    // UE forces use kg*cm/s^2. This is a tuning assumption, not measured hardware.
    DriveForce=12000;
    SteeringTorque=120000;
}

void ASandRoadheaderPawn::BeginPlay()
{
    Super::BeginPlay();
    bBench=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench"));
    bAutoTest=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderTest"));
    bStopTest=FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderStopped"));
    bRunning=bAutoTest && !bStopTest;
    bDebugPoints=FParse::Param(FCommandLine::Get(),TEXT("SandDebugPoints"));
    bChainStopped=FParse::Param(FCommandLine::Get(),TEXT("SandChainStopped"));
    bCutTest=bAutoTest && FParse::Param(FCommandLine::Get(),TEXT("SandCutTest"));
    FParse::Value(FCommandLine::Get(),TEXT("SandHead="),HeadType);
    if(HeadType!=TEXT("Paddle") && HeadType!=TEXT("Chevron") && HeadType!=TEXT("Spoke") && HeadType!=TEXT("Helix"))
    {
        UE_LOG(LogTemp,Warning,TEXT("Unknown SandHead; using Paddle")); HeadType=TEXT("Paddle");
    }
    bPresetPitch=FParse::Value(FCommandLine::Get(),TEXT("SandHeadPitch="),HeadPitch);
    HeadPitch=FMath::Clamp(HeadPitch,-25.0f,18.0f);
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
    float HeightCm=ToolMount->GetRelativeLocation().Z;
    FParse::Value(FCommandLine::Get(),TEXT("SandHeadHeightCm="),HeightCm);
    ToolMount->SetRelativeLocation(FVector(0,0,FMath::Clamp(HeightCm,-10.0f,50.0f)));
    ToolMount->SetRelativeRotation(FRotator(HeadPitch,0,0));
    UE_LOG(LogTemp,Display,TEXT("HEAD_CONFIG type=%s pitchDeg=%.2f mountHeightCm=%.2f fixedBench=%d"),*HeadType,HeadPitch,HeightCm,bBench);
    if(bBench)
    {
        Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,0);
        for(int32 I=28;I<(int32)T.ColliderCount;++I)
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
    if(FParse::Param(FCommandLine::Get(),TEXT("SandHeadInspect")))
    {
        CameraBoom->bDoCollisionTest=false;
        CameraBoom->bEnableCameraLag=false;
        CameraBoom->TargetArmLength=210;
        CameraBoom->SetRelativeLocation(FVector(35,0,15));
        CameraBoom->SetRelativeRotation(FRotator(-22,135,0));
    }
    UpdateMachineVisuals();
}

void ASandRoadheaderPawn::Tick(float Dt)
{
    if(!bBench) Super::Tick(Dt);
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
        HeadPitch=FMath::Clamp(HeadPitch+Input*Dt*12,-25.0f,18.0f);
        if(bCutTest && !bPresetPitch)
            HeadPitch=-FMath::Min(15.0f,PhysicalTime*4.0f);
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
    const bool Axial=HeadType==TEXT("Spoke") || HeadType==TEXT("Helix");
    Box(FVector3f(.51f,0,.01f),Axial?FVector3f(.18f,.035f,.035f):FVector3f(.10f,.23f,.10f));
    Box(FVector3f(.51f,0,.01f),Axial?FVector3f(.16f,.04f,.04f):FVector3f(.07f,.255f,.07f));
    for(int32 I=0;I<DrumCount;++I)
    {
        auto& C=Tool.AddCollider(); C.Motion=1; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.Phase=DrumAngle+I*2*PI/DrumCount; C.Speed=-DrumOmega;
        C.HalfExtentsMeters=FVector3f(.055f,.23f,.022f);
        if(HeadType!=TEXT("Paddle"))
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
                const float A=I*2*PI/DrumCount;
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
                    C.RotorOffset=Around.RotateVector(FVector3f(-.14f+I*.04f,.12f,0));
                    C.RotorOrientation=Around*FQuat4f(FVector3f(0,1,0),-.45f);
                    C.HalfExtentsMeters=FVector3f(.025f,.085f,.055f);
                }
            }
        }
        C=SampleMachineCollider(C,Seconds);
    }
    for(int32 I=0;I<BladeCount;++I)
    {
        auto& C=Tool.AddCollider(); C.Motion=2; C.MotionOrigin=Origin; C.MotionRotation=Rotation;
        C.Phase=ChainDistance+I*Loop/BladeCount; C.Speed=ChainSpeed;
        C.HalfExtentsMeters=FVector3f(.018f,.205f,.028f);
        C=SampleMachineCollider(C,Seconds);
    }
    // Enclose the sides and top of the drum, leaving the front intake and
    // rear-to-tray route open. Guides return thrown material by solid contact.
    Box(FVector3f(.50f,-.26f,.07f),FVector3f(.22f,.02f,.18f));
    Box(FVector3f(.50f,.26f,.07f),FVector3f(.22f,.02f,.18f));
    Box(FVector3f(.48f,0,.265f),FVector3f(.24f,.24f,.015f));
    // Motion is physical contact only; never enable the excavator carrier.
    for(uint32 I=0;I<Tool.ColliderCount;++I)
    {
        auto& C=Tool.Colliders[I];
        const FVector3f V=bBench?FVector3f::ZeroVector:FVector3f(ChassisBody->GetPhysicsLinearVelocityAtPoint(FVector(C.CenterMeters)*100)/100.0);
        C.SeparationSpeedLimit=0.15f;
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
        // Explicit cutaway inspection: hide casing and feeder visuals only.
        // Their contact geometry remains active; never use this as an open-case test.
        const bool Cutaway=FParse::Param(FCommandLine::Get(),TEXT("SandHeadInspect"));
        V->SetVisibility(!(Cutaway && I>=25));
        V->SetWorldLocationAndRotation(FVector(C.CenterMeters)*100,FQuat(M));
        V->SetWorldScale3D(FVector(C.HalfExtentsMeters)*2);
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
    Sand::MPM::FToolColliderState T; BuildPhysicalTool(T,Dt*0.5f);
    const FVector3f DrumCenter(ToolMount->GetComponentTransform().TransformPosition(FVector(51,0,1))/100.0);
    const FVector3f Axis((HeadType==TEXT("Spoke") || HeadType==TEXT("Helix"))?ToolMount->GetForwardVector():ToolMount->GetRightVector());
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
    // Low-speed, high-torque geared drives. An anti-rollback clutch prevents
    // uncommanded reversal under overload; excess load can still stall the drum.
    DrumOmega=DrivenSpeed(DrumOmega,bRunning?Direction*1.8f:0,DrumImpulse,Dt,12,180,240);
    ChainSpeed=DrivenSpeed(ChainSpeed,bRunning && !bChainStopped?Direction*.30f:0,ChainImpulse,Dt,50,1000,500);
    if(bRunning) { DrumOmega=Direction*FMath::Max(0.0f,Direction*DrumOmega); }
    if(bRunning && !bChainStopped) { ChainSpeed=Direction*FMath::Max(0.0f,Direction*ChainSpeed); }
    if(!bRunning) DrumOmega=BrakeSpeed(DrumOmega,Dt,12,300);
    if(!bRunning || bChainStopped) ChainSpeed=BrakeSpeed(ChainSpeed,Dt,50,1000);
    DrumOmega=FMath::Clamp(DrumOmega,-2.2f,2.2f);
    ChainSpeed=FMath::Clamp(ChainSpeed,-.4f,.4f);
    PhysicalTime+=Dt;
    UpdateMachineVisuals();
    if(!Particles.IsEmpty())
    {
        // Surface reconstruction may hide sparse conveyed material. Show the
        // actual continuum sample positions near the machine as instanced dots.
        // They have no collision, mass or independent particle simulation.
        VisibleMaterialPoints->ClearInstances();
        TArray<FTransform> PointTransforms;
        TArray<FVector3f> ConveyorPositions;
        if(TransportHistory.Num()!=Particles.Num()) TransportHistory.Init(0,Particles.Num());
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
            if(L.X<25 && L.X>-55 && FMath::Abs(L.Y)<20.5 && L.Z>1.5 && L.Z<15) TransportHistory[I]|=1;
            if((TransportHistory[I]&1) && !(TransportHistory[I]&2) && L.X<-65)
            {
                TransportHistory[I]|=2;
                DeliveredMass+=P.PositionAndMass.W;
            }
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
        FVector3f HeadImpulse=FVector3f::ZeroVector;
        for(int32 I=7;I<7+DrumCount;++I) HeadImpulse+=R.ColliderLinear[I];
        const FVector Reaction=FVector(-HeadImpulse)/FMath::Max(Dt,1.e-6f);
        const FVector Velocity=ChassisBody->GetPhysicsLinearVelocity()/100.0;
        UE_LOG(LogTemp,Display,TEXT("HEAD_DIAGNOSTIC type=%s t=%.2f pitchDeg=%.2f rotorFxN=%.3f rotorFyN=%.3f rotorFzN=%.3f chassisSpeedMps=%.4f displacementM=%.4f fixedBench=%d"),
            *HeadType,PhysicalTime,HeadPitch,Reaction.X,Reaction.Y,Reaction.Z,Velocity.Size(),
            FVector::Dist(ChassisBody->GetComponentLocation(),InitialChassisLocation)/100.0,bBench);
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
