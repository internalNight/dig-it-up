#pragma once

#include "GameFramework/Pawn.h"
#include "SandExcavatorPawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** Compact Chaos-driven excavator used as the first interactive sand tool. */
UCLASS()
class SANDSIMULATION_API ASandExcavatorPawn : public APawn
{
    GENERATED_BODY()

public:
    ASandExcavatorPawn();
    virtual void Tick(float DeltaSeconds) override;

    /** Refresh the four track support samples from the current full 3D MPM state. */
    void UpdateSandSupportSurface(const TArray<FVector3f>& ParticlePositionsMeters, float SurfaceRadiusCm=2.625f);
    void UpdateVisibleSandSupport(const TArray<FVector>& Vertices, const TArray<int32>& Indices);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> ChassisBody;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<USceneComponent> BoomPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> LeftTrackCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> RightTrackCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<USceneComponent> StickPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<USceneComponent> BucketPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> BucketCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> BucketBackCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> BucketLeftCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> BucketRightCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Excavator")
    TObjectPtr<UBoxComponent> BucketLipCollider;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Excavator|Drive", meta = (ClampMin = "100.0"))
    float DriveForce = 4200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Excavator|Drive", meta = (ClampMin = "100.0"))
    float SteeringTorque = 18000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Excavator|Arm", meta = (ClampMin = "5.0", Units = "deg/s"))
    float JointSpeedDegreesPerSecond = 42.0f;

    float GetBoomAngleDegrees() const { return BoomAngleDegrees; }
    float GetStickAngleDegrees() const { return StickAngleDegrees; }
    float GetBucketAngleDegrees() const { return BucketAngleDegrees; }
    int32 GetGroundedSupportCount() const { return GroundedSupportCount; }
    void SetAppliedContactForce(const FVector& Force) { AppliedContactForce=Force; }
    float GetTractionBudgetN() const {return TractionBudgetN;}
    float GetDriveTargetMps() const {return DriveTargetMps;}

protected:
    virtual void BeginPlay() override;

protected:
    void UpdateVisualJoints();
    void ApplySandSuspension(float DeltaSeconds, bool bBrakeApplied);

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ChassisVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> UpperDeckVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CabVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WindshieldVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CounterweightVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> LeftTrackVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> RightTrackVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BoomVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> StickVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BucketVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BucketBackVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BucketLeftVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BucketRightVisual;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> BucketLipVisual;

    UPROPERTY()
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY()
    TObjectPtr<UCameraComponent> FollowCamera;

    float BoomAngleDegrees = -24.0f;
    float StickAngleDegrees = 58.0f;
    float BucketAngleDegrees = -35.0f;
    float ElapsedSimulationSeconds = 0.0f;
    TStaticArray<float, 4> SandSupportHeightsCentimeters = { 200.0f, 200.0f, 200.0f, 200.0f };
    TStaticArray<float, 4> ParticleSupportCeilingsCentimeters = { 201.0f, 201.0f, 201.0f, 201.0f };
    TStaticArray<bool, 4> bSandSupportSampleValid = { true, true, true, true };
    FVector SmoothedSandSurfaceNormal = FVector::UpVector;
    bool bHasSandSupportSamples = false;
    int32 GroundedSupportCount = 4;
    float CurrentTerrainGradientMagnitude = 0.0f;
    FVector AppliedContactForce=FVector::ZeroVector;
    float TractionBudgetN=0,DriveTargetMps=0;
    float BearingNormalN=0,FilteredBearingNormalN=0;
};
