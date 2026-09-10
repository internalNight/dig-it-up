#include "SandExcavatorPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/AutomationTest.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// Intersect a vertical line with the actual volumetric mesh, including slopes
// and multiple layers. The non-carried-particle ceiling rejects lifted loads.
bool SampleVisibleSandHeight(const TArray<FVector>& Vertices, const TArray<int32>& Indices,
    const FVector2D& XY, const float Ceiling, float& OutHeight)
{
    bool bFound = false;
    OutHeight = 0.0f;
    for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
    {
        const FVector& A = Vertices[Indices[Index]];
        const FVector& B = Vertices[Indices[Index + 1]];
        const FVector& C = Vertices[Indices[Index + 2]];
        if (XY.X < FMath::Min3(A.X, B.X, C.X) || XY.X > FMath::Max3(A.X, B.X, C.X) ||
            XY.Y < FMath::Min3(A.Y, B.Y, C.Y) || XY.Y > FMath::Max3(A.Y, B.Y, C.Y))
        {
            continue;
        }
        const double Denominator = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
        if (FMath::Abs(Denominator) < 1.e-8) { continue; }
        const double U = ((B.Y - C.Y) * (XY.X - C.X) + (C.X - B.X) * (XY.Y - C.Y)) / Denominator;
        const double V = ((C.Y - A.Y) * (XY.X - C.X) + (A.X - C.X) * (XY.Y - C.Y)) / Denominator;
        if (U < -1.e-6 || V < -1.e-6 || U + V > 1.000001) { continue; }
        const float Height = U * A.Z + V * B.Z + (1.0 - U - V) * C.Z;
        if (Height >= 0.0f && Height <= Ceiling && (!bFound || Height > OutHeight))
        {
            OutHeight = Height;
            bFound = true;
        }
    }
    return bFound;
}

void ConfigureVisual(
    UStaticMeshComponent* Visual,
    UStaticMesh* CubeMesh,
    const FVector& Scale,
    const FVector& Location)
{
    Visual->SetStaticMesh(CubeMesh);
    Visual->SetRelativeScale3D(Scale);
    Visual->SetRelativeLocation(Location);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetCastShadow(true);
}

void ApplyVisualMaterial(
    UStaticMeshComponent* Visual,
    const FLinearColor& Color,
    const float Roughness,
    const float Metallic = 0.0f)
{
    if (Visual == nullptr)
    {
        return;
    }
    if (UMaterialInstanceDynamic* Material = Visual->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Material->SetVectorParameterValue(TEXT("DiffuseColor"), Color);
        Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
        Material->SetScalarParameterValue(TEXT("Metallic"), Metallic);
    }
}
}

