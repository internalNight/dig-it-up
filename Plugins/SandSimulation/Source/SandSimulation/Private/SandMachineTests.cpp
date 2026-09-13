#include "SandMachineKinematics.h"
#include "SandMPMSolver.h"
#include "SandMachineDrive.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandDriveLoadTest,"SandSimulation.Machine.DriveUnderLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandDriveLoadTest::RunTest(const FString&)
{
    using namespace Sand::Machine;
    // Reproduce the user's 95 Nm load. It must settle forwards, at finite torque,
    // under either supported outer-step duration, instead of oscillating/reversing.
    for(float Dt : {1.0f/30,1.0f/15})
    {
        float Omega=0;
        for(int32 I=0;I<300;++I)
        {
            const float Next=DrivenSpeed(Omega,1.8f,95*Dt,Dt,12,180,240);
            const float Torque=(12*(Next-Omega)+95*Dt)/Dt;
            TestTrue(TEXT("Finite actuator torque"),FMath::Abs(Torque)<=240.001f);
            TestTrue(TEXT("95 Nm load does not reverse the drive"),Next>=0);
            Omega=Next;
        }
        TestTrue(TEXT("Steady speed agrees with load balance"),FMath::Abs(Omega-(1.8f-95.0f/180))<.001f);
        TestEqual(TEXT("Holding brake arrests a small backdrive"),BrakeSpeed(-.1f,Dt,12,300),0.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandChainPathTest,"SandSimulation.Machine.ClosedChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandChainPathTest::RunTest(const FString&)
{
    using namespace Sand::Machine;
    // Geometry and derivative continuity prevent a blade teleporting at a wrap.
    for(float S : {0.0f,Run,Run+PI*Radius,2*Run+PI*Radius,Loop})
    {
        FVector3f A,B,TA,TB;
        ChainPose(S-1.e-5f,A,TA); ChainPose(S+1.e-5f,B,TB);
        TestTrue(TEXT("Continuous position at each sprocket seam"),(A-B).Length()<1.e-4f);
        TestTrue(TEXT("Continuous contact velocity at each seam"),(TA-TB).Length()<.001f);
    }
    for(int32 I=-100;I<100;++I)
    {
        const float S=I*.03f; FVector3f P,T,Q,U;
        ChainPose(S,P,T); ChainPose(S+Loop,Q,U);
        TestTrue(TEXT("Closed path also works in reverse"),(P-Q).Length()<1.e-5f);
        TestTrue(TEXT("Unit speed parameterization"),FMath::Abs(T.Length()-1)<1.e-5f);
        ChainPose(S+.0001f,Q,U);
        TestTrue(TEXT("Contact velocity agrees with trajectory"),((Q-P)/.0001f-T).Length()<.005f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandRotorMotionTest,"SandSimulation.Machine.RotorContactVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandRotorMotionTest::RunTest(const FString&)
{
    Sand::MPM::FToolOrientedBoxState C;
    C.Motion=1; C.Speed=-3; C.Phase=.4f;
    C.MotionRotation=FQuat4f(FVector3f(0,0,1),.7f);
    const auto A=Sand::MPM::SampleMachineCollider(C,.05f);
    const auto B=Sand::MPM::SampleMachineCollider(C,.0501f);
    TestTrue(TEXT("Rotating collider position and contact velocity match"),
        ((B.CenterMeters-A.CenterMeters)/.0001f-A.LinearVelocityMetersPerSecond).Length()<.002f);
    TestTrue(TEXT("Orthonormal axes"),FMath::Abs(FVector3f::DotProduct(A.AxisX,A.AxisZ))<1.e-5f);
    C.Speed=0;
    const auto Stopped=Sand::MPM::SampleMachineCollider(C,12);
    TestTrue(TEXT("Stopped rotor has zero prescribed velocity"),Stopped.LinearVelocityMetersPerSecond.IsNearlyZero());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandGeneralRotorTest,"SandSimulation.Machine.ArbitraryRotorVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandGeneralRotorTest::RunTest(const FString&)
{
    for(const FVector3f Axis : {FVector3f(1,0,0),FVector3f(0,1,0)})
    for(float Speed : {-1.8f,0.0f,1.8f})
    {
        Sand::MPM::FToolOrientedBoxState C;
        C.Motion=3; C.RotorAxis=Axis; C.Speed=Speed; C.Phase=.3f;
        C.RotorOffset=FVector3f(.08f,.12f,.03f);
        C.RotorOrientation=FQuat4f(FVector3f(0,0,1),.4f);
        C.MotionRotation=FQuat4f(FVector3f(0,1,0),.2f);
        C.BaseVelocity=FVector3f(.05f,0,0);
        const auto A=Sand::MPM::SampleMachineCollider(C,.2f);
        const auto B=Sand::MPM::SampleMachineCollider(C,.2001f);
        TestTrue(TEXT("Rotor translation derivative matches contact velocity"),
            ((B.CenterMeters-A.CenterMeters)/.0001f-A.LinearVelocityMetersPerSecond).Length()<.003f);
        // A surface point must also include angular velocity, not just orbit velocity.
        const FVector3f PA=A.CenterMeters+.04f*A.AxisX;
        const FVector3f PB=B.CenterMeters+.04f*B.AxisX;
        const FVector3f VA=A.LinearVelocityMetersPerSecond+FVector3f::CrossProduct(A.AngularVelocityRadiansPerSecond,.04f*A.AxisX);
        TestTrue(TEXT("Surface point velocity matches spinning geometry"),((PB-PA)/.0001f-VA).Length()<.004f);
    }
    return true;
}
#endif
