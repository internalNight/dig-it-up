#include "SandMachineKinematics.h"
#include "SandMPMSolver.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
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
#endif