ASandExcavatorPawn::ASandExcavatorPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    ChassisBody = CreateDefaultSubobject<UBoxComponent>(TEXT("ChassisBody"));
    SetRootComponent(ChassisBody);
    ChassisBody->SetBoxExtent(FVector(20.0, 13.0, 7.5));
    ChassisBody->SetCollisionProfileName(TEXT("PhysicsActor"));
    ChassisBody->SetSimulatePhysics(true);
    ChassisBody->SetEnableGravity(true);
    ChassisBody->SetLinearDamping(1.4f);
    ChassisBody->SetAngularDamping(4.0f);
    ChassisBody->BodyInstance.bUseCCD = true;
    ChassisBody->BodyInstance.bLockXRotation = false;
    ChassisBody->BodyInstance.bLockYRotation = false;
    ChassisBody->BodyInstance.bLockZTranslation = false;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterialFinder(
        TEXT("/Engine/TemplateResources/MI_Template_BaseOrange.MI_Template_BaseOrange"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrackMaterialFinder(
        TEXT("/Engine/TemplateResources/MI_Template_BaseGray_03.MI_Template_BaseGray_03"));
    UStaticMesh* CubeMesh = CubeFinder.Object;

    ChassisVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChassisVisual"));
    ChassisVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(ChassisVisual, CubeMesh, FVector(0.36, 0.22, 0.13), FVector(0.0, 0.0, 2.0));

    UpperDeckVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpperDeckVisual"));
    UpperDeckVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(UpperDeckVisual, CubeMesh, FVector(0.30, 0.20, 0.045), FVector(-2.0, 0.0, 11.0));

    CabVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabVisual"));
    CabVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(CabVisual, CubeMesh, FVector(0.18, 0.15, 0.18), FVector(-7.0, -3.5, 21.0));

    WindshieldVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WindshieldVisual"));
    WindshieldVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(WindshieldVisual, CubeMesh, FVector(0.012, 0.125, 0.125), FVector(2.2, -3.5, 22.0));

    CounterweightVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CounterweightVisual"));
    CounterweightVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(CounterweightVisual, CubeMesh, FVector(0.10, 0.21, 0.10), FVector(-17.0, 0.0, 8.0));

    LeftTrackVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftTrackVisual"));
    LeftTrackVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(LeftTrackVisual, CubeMesh, FVector(0.42, 0.055, 0.085), FVector(0.0, -14.0, -3.5));

    RightTrackVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightTrackVisual"));
    RightTrackVisual->SetupAttachment(ChassisBody);
    ConfigureVisual(RightTrackVisual, CubeMesh, FVector(0.42, 0.055, 0.085), FVector(0.0, 14.0, -3.5));

    LeftTrackCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("LeftTrackCollider"));
    LeftTrackCollider->SetupAttachment(ChassisBody);
    LeftTrackCollider->SetBoxExtent(FVector(21.0, 3.0, 4.25));
    LeftTrackCollider->SetRelativeLocation(FVector(0.0, -14.0, -3.5));
    LeftTrackCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    RightTrackCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("RightTrackCollider"));
    RightTrackCollider->SetupAttachment(ChassisBody);
    RightTrackCollider->SetBoxExtent(FVector(21.0, 3.0, 4.25));
    RightTrackCollider->SetRelativeLocation(FVector(0.0, 14.0, -3.5));
    RightTrackCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BoomPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BoomPivot"));
    BoomPivot->SetupAttachment(ChassisBody);
    BoomPivot->SetRelativeLocation(FVector(13.0, 0.0, 10.0));

    BoomVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoomVisual"));
    BoomVisual->SetupAttachment(BoomPivot);
    ConfigureVisual(BoomVisual, CubeMesh, FVector(0.28, 0.055, 0.055), FVector(14.0, 0.0, 0.0));

    StickPivot = CreateDefaultSubobject<USceneComponent>(TEXT("StickPivot"));
    StickPivot->SetupAttachment(BoomPivot);
    StickPivot->SetRelativeLocation(FVector(28.0, 0.0, 0.0));

    StickVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StickVisual"));
    StickVisual->SetupAttachment(StickPivot);
    ConfigureVisual(StickVisual, CubeMesh, FVector(0.22, 0.050, 0.050), FVector(11.0, 0.0, 0.0));

    BucketPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BucketPivot"));
    BucketPivot->SetupAttachment(StickPivot);
    BucketPivot->SetRelativeLocation(FVector(22.0, 0.0, 0.0));

    BucketCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("BucketCollider"));
    BucketCollider->SetupAttachment(BucketPivot);
    BucketCollider->SetBoxExtent(FVector(15.0, 18.0, 1.0));
    BucketCollider->SetRelativeLocation(FVector(13.0, 0.0, -10.0));
    BucketCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BucketBackCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("BucketBackCollider"));
    BucketBackCollider->SetupAttachment(BucketPivot);
    BucketBackCollider->SetBoxExtent(FVector(1.0, 18.0, 10.0));
    BucketBackCollider->SetRelativeLocation(FVector(-2.0, 0.0, 0.0));
    BucketBackCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BucketLeftCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("BucketLeftCollider"));
    BucketLeftCollider->SetupAttachment(BucketPivot);
    BucketLeftCollider->SetBoxExtent(FVector(15.0, 1.0, 10.0));
    BucketLeftCollider->SetRelativeLocation(FVector(13.0, -17.0, 0.0));
    BucketLeftCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BucketRightCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("BucketRightCollider"));
    BucketRightCollider->SetupAttachment(BucketPivot);
    BucketRightCollider->SetBoxExtent(FVector(15.0, 1.0, 10.0));
    BucketRightCollider->SetRelativeLocation(FVector(13.0, 17.0, 0.0));
    BucketRightCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BucketLipCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("BucketLipCollider"));
    BucketLipCollider->SetupAttachment(BucketPivot);
    BucketLipCollider->SetBoxExtent(FVector(1.0, 18.0, 3.0));
    BucketLipCollider->SetRelativeLocation(FVector(28.0, 0.0, -7.0));
    BucketLipCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BucketVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BucketVisual"));
    BucketVisual->SetupAttachment(BucketPivot);
    ConfigureVisual(BucketVisual, CubeMesh, FVector(0.30, 0.36, 0.02), FVector(13.0, 0.0, -10.0));

    BucketBackVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BucketBackVisual"));
    BucketBackVisual->SetupAttachment(BucketPivot);
    ConfigureVisual(BucketBackVisual, CubeMesh, FVector(0.02, 0.36, 0.20), FVector(-2.0, 0.0, 0.0));

    BucketLeftVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BucketLeftVisual"));
    BucketLeftVisual->SetupAttachment(BucketPivot);
    ConfigureVisual(BucketLeftVisual, CubeMesh, FVector(0.30, 0.02, 0.20), FVector(13.0, -17.0, 0.0));

    BucketRightVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BucketRightVisual"));
    BucketRightVisual->SetupAttachment(BucketPivot);
    ConfigureVisual(BucketRightVisual, CubeMesh, FVector(0.30, 0.02, 0.20), FVector(13.0, 17.0, 0.0));

    BucketLipVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BucketLipVisual"));
    BucketLipVisual->SetupAttachment(BucketPivot);
    ConfigureVisual(BucketLipVisual, CubeMesh, FVector(0.02, 0.36, 0.06), FVector(28.0, 0.0, -7.0));

    for (UStaticMeshComponent* BodyPart : {
        ChassisVisual, UpperDeckVisual, CabVisual, CounterweightVisual,
        BoomVisual, StickVisual, BucketVisual,
        BucketBackVisual, BucketLeftVisual, BucketRightVisual, BucketLipVisual })
    {
        BodyPart->SetMaterial(0, BodyMaterialFinder.Object);
    }
    LeftTrackVisual->SetMaterial(0, TrackMaterialFinder.Object);
    RightTrackVisual->SetMaterial(0, TrackMaterialFinder.Object);
    WindshieldVisual->SetMaterial(0, TrackMaterialFinder.Object);

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(ChassisBody);
    CameraBoom->SetRelativeLocation(FVector(0.0, 0.0, 28.0));
    CameraBoom->SetRelativeRotation(FRotator(-42.0, -58.0, 0.0));
    CameraBoom->TargetArmLength = 185.0f;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 7.0f;
    CameraBoom->bDoCollisionTest = true;
    CameraBoom->ProbeSize = 8.0f;
    CameraBoom->bInheritPitch = false;
    CameraBoom->bInheritRoll = false;
    CameraBoom->bInheritYaw = true;

    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->FieldOfView = 58.0f;

    AutoPossessPlayer = EAutoReceiveInput::Disabled;
    UpdateVisualJoints();
}

