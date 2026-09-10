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
    bool IsConveyorRegion(const FVector3f& Position) const;
    bool IsDebugPoints() const { return bDebugPoints; }
    bool IsCutAcceptance() const { return bCutTest; }
    float GetAcceptanceThrottle() const { return PhysicalTime>2 && PhysicalTime<7 ? 0.25f : 0.0f; }
    bool IsRunning() const { return bRunning; }
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
    float DrumOmega = 0;
    float ChainSpeed = 0;
    float DrumLoad = 0;
    float ChainLoad = 0;
    float HeadPitch = 0;
    float Direction = 1;
    float PhysicalTime = 0;
    float NextReport = 0;
    double InitialMass = -1;
    TArray<uint8> TransportHistory;
    double DeliveredMass=0;
    bool bDebugPoints = false;
    bool bChainStopped = false;
    bool bCutTest = false;
    bool bRunning = false;
    bool bInternalCamera = false;
    bool bBench = false;
    bool bAutoTest = false;
    bool bStopTest = false;

};
