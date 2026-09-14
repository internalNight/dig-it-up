#include "SandBearing.h"
#include "SandMachineKinematics.h"
#include "SandMPMSolver.h"
#include "SandMachineDrive.h"
#include "SandTraction.h"
#include "SandMachineGeometry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandTractionTest,"SandSimulation.Machine.FiniteTraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandTractionTest::RunTest(const FString&)
{
    using namespace Sand::Machine;
    TestTrue(TEXT("No airborne traction"),TractionForce(12,0,.6f,.1f,FVector2f::ZeroVector,120).IsNearlyZero());
    for(float N : {10.f,100.f}) for(float Target : {-1.f,0.f,1.f}) {
        const FVector2f V(.3f,.2f);
        const auto F=TractionForce(12,N,.6f,Target,V,120);
        TestTrue(TEXT("Longitudinal plus lateral share friction budget"),F.Size()<=.6f*N+.001f);
        if(Target==0) TestTrue(TEXT("Brake cannot add kinetic energy"),FVector2f::DotProduct(F,V)<=0);
    }
    TestEqual(TEXT("Severe draft removes positive feed"),FeedFraction(80,0),0.f);
    TestEqual(TEXT("No-load feed is available"),FeedFraction(0,0),1.f);
    TestEqual(TEXT("Overload pauses manual forward penetration"),DriveFeedFraction(1.f,0.f),0.f);
    TestEqual(TEXT("Reverse remains available during overload relief"),DriveFeedFraction(-1.f,0.f),1.f);
    const float Dt=1.f/60;
    const FVector2f Slow(.005f,.002f),Fast(2.f,0);
    TestTrue(TEXT("Finite brake stops a supportable velocity within one step"),
        (Slow+TractionForce(12,100,.6f,0,Slow,120,Dt)*Dt/12).Size()<.00001f);
    TestTrue(TEXT("Brake cannot hold beyond available friction impulse"),
        (Fast+TractionForce(12,10,.6f,0,Fast,120,Dt)*Dt/12).X>1.9f);
    // At zero speed a finite static contact must balance a supportable
    // external load. A velocity-only servo asks for zero and creeps forever.
    const FVector2f Load(-35,12);
    const FVector2f Hold=TractionForce(12,120,.6f,0,FVector2f::ZeroVector,120,.033f,Load);
    TestTrue(TEXT("Static contact balances sustained load"),(Hold+Load).Size()<.0001f);
    const FVector2f Overload(-100,0);
    const FVector2f Limited=TractionForce(12,100,.6f,0,FVector2f::ZeroVector,120,.033f,Overload);
    TestTrue(TEXT("Overload still slides"),(Limited+Overload).X< -39.9f);
    return true;
}
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandAssemblySATTest,"SandSimulation.Machine.IndependentSolidClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandAssemblySATTest::RunTest(const FString&)
{
    Sand::MPM::FToolOrientedBoxState A,B;
    A.HalfExtentsMeters=B.HalfExtentsMeters=FVector3f(.1f);
    B.CenterMeters=FVector3f(.25f,0,0);
    TestEqual(TEXT("Separated solids"),Sand::Machine::BoxOverlapDepth(A,B),0.f);
    B.CenterMeters.X=.19f;
    TestTrue(TEXT("Detect 10 mm interference"),FMath::IsNearlyEqual(Sand::Machine::BoxOverlapDepth(A,B),.01f,.0001f));
    const FQuat4f Q(FVector3f(0,1,0),PI/4);
    B.AxisX=Q.GetAxisX(); B.AxisZ=Q.GetAxisZ(); B.CenterMeters.X=.23f;
    TestTrue(TEXT("Rotated corner interference"),Sand::Machine::BoxOverlapDepth(A,B)>.01f);
    B.CenterMeters.X=.25f;
    TestEqual(TEXT("Rotated corner clears"),Sand::Machine::BoxOverlapDepth(A,B),0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandBeltSkinTest,"SandSimulation.Machine.BeltSkinAndRaisedFlight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandBeltSkinTest::RunTest(const FString&)
{
    using namespace Sand::MPM;
    FToolOrientedBoxState C; C.Motion=4; C.CenterMeters=FVector3f(.2f,0,.3f); C.Speed=-.3f;
    const auto A=SampleMachineCollider(C,2);
    TestTrue(TEXT("Endless belt envelope stays in place"),(A.CenterMeters-C.CenterMeters).IsNearlyZero());
    TestTrue(TEXT("Belt skin has commanded contact velocity"),(A.LinearVelocityMetersPerSecond-FVector3f(-.3f,0,0)).IsNearlyZero());
    C.Speed=0;
    TestTrue(TEXT("Stopped belt cannot keep conveying by surface velocity"),SampleMachineCollider(C,2).LinearVelocityMetersPerSecond.IsNearlyZero());
    C.Motion=2; C.Speed=.3f; C.Phase=Sand::Machine::Run+.03f; C.SurfaceOffset=.014f;
    const auto P=SampleMachineCollider(C,.1f),Q=SampleMachineCollider(C,.1001f);
    TestTrue(TEXT("Raised flight follows belt curvature with matching velocity"),((Q.CenterMeters-P.CenterMeters)/.0001f-P.LinearVelocityMetersPerSecond).Length()<.003f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandPhysicalClockTest,"SandSimulation.Machine.ExactCoupledSubsteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandPhysicalClockTest::RunTest(const FString&)
{
    for(float Hz : {30.f,60.f,120.f}) for(float InnerHz : {600.f,900.f,1200.f}) {
        const float Outer=1/Hz, Maximum=1/InnerHz;
        const float Dt=Sand::MPM::FittedInternalStep(Outer,Maximum);
        const int32 N=FMath::RoundToInt(Outer/Dt);
        TestTrue(TEXT("Internal steps exactly span the rigid step"),FMath::Abs(N*Dt-Outer)<1.e-7f);
        TestTrue(TEXT("Fitting cannot relax the stability step"),Dt<=Maximum+1.e-8f);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandBearingEnvelopeTest,"SandSimulation.Machine.BearingRejectsAirborneSamples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSandBearingEnvelopeTest::RunTest(const FString&)
{
    TArray<float> H;
    for(int32 Z=0;Z<20;++Z) for(int32 I=0;I<10;++I) H.Add(2.5f+5*Z);
    float Level=0;
    TestTrue(TEXT("Connected bulk supports"),Sand::Machine::ConnectedBearingHeight(H,5,250,Level));
    TestEqual(TEXT("Initial bulk surface"),Level,100.f);
    H.Add(145); H.Add(150);
    TestTrue(TEXT("Bulk still supports with airborne samples"),Sand::Machine::ConnectedBearingHeight(H,5,250,Level));
    TestEqual(TEXT("Airborne samples cannot raise terrain"),Level,100.f);
    H.RemoveAll([](float Z){return Z>=50 && Z<55;});
    for(int32 I=0;I<10;++I) H.Add(57.5f);
    TestTrue(TEXT("One sampling-scale sparse bin keeps bulk support"),Sand::Machine::ConnectedBearingHeight(H,5,250,Level));
    TestEqual(TEXT("Connected bulk continues above one sparse bin"),Level,100.f);
    H.RemoveAll([](float Z){return Z>=60 && Z<70;});
    Sand::Machine::ConnectedBearingHeight(H,5,250,Level);
    TestEqual(TEXT("Two consecutive sparse bins stop support"),Level,60.f);
    for(int32 Z=12;Z<20;++Z) for(int32 I=0;I<10;++I) H.Add(2.5f+5*Z);
    for(int32 I=0;I<10;++I) H.Add(102.5f);
    Sand::Machine::ConnectedBearingHeight(H,5,250,Level);
    TestEqual(TEXT("Continuous deposition raises terrain"),Level,105.f);
    H.RemoveAll([](float Z){return Z>=90;});
    Sand::Machine::ConnectedBearingHeight(H,5,250,Level);
    TestEqual(TEXT("Excavation lowers terrain"),Level,90.f);
    return true;
}
#endif