void ASandExcavatorPawn::BeginPlay()
{
    Super::BeginPlay();
    for (int32 Index = 0; Index < 4; ++Index)
    {
        SandSupportHeightsCentimeters[Index] = GetActorLocation().Z - 8.0f;
        ParticleSupportCeilingsCentimeters[Index] = GetActorLocation().Z - 7.0f;
    }
    ChassisBody->SetMassOverrideInKg(NAME_None, 12.0f, true);
    const FLinearColor ConstructionYellow = FLinearColor::FromSRGBColor(FColor(224, 143, 22));
    const FLinearColor ArmYellow = FLinearColor::FromSRGBColor(FColor(243, 166, 33));
    const FLinearColor BucketSteel = FLinearColor::FromSRGBColor(FColor(92, 72, 49));
    const FLinearColor TrackRubber = FLinearColor::FromSRGBColor(FColor(25, 31, 36));
    const FLinearColor CabGlass = FLinearColor::FromSRGBColor(FColor(31, 55, 66));
    for (UStaticMeshComponent* BodyPart : {
        ChassisVisual, UpperDeckVisual, CabVisual, CounterweightVisual })
    {
        ApplyVisualMaterial(BodyPart, ConstructionYellow, 0.66f);
    }
    ApplyVisualMaterial(BoomVisual, ArmYellow, 0.60f);
    ApplyVisualMaterial(StickVisual, ArmYellow, 0.60f);
    for (UStaticMeshComponent* BucketPart : {
        BucketVisual, BucketBackVisual, BucketLeftVisual, BucketRightVisual, BucketLipVisual })
    {
        ApplyVisualMaterial(BucketPart, BucketSteel, 0.78f, 0.12f);
    }
    ApplyVisualMaterial(LeftTrackVisual, TrackRubber, 0.92f);
    ApplyVisualMaterial(RightTrackVisual, TrackRubber, 0.92f);
    ApplyVisualMaterial(WindshieldVisual, CabGlass, 0.30f, 0.10f);
    UpdateVisualJoints();
}

