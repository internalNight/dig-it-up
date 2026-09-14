#pragma once
#include "SandExcavatorPawn.h"
#include "SandRoadheaderPawn.generated.h"

class UInstancedStaticMeshComponent;
class ASandSurfacePreviewActor;

namespace Sand::MPM { struct FToolColliderState; struct FToolInteractionResult; struct FParticleData; }

/** Open-deck continuous miner. Only moving solid contacts transport the sand. */
UCLASS()
class SANDSIMULATION_API ASandRoadheaderPawn : public ASandExcavatorPawn
{
    GENERATED_BODY()
public:
    ASandRoadheaderPawn();
    virtual void Tick(float DeltaSeconds) override;
    void BuildPhysicalTool(Sand::MPM::FToolColliderState& Tool, float Seconds);
    void CompletePhysicalStep(const Sand::MPM::FToolInteractionResult& Result, float Seconds,
        const TArray<Sand::MPM::FParticleData>& Particles);
    float GetDrumRPM() const { return DrumOmega * 60.0f / (2.0f*PI); }
    float GetConveyorSpeed() const { return ChainSpeed; }
    float GetLoadTorque() const { return DrumLoad; }
    const FString& GetHeadType() const { return HeadType; }
    float GetFeedFraction() const;
    bool IsConveyorRegion(const FVector3f& Position) const;
    bool IsDebugPoints() const { return bDebugPoints; }
    bool IsCutAcceptance() const { return bCutTest; }
    float GetAcceptanceThrottle() const { return bRunning && !bHoldTest && PhysicalTime>RigStart() && PhysicalTime<(bApproach?FeedEndSeconds:7.f) ? (bWorkingLayout?1.f:.25f) : 0.0f; }
    bool IsRunning() const { return bRunning; }
    double GetDeliveredMass() const { return DeliveredMass; }
    double GetTailCrossMass() const { return TailCrossMass; }
    const TCHAR* GetDepthStatus() const { return !bAdaptiveFeed?TEXT("OFF"):!bApproach?TEXT("MANUAL"):bDepthRelief?TEXT("LIFTING"):TEXT("AUTO"); }
    double GetRearSettledMass() const { return RearSettledMass; }
    float GetTravelSpeedMps() const { return bWorkingLayout?.03f:.12f; }
    double GetTroughMass() const { return TroughMass; }
protected:
    virtual void BeginPlay() override;
private:
    void UpdateMachineVisuals();
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> VisibleMaterialPoints;
    UPROPERTY() TObjectPtr<ASandSurfacePreviewActor> ConveyorSurface;
    UPROPERTY() TObjectPtr<USceneComponent> ToolMount;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MachineVisuals;
    float DrumAngle = 0;
    float ChainDistance = 0;
    float RollerAngle = 0;
    float DrumOmega = 0;
    float ChainSpeed = 0;
    float DrumLoad = 0;
    float ChainLoad = 0;
    float HeadPitch = 0;
    // Geometric screening prototypes, not calibrated replicas of vendor machines.
    FString HeadType = TEXT("Paddle");
    FString SoilCase;
    float BladeAngle=60, BladeDepth=.10f, BladeSpeed=.05f;
    float MeasuredDraftN=0;
    double BladeWork=0, ForceIntegral=0, CuttingSeconds=0;
    void CompleteSoilStep(const Sand::MPM::FToolInteractionResult& R,float Dt,const TArray<Sand::MPM::FParticleData>& Particles);
    int32 HeadElementCount() const { return HeadType==TEXT("Helix")?24:HeadType==TEXT("BucketWheel")?(bWorkingLayout?24:16):8; }
    bool bPresetPitch = false;
    bool bFeedRig = false;
    FVector3f IntakeOffset=FVector3f::ZeroVector;
    bool bTransferApron=false, bApproach=false, bRaisedDrum=false, bWorkingLayout=false;
    bool bAdaptiveFeed=false, bDepthRelief=false;
    float ControlLoadRatio=0, ControlAdvanceM=0, ReferenceHeadZ=0, DesiredHeadZ=0;
    FVector LastControlLocation=FVector::ZeroVector;
    float ConveyorFront=.32f, TargetPitch=0, MotorTime=0, WorkingHeightCm=4;
    float HeightInput=0, LiftSpeedMps=0, DrumTargetOmega=1.8f;
    float StartHeightCm=24;
    float MinHeightCm=-6;
    float DepthRampM=.48f;
    float ApproachSpeedCmPerS=1.5f;
    float HelixFriction=.25f;
    float ReliefOnLoad=.85f, ReliefOffLoad=.55f;
    float ConveyorLoop() const;
    float RigStart() const { return bApproach?9.f:2.f; }
    float RigSpeed = .03f;
    float TestDuration = 16.f, StopAtSeconds=-1, FeedEndSeconds=25;
    FVector InitialChassisLocation = FVector::ZeroVector;
    float Direction = 1;
    float HeadDirection = 1;
    float PhysicalTime = 0;
    float NextReport = 0;
    double InitialMass = -1;
    TArray<uint8> TransportHistory;
    TArray<FVector3f> PreviousLocalPositions;
    double IntakeSeenMass=0, LiftedMass=0, TroughSeenMass=0;
    double RotorMass=0, TroughMass=0, SideLossMass=0;
    double DeliveredMass=0, TailCrossMass=0, RearSettledMass=0;
    bool bDebugPoints = false;
    bool bChainStopped = false;
    bool bCutTest = false;
    bool bHoldTest = false;
    bool bRunning = false;
    bool bInternalCamera = false;
    bool bBench = false;
    bool bAutoTest = false;
    bool bStopTest = false;

};
