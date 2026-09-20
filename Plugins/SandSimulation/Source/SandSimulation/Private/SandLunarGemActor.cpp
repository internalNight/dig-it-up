#include "SandLunarGemActor.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ASandLunarGemActor::ASandLunarGemActor()
{
    PrimaryActorTick.bCanEverTick = false;
    GemMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GemMesh"));
    SetRootComponent(GemMesh);
    GemMesh->SetMobility(EComponentMobility::Movable);
    GemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GemMesh->SetCollisionProfileName(TEXT("BlockAll"));
    GemMesh->SetSimulatePhysics(false);
    GemMesh->SetCastShadow(true);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BaseMaterial.Succeeded())
    {
        GemMesh->SetMaterial(0,BaseMaterial.Object);
    }
}

void ASandLunarGemActor::Configure(const float DiameterMeters)
{
    RadiusCentimeters = 50.0f * FMath::Clamp(DiameterMeters,0.08f,0.32f);
    BuildGemMesh();
}

void ASandLunarGemActor::BeginPlay()
{
    Super::BeginPlay();
    if (GemMesh->GetNumSections() == 0)
    {
        BuildGemMesh();
    }
    if (UMaterialInstanceDynamic* Material = GemMesh->CreateAndSetMaterialInstanceDynamic(0))
    {
        const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(242,174,31));
        Material->SetVectorParameterValue(TEXT("Color"),Gold);
        Material->SetVectorParameterValue(TEXT("DiffuseColor"),Gold);
        Material->SetScalarParameterValue(TEXT("Metallic"),0.82f);
        Material->SetScalarParameterValue(TEXT("Roughness"),0.22f);
    }
}

void ASandLunarGemActor::BuildGemMesh()
{
    // An irregular eight-sided crystal: clearly a faceted specimen rather
    // than a sphere/ellipsoid, with a slightly broader lower shoulder.
    TArray<FVector> Vertices;
    Vertices.Add(FVector(0,0,1.25f * RadiusCentimeters));
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const float Angle = 2.0f * PI * Index / 8.0f + 0.11f;
        const float RadiusScale = Index % 2 == 0 ? 1.0f : 0.82f;
        Vertices.Add(FVector(
            RadiusCentimeters * RadiusScale * FMath::Cos(Angle),
            RadiusCentimeters * RadiusScale * FMath::Sin(Angle),
            -0.12f * RadiusCentimeters));
    }
    Vertices.Add(FVector(0,0,-0.90f * RadiusCentimeters));

    TArray<int32> Triangles;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const int32 A = 1 + Index;
        const int32 B = 1 + (Index + 1) % 8;
        Triangles.Append({0,A,B,9,B,A});
    }

    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    GemMesh->ClearAllMeshSections();
    GemMesh->CreateMeshSection_LinearColor(
        0,Vertices,Triangles,Normals,UVs,Colors,Tangents,true);
    GemMesh->ClearCollisionConvexMeshes();
    GemMesh->AddCollisionConvexMesh(Vertices);
}

float ASandLunarGemActor::GetTopWorldZ() const
{
    return GetActorLocation().Z + 1.25f * RadiusCentimeters;
}