void ASandExcavatorPawn::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ElapsedSimulationSeconds += DeltaSeconds;
    APlayerController* PlayerController = Cast<APlayerController>(GetController());
    const bool bBoundaryTest = FParse::Param(FCommandLine::Get(), TEXT("SandBoundaryTest"));
    const bool bBoomRaiseTest = FParse::Param(FCommandLine::Get(), TEXT("SandBoomRaiseTest"));
    const bool bAutopilotDemo = bBoundaryTest || bBoomRaiseTest ||
        FParse::Param(FCommandLine::Get(), TEXT("SandAutopilot")) ||
        FParse::Param(FCommandLine::Get(), TEXT("SandVictoryTest"));
    const bool bSlopeCoastTest = FParse::Param(FCommandLine::Get(), TEXT("SandSlopeCoastTest"));
    if (PlayerController == nullptr && !bAutopilotDemo)
    {
        return;
    }

    float Throttle = 0.0f;
    float Steering = 0.0f;
    float BoomInput = 0.0f;
    float StickInput = 0.0f;
    float BucketInput = 0.0f;
    bool bBrake = false;
    if (bBoundaryTest)
    {
        // Headless acceptance mode drives into the nearest retaining wall.
        Throttle = -1.0f;
    }
    else if (bBoomRaiseTest)
    {
        bBrake = true;
        BoomInput = ElapsedSimulationSeconds >= 1.0f ? 1.0f : 0.0f;
    }
    else if (bAutopilotDemo)
    {
        // Deterministic scoop, carry and dump cycle used by headless validation/capture.
        if (ElapsedSimulationSeconds >= 1.0f && ElapsedSimulationSeconds < 2.0f)
        {
            BoomInput = -0.85f;
            StickInput = -0.20f;
        }
        else if (ElapsedSimulationSeconds >= 2.0f && ElapsedSimulationSeconds < 3.8f)
        {
            Throttle = FParse::Param(FCommandLine::Get(), TEXT("SandVictoryTest")) ? 0.18f : 0.45f;
            StickInput = 0.55f;
            BucketInput = 0.75f;
        }
        else if (ElapsedSimulationSeconds >= 3.8f && ElapsedSimulationSeconds < 5.0f)
        {
            BoomInput = 0.65f;
            bBrake = true;
        }
        else if (ElapsedSimulationSeconds >= 5.0f && ElapsedSimulationSeconds < 6.0f)
        {
            bBrake = true;
        }
        else if (ElapsedSimulationSeconds >= 6.0f && ElapsedSimulationSeconds < 10.5f)
        {
            // Uncurl until the opening faces downward. Carried MPM material then
            // returns to the ordinary granular solve and forms a deposited pile.
            BucketInput = -1.0f;
            bBrake = true;
        }
        else if (ElapsedSimulationSeconds >= 10.5f)
        {
            // Test-only mode releases the holding brake after the dig/dump
            // cycle so automated validation can observe downhill coasting.
            bBrake = !bSlopeCoastTest;
        }
    }
    else
    {
        if (PlayerController->WasInputKeyJustPressed(EKeys::Escape))
        {
            PlayerController->ConsoleCommand(TEXT("quit"));
            return;
        }
        Throttle =
            (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
        Steering =
            (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
        BoomInput =
            (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f);
        StickInput =
            (PlayerController->IsInputKeyDown(EKeys::R) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::F) ? 1.0f : 0.0f);
        BucketInput =
            (PlayerController->IsInputKeyDown(EKeys::T) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::G) ? 1.0f : 0.0f);
        bBrake = PlayerController->IsInputKeyDown(EKeys::SpaceBar);
    }

    ApplySandSuspension(DeltaSeconds, bBrake);

    FVector HorizontalForward = GetActorForwardVector();
    HorizontalForward.Z = 0.0;
    HorizontalForward.Normalize();
    ChassisBody->AddForce(HorizontalForward * Throttle * DriveForce, NAME_None, false);
    ChassisBody->AddTorqueInRadians(FVector::UpVector * Steering * SteeringTorque, NAME_None, false);

    if (bBrake)
    {
        ChassisBody->SetPhysicsLinearVelocity(ChassisBody->GetPhysicsLinearVelocity() * 0.82f);
        ChassisBody->SetPhysicsAngularVelocityInRadians(ChassisBody->GetPhysicsAngularVelocityInRadians() * 0.72f);
    }

    // Approximate the strong anisotropic traction of two tracks. The simple
    // first-playable chassis has no per-link track contact, so without this
    // term bucket reaction impulses can unrealistically slide the 12 kg model
    // metres across the sand while the controls are released.
    const FVector Forward = HorizontalForward;
    FVector LinearVelocity = ChassisBody->GetPhysicsLinearVelocity();
    const float VerticalSpeed = LinearVelocity.Z;
    FVector HorizontalVelocity(LinearVelocity.X, LinearVelocity.Y, 0.0);
    float ForwardSpeed = FVector::DotProduct(HorizontalVelocity, Forward);
    FVector LateralVelocity = HorizontalVelocity - Forward * ForwardSpeed;
    LateralVelocity *= FMath::Exp(-12.0f * DeltaSeconds);
    if (FMath::Abs(Throttle) < 0.01f)
    {
        // Coulomb rolling resistance dissipates small slope-sampling pulses.
        // Unlike velocity-only damping it can hold an idle vehicle at rest;
        // steeper excavated slopes still overcome it with the brake released.
        ForwardSpeed *= FMath::Exp(-1.2f * DeltaSeconds);
        ForwardSpeed = FMath::Sign(ForwardSpeed) * FMath::Max(
            0.0f, FMath::Abs(ForwardSpeed) - 120.0f * DeltaSeconds);
        // Static track friction on near-level ground absorbs tiny asynchronous
        // MPM reaction pulses. It is deliberately disabled on a real slope so
        // gravity can still start the unbraked vehicle moving downhill.
        if (GroundedSupportCount >= 3 &&
            CurrentTerrainGradientMagnitude < 0.15f &&
            HorizontalVelocity.SizeSquared() < FMath::Square(5.0f))
        {
            ForwardSpeed = 0.0f;
            LateralVelocity = FVector::ZeroVector;
        }
    }
    ForwardSpeed = FMath::Clamp(ForwardSpeed, -80.0f, 80.0f);
    LinearVelocity = Forward * ForwardSpeed + LateralVelocity.GetClampedToMaxSize(18.0f);
    LinearVelocity.Z = FMath::Clamp(VerticalSpeed, -450.0f, 220.0f);
    ChassisBody->SetPhysicsLinearVelocity(LinearVelocity);

    FVector AngularVelocity = ChassisBody->GetPhysicsAngularVelocityInRadians();
    if (FMath::Abs(Steering) < 0.01f)
    {
        AngularVelocity.Z *= FMath::Exp(-8.0f * DeltaSeconds);
    }
    AngularVelocity.X *= FMath::Exp(-2.4f * DeltaSeconds);
    AngularVelocity.Y *= FMath::Exp(-2.4f * DeltaSeconds);
    AngularVelocity.X = FMath::Clamp(AngularVelocity.X, -0.9, 0.9);
    AngularVelocity.Y = FMath::Clamp(AngularVelocity.Y, -0.9, 0.9);
    AngularVelocity.Z = FMath::Clamp(AngularVelocity.Z, -1.2, 1.2);
    ChassisBody->SetPhysicsAngularVelocityInRadians(AngularVelocity);

    const float JointDelta = JointSpeedDegreesPerSecond * DeltaSeconds;
    const float PreviousBoomAngle = BoomAngleDegrees;
    BoomAngleDegrees += JointDelta * BoomInput;
    StickAngleDegrees += JointDelta * StickInput;
    BucketAngleDegrees += JointDelta * BucketInput;

    // Q can raise the main boom almost vertically; the downward stop is unchanged.
    BoomAngleDegrees = FMath::Clamp(BoomAngleDegrees, -65.0f, 85.0f);
    if (bBoomRaiseTest && BoomAngleDegrees == 85.0f && PreviousBoomAngle < 85.0f)
    {
        UE_LOG(LogTemp, Display, TEXT("Boom raise acceptance: reached %.1f degrees"), BoomAngleDegrees);
    }
    StickAngleDegrees = FMath::Clamp(StickAngleDegrees, 5.0f, 115.0f);
    // The former -190 degree stop left the bucket opening just above the
    // carried-particle release angle, so a fully commanded dump could retain
    // its load indefinitely. The extra 15 degrees gives the lip a genuinely
    // downward-facing attitude while keeping the linkage clear of the stick.
    BucketAngleDegrees = FMath::Clamp(BucketAngleDegrees, -205.0f, 35.0f);
    UpdateVisualJoints();
}

void ASandExcavatorPawn::UpdateVisualJoints()
{
    BoomPivot->SetRelativeRotation(FRotator(BoomAngleDegrees, 0.0, 0.0));
    StickPivot->SetRelativeRotation(FRotator(StickAngleDegrees, 0.0, 0.0));
    BucketPivot->SetRelativeRotation(FRotator(BucketAngleDegrees, 0.0, 0.0));
}

void ASandExcavatorPawn::UpdateSandSupportSurface(
    const TArray<FVector3f>& ParticlePositionsMeters)
{
    if (ParticlePositionsMeters.IsEmpty())
    {
        return;
    }

    static const FVector2f LocalSamplesCentimeters[4] =
    {
        FVector2f(15.0f, -14.0f),
        FVector2f(-15.0f, -14.0f),
        FVector2f(15.0f, 14.0f),
        FVector2f(-15.0f, 14.0f)
    };
    // A narrow patch cannot bridge a small trench by resting on its far bank.
    constexpr float QueryRadiusCentimeters = 10.5f;
    // The density isosurface lies below particle-centre + half-spacing.
    // Match it and allow a shallow tread indentation instead of a visible gap.
    constexpr float ParticleSurfaceRadiusCentimeters = 2.625f;
    constexpr int32 TopParticleCount = 8;
    const float MaximumGroundHeight = ChassisBody->GetComponentLocation().Z + 22.0f;
    FVector SampleForward = GetActorForwardVector();
    SampleForward.Z = 0.0;
    SampleForward.Normalize();
    FVector SampleRight = GetActorRightVector();
    SampleRight.Z = 0.0;
    SampleRight.Normalize();
    const FVector ChassisLocation = ChassisBody->GetComponentLocation();

    for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex)
    {
        const FVector SampleWorld = ChassisLocation +
            SampleForward * LocalSamplesCentimeters[SampleIndex].X +
            SampleRight * LocalSamplesCentimeters[SampleIndex].Y;
        TStaticArray<float, TopParticleCount> TopHeights;
        for (float& Height : TopHeights)
        {
            Height = -TNumericLimits<float>::Max();
        }
        for (const FVector3f& PositionMeters : ParticlePositionsMeters)
        {
            const float ParticleX = PositionMeters.X * 100.0f;
            const float ParticleY = PositionMeters.Y * 100.0f;
            const float ParticleZ = PositionMeters.Z * 100.0f;
            const float DeltaX = ParticleX - SampleWorld.X;
            const float DeltaY = ParticleY - SampleWorld.Y;
            if (DeltaX * DeltaX + DeltaY * DeltaY <= FMath::Square(QueryRadiusCentimeters) &&
                ParticleZ <= MaximumGroundHeight)
            {
                if (ParticleZ > TopHeights[TopParticleCount - 1])
                {
                    int32 InsertIndex = TopParticleCount - 1;
                    while (InsertIndex > 0 && ParticleZ > TopHeights[InsertIndex - 1])
                    {
                        TopHeights[InsertIndex] = TopHeights[InsertIndex - 1];
                        --InsertIndex;
                    }
                    TopHeights[InsertIndex] = ParticleZ;
                }
            }
        }

        float HeightSum = 0.0f;
        int32 ValidHeightCount = 0;
        for (const float Height : TopHeights)
        {
            if (Height <= -TNumericLimits<float>::Max() * 0.5f)
            {
                break;
            }
            HeightSum += Height;
            ++ValidHeightCount;
        }
        const float MeasuredHeight = ValidHeightCount > 0
            ? FMath::Max(0.0f, HeightSum / ValidHeightCount + ParticleSurfaceRadiusCentimeters)
            : 0.0f;
        ParticleSupportCeilingsCentimeters[SampleIndex] = MeasuredHeight + 1.0f;
    }
}

void ASandExcavatorPawn::UpdateVisibleSandSupport(
    const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
    const FVector Origin = ChassisBody->GetComponentLocation();
    FVector Forward = GetActorForwardVector();
    Forward.Z = 0.0; Forward.Normalize();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex)
    {
        const FVector Sample = Origin + Forward * (SampleIndex % 2 == 0 ? 15.0 : -15.0) +
            Right * (SampleIndex < 2 ? -14.0 : 14.0);
        float Height;
        const float Ceiling = FMath::Min(
            ParticleSupportCeilingsCentimeters[SampleIndex], static_cast<float>(Origin.Z + 22.0));
        bSandSupportSampleValid[SampleIndex] = SampleVisibleSandHeight(
            Vertices, Indices, FVector2D(Sample.X, Sample.Y), Ceiling, Height);
        // The density filter erodes the surface inside a rigid track's void.
        // Following that void without a bearing envelope makes a parked track
        // excavate itself downward indefinitely. Limit visual sinkage to 3 cm
        // below the surrounding non-carried support patch (which itself moves
        // down when terrain is excavated; this is not an absolute height clamp).
        const float BearingFloor = FMath::Max(0.0f, ParticleSupportCeilingsCentimeters[SampleIndex] - 4.0f);
        const float Target = FMath::Max(BearingFloor, Height - 0.5f);
        const float Blend = Target < SandSupportHeightsCentimeters[SampleIndex] - 1.0f ? 0.22f : 0.12f;
        SandSupportHeightsCentimeters[SampleIndex] = bHasSandSupportSamples
            ? FMath::Lerp(SandSupportHeightsCentimeters[SampleIndex], Target, Blend) : Target;
    }
    bHasSandSupportSamples = true;
}

void ASandExcavatorPawn::ApplySandSuspension(
    const float DeltaSeconds,
    const bool bBrakeApplied)
{
    constexpr float TrackBottomOffsetCentimeters = 7.75f;
    constexpr float SuspensionStiffness = 1450.0f;
    constexpr float SuspensionDamping = 360.0f;
    constexpr float MaximumSupportForce = 34000.0f;
    constexpr float ContactRangeCentimeters = 12.0f;
    constexpr float MaximumTerrainSlope = 0.70f; // approximately 35 degrees

    FVector HorizontalForward = GetActorForwardVector();
    HorizontalForward.Z = 0.0;
    if (!HorizontalForward.Normalize())
    {
        HorizontalForward = FVector::ForwardVector;
    }
    const FVector HorizontalRight = FVector::CrossProduct(FVector::UpVector, HorizontalForward);

    GroundedSupportCount = 0;
    CurrentTerrainGradientMagnitude = 0.0f;
    float SupportedHeightSum = 0.0f;
    const float ChassisBottom = ChassisBody->GetComponentLocation().Z - TrackBottomOffsetCentimeters;
    for (int32 SupportIndex = 0; SupportIndex < 4; ++SupportIndex)
    {
        if (!bSandSupportSampleValid[SupportIndex] ||
            ChassisBottom - SandSupportHeightsCentimeters[SupportIndex] > ContactRangeCentimeters)
        {
            continue;
        }
        SupportedHeightSum += SandSupportHeightsCentimeters[SupportIndex];
        ++GroundedSupportCount;
    }

    if (GroundedSupportCount > 0)
    {
        const float AverageGroundHeight = SupportedHeightSum / GroundedSupportCount;
        const float DesiredChassisHeight = AverageGroundHeight + TrackBottomOffsetCentimeters;
        const float HeightError = DesiredChassisHeight - ChassisBody->GetComponentLocation().Z;
        const float VerticalVelocity = ChassisBody->GetPhysicsLinearVelocity().Z;
        const float VehicleWeight = ChassisBody->GetMass() * 980.0f;
        const float TotalSupportForce = FMath::Clamp(
            VehicleWeight + SuspensionStiffness * HeightError - SuspensionDamping * VerticalVelocity,
            0.0f,
            MaximumSupportForce);
        ChassisBody->AddForce(FVector::UpVector * TotalSupportForce, NAME_None, false);

        FVector TerrainGradient = FVector::ZeroVector;
        if (GroundedSupportCount == 4)
        {
            const float FrontHeight = 0.5f * (
                SandSupportHeightsCentimeters[0] + SandSupportHeightsCentimeters[2]);
            const float RearHeight = 0.5f * (
                SandSupportHeightsCentimeters[1] + SandSupportHeightsCentimeters[3]);
            const float LeftHeight = 0.5f * (
                SandSupportHeightsCentimeters[0] + SandSupportHeightsCentimeters[1]);
            const float RightHeight = 0.5f * (
                SandSupportHeightsCentimeters[2] + SandSupportHeightsCentimeters[3]);
            FVector LocalGradient(
                (FrontHeight - RearHeight) / 30.0f,
                (RightHeight - LeftHeight) / 28.0f,
                0.0f);
            LocalGradient = LocalGradient.GetClampedToMaxSize(MaximumTerrainSlope);
            constexpr float FlatGroundGradientDeadZone = 0.045f;
            const float GradientMagnitude = LocalGradient.Size();
            if (GradientMagnitude <= FlatGroundGradientDeadZone)
            {
                LocalGradient = FVector::ZeroVector;
            }
            else
            {
                LocalGradient *=
                    (GradientMagnitude - FlatGroundGradientDeadZone) / GradientMagnitude;
            }
            TerrainGradient = HorizontalForward * LocalGradient.X + HorizontalRight * LocalGradient.Y;
            CurrentTerrainGradientMagnitude = TerrainGradient.Size();
            FVector MeasuredNormal = FVector::UpVector - TerrainGradient;
            MeasuredNormal.Normalize();
            SmoothedSandSurfaceNormal = FMath::VInterpNormalRotationTo(
                SmoothedSandSurfaceNormal, MeasuredNormal, DeltaSeconds, 3.0f);
        }

        // A tracked chassis is strongly constrained by its two long contact
        // patches. Smoothly constrain pitch/roll to the sampled support plane;
        // translation, gravity, yaw, traction and downhill motion remain in
        // Chaos. This prevents a 40 cm vehicle from being flipped by one noisy
        // particle sample or a bucket impulse.
        FVector TargetForward = FVector::VectorPlaneProject(
            HorizontalForward, SmoothedSandSurfaceNormal);
        TargetForward.Normalize();
        const FQuat TargetRotation = FRotationMatrix::MakeFromXZ(
            TargetForward, SmoothedSandSurfaceNormal).ToQuat();
        const float RotationBlend = 1.0f - FMath::Exp(-4.0f * DeltaSeconds);
        const FQuat ConstrainedRotation = FQuat::Slerp(
            ChassisBody->GetComponentQuat(), TargetRotation, RotationBlend).GetNormalized();
        ChassisBody->SetWorldRotation(
            ConstrainedRotation, false, nullptr, ETeleportType::TeleportPhysics);
        const FVector ExistingAngularVelocity =
            ChassisBody->GetPhysicsAngularVelocityInRadians();
        ChassisBody->SetPhysicsAngularVelocityInRadians(
            FVector::UpVector * ExistingAngularVelocity.Z);

        // A normal support force would naturally leave this tangential gravity
        // component. Apply it at the centre so a released brake lets the tracks
        // move down a player-made slope without injecting roll torque.
        // Sub-grid roughness is not a reliable hill. Below this effective
        // static-friction threshold the support can hold the tangential load;
        // driving still follows sampled terrain height, including shallow ramps.
        if (!bBrakeApplied && CurrentTerrainGradientMagnitude > 0.15f)
        {
            const FVector DownhillForce = -TerrainGradient *
                ChassisBody->GetMass() * 980.0f * 0.72f;
            ChassisBody->AddForce(DownhillForce, NAME_None, false);
        }
    }
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandVisibleSupportTest, "SandSimulation.Contact.VisibleSurfaceSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandVisibleSupportTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> Vertices = {
        FVector(0,0,10), FVector(10,0,20), FVector(0,10,10),
        FVector(0,0,50), FVector(10,0,50), FVector(0,10,50)};
    const TArray<int32> Indices = {0,1,2,3,4,5};
    float Height;
    TestTrue(TEXT("Slope intersects"), SampleVisibleSandHeight(Vertices, Indices, FVector2D(2,2), 30, Height));
    TestTrue(TEXT("Slope height, ignoring overhead load"), FMath::IsNearlyEqual(Height, 12.0f));
    TestTrue(TEXT("Upper surface intersects"), SampleVisibleSandHeight(Vertices, Indices, FVector2D(2,2), 60, Height));
    TestEqual(TEXT("Select highest permitted 3D layer"), Height, 50.0f);
    TestFalse(TEXT("No bridging across missing mesh"), SampleVisibleSandHeight(Vertices, Indices, FVector2D(9,9), 60, Height));
    TestFalse(TEXT("Ceiling excludes unsupported overhead mesh"), SampleVisibleSandHeight(Vertices, Indices, FVector2D(2,2), 5, Height));
    return true;
}
#endif
