#include "SandSurfacePreviewActor.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "UObject/ConstructorHelpers.h"
#include "GlobalShader.h"
#include "Generators/MarchingCubes.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"
#include "UnrealClient.h"

namespace Sand::Surface
{
constexpr float FixedPointScale = 65536.0f;

struct alignas(16) FSandDensityParticleData
{
    FVector4f PositionAndWeight;
};

static_assert(sizeof(FSandDensityParticleData) == 16);

struct FDensityFieldDescription
{
    FIntVector Resolution = FIntVector::ZeroValue;
    FVector3f MinimumMeters = FVector3f::ZeroVector;
    float VoxelSizeMeters = 0.025f;
    float KernelRadiusMeters = 0.055f;
    float IsoDensity = 0.85f;

    int32 NodeCount() const
    {
        return Resolution.X * Resolution.Y * Resolution.Z;
    }
};

struct FSurfaceMeshData
{
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
};

class FSandSplatDensityCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandSplatDensityCS);
    SHADER_USE_PARAMETER_STRUCT(FSandSplatDensityCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, DensityParticleCount)
        SHADER_PARAMETER(uint32, DensityGridSizeX)
        SHADER_PARAMETER(uint32, DensityGridSizeY)
        SHADER_PARAMETER(uint32, DensityGridSizeZ)
        SHADER_PARAMETER(float, DensityVoxelSizeMeters)
        SHADER_PARAMETER(float, DensityKernelRadiusMeters)
        SHADER_PARAMETER(float, DensityFixedPointScale)
        SHADER_PARAMETER(FVector3f, DensityGridMinimumMeters)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSandDensityParticleData>, DensityParticles)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, DensityFieldFixedPoint)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(
    FSandSplatDensityCS,
    "/SandSimulation/Private/SandDensityField.usf",
    "SplatDensityCS",
    SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FDensityReadbackParameters, )
    RDG_BUFFER_ACCESS(SourceBuffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

int32 FlattenNode(const FIntVector& Node, const FIntVector& Resolution)
{
    return Node.X + Resolution.X * (Node.Y + Resolution.Y * Node.Z);
}

FVector3f NodePositionMeters(const FIntVector& Node, const FDensityFieldDescription& Field)
{
    return Field.MinimumMeters + FVector3f(Node) * Field.VoxelSizeMeters;
}

FVector3f InterpolateIsoVertex(
    const FVector3f& A,
    const FVector3f& B,
    const float DensityA,
    const float DensityB,
    const float IsoDensity)
{
    const float Denominator = DensityB - DensityA;
    const float T = FMath::IsNearlyZero(Denominator)
        ? 0.5f
        : FMath::Clamp((IsoDensity - DensityA) / Denominator, 0.0f, 1.0f);
    return FMath::Lerp(A, B, T);
}

void AppendTriangle(
    FSurfaceMeshData& Mesh,
    FVector3f A,
    FVector3f B,
    FVector3f C,
    const FVector3f& DesiredOutwardDirection)
{
    FVector3f Normal = FVector3f::CrossProduct(B - A, C - A);
    if (FVector3f::DotProduct(Normal, DesiredOutwardDirection) < 0.0f)
    {
        Swap(B, C);
        Normal = -Normal;
    }
    if (!Normal.Normalize())
    {
        return;
    }

    constexpr float UnrealUnitsPerMeter = 100.0f;
    const int32 FirstIndex = Mesh.Vertices.Num();
    Mesh.Vertices.Append({ FVector(A * UnrealUnitsPerMeter), FVector(B * UnrealUnitsPerMeter), FVector(C * UnrealUnitsPerMeter) });
    Mesh.Indices.Append({ FirstIndex, FirstIndex + 2, FirstIndex + 1 });
    // Keep the topology preview's lighting stable while vertices are still
    // emitted per triangle.  Shared-vertex, filtered density normals are the
    // next rendering-stage task; the simulation itself does not use this data.
    const FVector PreviewNormal = FVector::UpVector;
    Mesh.Normals.Append({ PreviewNormal, PreviewNormal, PreviewNormal });
    Mesh.UVs.Append({ FVector2D(A.X, A.Y), FVector2D(B.X, B.Y), FVector2D(C.X, C.Y) });
    const FLinearColor SandColor(0.46f, 0.22f, 0.055f, 1.0f);
    Mesh.Colors.Append({ SandColor, SandColor, SandColor });
}

float SampleDensityMeters(
    const TArray<uint32>& FixedDensity,
    const FDensityFieldDescription& Field,
    const FVector3f& PositionMeters)
{
    const FVector3f GridCoordinate = (PositionMeters - Field.MinimumMeters) / Field.VoxelSizeMeters;
    const FIntVector Base(
        FMath::Clamp(FMath::FloorToInt(GridCoordinate.X), 0, Field.Resolution.X - 2),
        FMath::Clamp(FMath::FloorToInt(GridCoordinate.Y), 0, Field.Resolution.Y - 2),
        FMath::Clamp(FMath::FloorToInt(GridCoordinate.Z), 0, Field.Resolution.Z - 2));
    const FVector3f Fraction(
        FMath::Clamp(GridCoordinate.X - Base.X, 0.0f, 1.0f),
        FMath::Clamp(GridCoordinate.Y - Base.Y, 0.0f, 1.0f),
        FMath::Clamp(GridCoordinate.Z - Base.Z, 0.0f, 1.0f));

    float Samples[2][2][2];
    for (int32 Z = 0; Z < 2; ++Z)
    {
        for (int32 Y = 0; Y < 2; ++Y)
        {
            for (int32 X = 0; X < 2; ++X)
            {
                Samples[Z][Y][X] = FixedDensity[FlattenNode(Base + FIntVector(X, Y, Z), Field.Resolution)] /
                    FixedPointScale;
            }
        }
    }

    const float X00 = FMath::Lerp(Samples[0][0][0], Samples[0][0][1], Fraction.X);
    const float X10 = FMath::Lerp(Samples[0][1][0], Samples[0][1][1], Fraction.X);
    const float X01 = FMath::Lerp(Samples[1][0][0], Samples[1][0][1], Fraction.X);
    const float X11 = FMath::Lerp(Samples[1][1][0], Samples[1][1][1], Fraction.X);
    return FMath::Lerp(
        FMath::Lerp(X00, X10, Fraction.Y),
        FMath::Lerp(X01, X11, Fraction.Y),
        Fraction.Z);
}

TArray<uint32> SmoothDensityField(
    const TArray<uint32>& FixedDensity,
    const FDensityFieldDescription& Field,
    const int32 PassCount = 1)
{
    TArray<uint32> Current = FixedDensity;
    TArray<uint32> Filtered;
    Filtered.SetNumUninitialized(FixedDensity.Num());

    for (int32 Pass = 0; Pass < PassCount; ++Pass)
    {
        for (int32 Z = 0; Z < Field.Resolution.Z; ++Z)
        {
            for (int32 Y = 0; Y < Field.Resolution.Y; ++Y)
            {
                for (int32 X = 0; X < Field.Resolution.X; ++X)
                {
                    uint64 WeightedDensity = 0;
                    uint32 TotalWeight = 0;
                    for (int32 OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
                    {
                        const int32 SampleZ = Z + OffsetZ;
                        if (SampleZ < 0 || SampleZ >= Field.Resolution.Z)
                        {
                            continue;
                        }
                        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                        {
                            const int32 SampleY = Y + OffsetY;
                            if (SampleY < 0 || SampleY >= Field.Resolution.Y)
                            {
                                continue;
                            }
                            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                            {
                                const int32 SampleX = X + OffsetX;
                                if (SampleX < 0 || SampleX >= Field.Resolution.X)
                                {
                                    continue;
                                }
                                const uint32 Weight =
                                    (OffsetX == 0 ? 2u : 1u) *
                                    (OffsetY == 0 ? 2u : 1u) *
                                    (OffsetZ == 0 ? 2u : 1u);
                                WeightedDensity += static_cast<uint64>(Current[FlattenNode(
                                    FIntVector(SampleX, SampleY, SampleZ), Field.Resolution)]) * Weight;
                                TotalWeight += Weight;
                            }
                        }
                    }
                    Filtered[FlattenNode(FIntVector(X, Y, Z), Field.Resolution)] =
                        static_cast<uint32>(WeightedDensity / TotalWeight);
                }
            }
        }
        Swap(Current, Filtered);
    }
    return Current;
}

void PolygonizeTetrahedron(
    FSurfaceMeshData& Mesh,
    const FVector3f Positions[4],
    const float Densities[4],
    const float IsoDensity)
{
    FVector3f InsideCentroid = FVector3f::ZeroVector;
    FVector3f OutsideCentroid = FVector3f::ZeroVector;
    int32 InsideCorners[3];
    int32 OutsideCorners[3];
    int32 InsideCount = 0;
    int32 OutsideCount = 0;
    for (int32 Corner = 0; Corner < 4; ++Corner)
    {
        if (Densities[Corner] >= IsoDensity)
        {
            InsideCentroid += Positions[Corner];
            InsideCorners[InsideCount++] = Corner;
        }
        else
        {
            OutsideCentroid += Positions[Corner];
            OutsideCorners[OutsideCount++] = Corner;
        }
    }
    if (InsideCount == 0 || OutsideCount == 0)
    {
        return;
    }
    InsideCentroid /= static_cast<float>(InsideCount);
    OutsideCentroid /= static_cast<float>(OutsideCount);
    FVector3f OutwardDirection = OutsideCentroid - InsideCentroid;
    OutwardDirection.Normalize();

    const auto Intersection = [Positions, Densities, IsoDensity](const int32 Inside, const int32 Outside)
    {
        return InterpolateIsoVertex(
            Positions[Inside], Positions[Outside], Densities[Inside], Densities[Outside], IsoDensity);
    };

    if (InsideCount == 1)
    {
        const int32 I = InsideCorners[0];
        AppendTriangle(
            Mesh,
            Intersection(I, OutsideCorners[0]),
            Intersection(I, OutsideCorners[1]),
            Intersection(I, OutsideCorners[2]),
            OutwardDirection);
        return;
    }

    if (InsideCount == 3)
    {
        const int32 O = OutsideCorners[0];
        AppendTriangle(
            Mesh,
            Intersection(InsideCorners[0], O),
            Intersection(InsideCorners[1], O),
            Intersection(InsideCorners[2], O),
            OutwardDirection);
        return;
    }

    const FVector3f P00 = Intersection(InsideCorners[0], OutsideCorners[0]);
    const FVector3f P01 = Intersection(InsideCorners[0], OutsideCorners[1]);
    const FVector3f P10 = Intersection(InsideCorners[1], OutsideCorners[0]);
    const FVector3f P11 = Intersection(InsideCorners[1], OutsideCorners[1]);
    AppendTriangle(Mesh, P00, P01, P11, OutwardDirection);
    AppendTriangle(Mesh, P00, P11, P10, OutwardDirection);
}

FSurfaceMeshData BuildSurfaceMesh(
    const TArray<uint32>& FixedDensity,
    const FDensityFieldDescription& Field)
{
    const TArray<uint32> SmoothedDensity = SmoothDensityField(FixedDensity, Field);
    UE::Geometry::FMarchingCubes MarchingCubes;
    const FVector3f MaximumMeters = Field.MinimumMeters +
        FVector3f(Field.Resolution - FIntVector(1, 1, 1)) * Field.VoxelSizeMeters;
    MarchingCubes.Bounds = UE::Geometry::FAxisAlignedBox3d(
        FVector3d(Field.MinimumMeters), FVector3d(MaximumMeters));
    MarchingCubes.CubeSize = Field.VoxelSizeMeters;
    MarchingCubes.IsoValue = Field.IsoDensity;
    MarchingCubes.RootMode = UE::Geometry::ERootfindingModes::SingleLerp;
    MarchingCubes.bEnableValueCaching = false;
    MarchingCubes.bParallelCompute = true;
    MarchingCubes.Implicit = [&SmoothedDensity, Field](const FVector3d Position)
    {
        return static_cast<double>(SampleDensityMeters(
            SmoothedDensity, Field, FVector3f(Position)));
    };
    MarchingCubes.Generate();

    constexpr double UnrealUnitsPerMeter = 100.0;
    FSurfaceMeshData Mesh;
    Mesh.Vertices.Reserve(MarchingCubes.Vertices.Num());
    Mesh.Normals.Reserve(MarchingCubes.Vertices.Num());
    Mesh.UVs.Reserve(MarchingCubes.Vertices.Num());
    Mesh.Colors.Reserve(MarchingCubes.Vertices.Num());
    for (const FVector3d& Vertex : MarchingCubes.Vertices)
    {
        const FVector3f PositionMeters(Vertex);
        const float GradientStep = Field.VoxelSizeMeters;
        FVector3f OutwardNormal(
            SampleDensityMeters(SmoothedDensity, Field, PositionMeters - FVector3f(GradientStep, 0.0f, 0.0f)) -
                SampleDensityMeters(SmoothedDensity, Field, PositionMeters + FVector3f(GradientStep, 0.0f, 0.0f)),
            SampleDensityMeters(SmoothedDensity, Field, PositionMeters - FVector3f(0.0f, GradientStep, 0.0f)) -
                SampleDensityMeters(SmoothedDensity, Field, PositionMeters + FVector3f(0.0f, GradientStep, 0.0f)),
            SampleDensityMeters(SmoothedDensity, Field, PositionMeters - FVector3f(0.0f, 0.0f, GradientStep)) -
                SampleDensityMeters(SmoothedDensity, Field, PositionMeters + FVector3f(0.0f, 0.0f, GradientStep)));
        if (!OutwardNormal.Normalize())
        {
            OutwardNormal = FVector3f::UpVector;
        }
        // Keep the bulk shape tied to the MPM density while restoring a small,
        // deterministic granular scale that the 5 cm extraction grid cannot
        // represent on its own. World-space noise stays fixed as the mesh is
        // rebuilt, avoiding the "swimming" produced by frame-random offsets.
        const float TopSurfaceWeight = FMath::SmoothStep(0.05f, 0.65f, FMath::Abs(OutwardNormal.Z));
        const float BroadNoise = FMath::PerlinNoise2D(FVector2D(Vertex.X, Vertex.Y) * 7.5f);
        const float FineNoise = FMath::PerlinNoise2D(
            FVector2D(Vertex.X + 19.31, Vertex.Y - 7.17) * 18.0f);
        const float DetailOffsetMeters = TopSurfaceWeight * (0.0015f * BroadNoise + 0.0005f * FineNoise);
        const FVector3d DetailedVertex = Vertex + FVector3d(OutwardNormal) * DetailOffsetMeters;
        Mesh.Vertices.Add(FVector(DetailedVertex * UnrealUnitsPerMeter));
        Mesh.Normals.Add(FVector(OutwardNormal));
        Mesh.UVs.Add(FVector2D(Vertex.X, Vertex.Y));
        const float ToneVariation = 0.93f + 0.07f * BroadNoise;
        Mesh.Colors.Add(FLinearColor(0.23f, 0.24f, 0.26f, 1.0f) * ToneVariation);
    }
    Mesh.Indices.Reserve(MarchingCubes.Triangles.Num() * 3);
    for (const UE::Geometry::FIndex3i& Triangle : MarchingCubes.Triangles)
    {
        int32 B = Triangle.B;
        int32 C = Triangle.C;
        const FVector GeometricNormal = FVector::CrossProduct(
            Mesh.Vertices[B] - Mesh.Vertices[Triangle.A],
            Mesh.Vertices[C] - Mesh.Vertices[Triangle.A]);
        const FVector ShadingNormal =
            Mesh.Normals[Triangle.A] + Mesh.Normals[B] + Mesh.Normals[C];
        // UE rasterizes clockwise fronts in its left-handed coordinate system,
        // which is the opposite ordering of the conventional cross-product
        // test used for the outward shading normal.
        if (FVector::DotProduct(GeometricNormal, ShadingNormal) > 0.0)
        {
            Swap(B, C);
        }
        Mesh.Indices.Append({ Triangle.A, B, C });
    }

    return Mesh;
}

TArray<FSandDensityParticleData> MakePreviewPile(const float Spacing)
{
    TArray<FSandDensityParticleData> Particles;
    constexpr float RadiusX = 0.72f;
    constexpr float RadiusY = 0.52f;
    constexpr float Slope = 0.577350269f; // tan(30 degrees)

    for (float Z = 0.5f * Spacing; Z < 0.55f; Z += Spacing)
    {
        for (float Y = -RadiusY; Y <= RadiusY; Y += Spacing)
        {
            for (float X = -RadiusX; X <= RadiusX; X += Spacing)
            {
                const float EllipticalRadius = FMath::Sqrt(
                    FMath::Square(X / RadiusX) + FMath::Square(Y / RadiusY));
                const float SurfaceHeight = FMath::Max(0.0f, Slope * RadiusX * (1.0f - EllipticalRadius));
                if (Z <= SurfaceHeight)
                {
                    Particles.Add({ FVector4f(X, Y, Z, 1.0f) });
                }
            }
        }
    }
    return Particles;
}

void AddDensityPass(
    FRDGBuilder& GraphBuilder,
    const TArray<FSandDensityParticleData>& Particles,
    const FDensityFieldDescription& Field,
    FRDGBufferRef& OutDensityBuffer)
{
    FRDGBufferRef ParticleBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Sand.Surface.Particles"), Particles);
    OutDensityBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Field.NodeCount()),
        TEXT("Sand.Surface.DensityField"));
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutDensityBuffer), 0u);

    FSandSplatDensityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSandSplatDensityCS::FParameters>();
    Parameters->DensityParticleCount = Particles.Num();
    Parameters->DensityGridSizeX = Field.Resolution.X;
    Parameters->DensityGridSizeY = Field.Resolution.Y;
    Parameters->DensityGridSizeZ = Field.Resolution.Z;
    Parameters->DensityVoxelSizeMeters = Field.VoxelSizeMeters;
    Parameters->DensityKernelRadiusMeters = Field.KernelRadiusMeters;
    Parameters->DensityFixedPointScale = FixedPointScale;
    Parameters->DensityGridMinimumMeters = Field.MinimumMeters;
    Parameters->DensityParticles = GraphBuilder.CreateSRV(ParticleBuffer);
    Parameters->DensityFieldFixedPoint = GraphBuilder.CreateUAV(OutDensityBuffer);

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("Sand.Surface.SplatDensity"),
        TShaderMapRef<FSandSplatDensityCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
        Parameters,
        FComputeShaderUtils::GetGroupCount(Particles.Num(), 64));
}
}

ASandSurfacePreviewActor::ASandSurfacePreviewActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SurfaceMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SurfaceMesh"));
    SetRootComponent(SurfaceMesh);
    SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SurfaceMesh->SetCastShadow(true);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded())
    {
        SurfaceMesh->SetMaterial(0, VertexColorMaterial.Object);
    }
}

void ASandSurfacePreviewActor::BeginPlay()
{
    Super::BeginPlay();
    if (UMaterialInstanceDynamic* PreviewMaterial = SurfaceMesh->CreateDynamicMaterialInstance(0))
    {
        PreviewMaterial->SetVectorParameterValue(
            TEXT("Color"), FLinearColor::FromSRGBColor(FColor(140, 144, 151)));
        PreviewMaterial->SetVectorParameterValue(
            TEXT("DiffuseColor"), FLinearColor::FromSRGBColor(FColor(140, 144, 151)));
        PreviewMaterial->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
        PreviewMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.92f);
    }
    if (bBuildOnBeginPlay)
    {
        GeneratePreviewSurface();
    }
}

void ASandSurfacePreviewActor::GeneratePreviewSurface()
{
    using namespace Sand::Surface;

    const TArray<FSandDensityParticleData> PreviewParticles = MakePreviewPile(PreviewParticleSpacingMeters);
    TArray<FVector3f> Positions;
    Positions.Reserve(PreviewParticles.Num());
    for (const FSandDensityParticleData& Particle : PreviewParticles)
    {
        Positions.Add(FVector3f(Particle.PositionAndWeight));
    }
    GenerateSurfaceFromParticlePositions(
        MoveTemp(Positions),
        FVector3f(-0.85f, -0.65f, -0.10f),
        FVector3f(0.85f, 0.65f, 0.65f));
}

bool ASandSurfacePreviewActor::GenerateSurfaceFromParticlePositions(
    TArray<FVector3f> ParticlePositionsMeters,
    const FVector3f& MinimumMeters,
    const FVector3f& MaximumMeters)
{
    using namespace Sand::Surface;

    if (bSurfaceBuildInFlight || ParticlePositionsMeters.IsEmpty())
    {
        return false;
    }
    bSurfaceBuildInFlight = true;
    const uint32 ThisGeneration = ++BuildGeneration;
    const float VoxelSize = FMath::Clamp(VoxelSizeMeters, 0.0125f, 0.1f);
    FDensityFieldDescription Field;
    Field.MinimumMeters = MinimumMeters;
    Field.VoxelSizeMeters = VoxelSize;
    const FVector3f ExtentMeters = MaximumMeters - MinimumMeters;
    Field.Resolution = FIntVector(
        FMath::CeilToInt(ExtentMeters.X / VoxelSize) + 1,
        FMath::CeilToInt(ExtentMeters.Y / VoxelSize) + 1,
        FMath::CeilToInt(ExtentMeters.Z / VoxelSize) + 1);
    Field.KernelRadiusMeters = FMath::Max(KernelRadiusMeters, 1.25f * VoxelSize);
    Field.IsoDensity = IsoDensity;
    TArray<FSandDensityParticleData> Particles;
    Particles.Reserve(ParticlePositionsMeters.Num());
    for (const FVector3f& Position : ParticlePositionsMeters)
    {
        Particles.Add({ FVector4f(Position, ParticleDensityWeight) });
    }
    const int32 ParticleCount = Particles.Num();
    const double RequestStartSeconds = FPlatformTime::Seconds();
    TWeakObjectPtr<ASandSurfacePreviewActor> WeakThis(this);

    ENQUEUE_RENDER_COMMAND(SandBuildPreviewDensity)(
        [WeakThis, ThisGeneration, Particles = MoveTemp(Particles), ParticleCount, Field, RequestStartSeconds](
            FRHICommandListImmediate& RHICmdList) mutable
        {
            const double DensityStartSeconds = FPlatformTime::Seconds();
            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef DensityBuffer = nullptr;
            AddDensityPass(GraphBuilder, Particles, Field, DensityBuffer);

            TArray<uint32> FixedDensity;
            FixedDensity.SetNumUninitialized(Field.NodeCount());
            FDensityReadbackParameters* ReadbackParameters = GraphBuilder.AllocParameters<FDensityReadbackParameters>();
            ReadbackParameters->SourceBuffer = DensityBuffer;
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("Sand.Surface.DensityReadback"),
                ReadbackParameters,
                ERDGPassFlags::Readback,
                [&FixedDensity, ReadbackParameters](FRHICommandListImmediate& ReadbackCommandList)
                {
                    const uint32 NumBytes = FixedDensity.Num() * sizeof(uint32);
                    FRHIGPUBufferReadback Readback(TEXT("Sand.Surface.DensityReadback"));
                    Readback.EnqueueCopy(ReadbackCommandList, ReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                    ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                    const void* Source = Readback.Lock(NumBytes);
                    FMemory::Memcpy(FixedDensity.GetData(), Source, NumBytes);
                    Readback.Unlock();
                });
            GraphBuilder.Execute();
            const double DensitySeconds = FPlatformTime::Seconds() - DensityStartSeconds;

            Async(EAsyncExecution::ThreadPool,
                [WeakThis, ThisGeneration, FixedDensity = MoveTemp(FixedDensity), ParticleCount, Field,
                    RequestStartSeconds, DensitySeconds]() mutable
                {
                    const double MeshStartSeconds = FPlatformTime::Seconds();
                    FSurfaceMeshData Mesh = BuildSurfaceMesh(FixedDensity, Field);
                    const double MeshSeconds = FPlatformTime::Seconds() - MeshStartSeconds;
                    AsyncTask(ENamedThreads::GameThread,
                        [WeakThis, ThisGeneration, Mesh = MoveTemp(Mesh), ParticleCount, Field,
                            RequestStartSeconds, DensitySeconds, MeshSeconds]() mutable
                        {
                            if (!WeakThis.IsValid() || WeakThis->BuildGeneration != ThisGeneration)
                            {
                                return;
                            }
                            WeakThis->bSurfaceBuildInFlight = false;
                            WeakThis->SurfaceMesh->CreateMeshSection_LinearColor(
                                0,
                                Mesh.Vertices,
                                Mesh.Indices,
                                Mesh.Normals,
                                Mesh.UVs,
                                Mesh.Colors,
                                TArray<FProcMeshTangent>(),
                                false,
                                false);
                            WeakThis->OnSurfaceMeshUpdated(Mesh.Vertices, Mesh.Indices);
                            const double TotalSeconds = FPlatformTime::Seconds() - RequestStartSeconds;
                            WeakThis->LastSurfaceBuildMilliseconds =
                                static_cast<float>(TotalSeconds * 1000.0);
                            const double DensityMegabytes =
                                static_cast<double>(Field.NodeCount() * sizeof(uint32)) / (1024.0 * 1024.0);
                            const double MeshMegabytes = static_cast<double>(
                                Mesh.Vertices.GetAllocatedSize() + Mesh.Indices.GetAllocatedSize() +
                                Mesh.Normals.GetAllocatedSize() + Mesh.UVs.GetAllocatedSize() +
                                Mesh.Colors.GetAllocatedSize()) / (1024.0 * 1024.0);
                            ++WeakThis->CompletedBuildCount;
                            if (WeakThis->CompletedBuildCount == 1 || WeakThis->CompletedBuildCount % 60 == 0)
                            {
                                UE_LOG(LogTemp, Display,
                                    TEXT("Sand surface frame %llu: %d particles, %d nodes (%.2f MB), %d geometric/%d raster triangles, mesh %.2f MB; density %.1f ms, mesh %.1f ms, total %.1f ms"),
                                    WeakThis->CompletedBuildCount,
                                    ParticleCount,
                                    Field.NodeCount(),
                                    DensityMegabytes,
                                    Mesh.Indices.Num() / 3,
                                    Mesh.Indices.Num() / 3,
                                    MeshMegabytes,
                                    DensitySeconds * 1000.0,
                                    MeshSeconds * 1000.0,
                                    TotalSeconds * 1000.0);
                            }

                            const bool bCaptureSequence =
                                FParse::Param(FCommandLine::Get(), TEXT("SandCaptureSequence"));
                            const bool bCaptureSingle =
                                FParse::Param(FCommandLine::Get(), TEXT("SandCaptureSurfacePreview"));
                            if (WeakThis->bAllowAutomaticCapture && !WeakThis->bCaptureScheduled && (bCaptureSequence || bCaptureSingle))
                            {
                                WeakThis->bCaptureScheduled = true;
                                if (bCaptureSequence)
                                {
                                    // Space captures around meaningful phases instead of
                                    // requesting screenshots faster than the async image
                                    // writer can finish. The previous 0.25 s cadence dropped
                                    // most requests and never reached the completed dump.
                                    constexpr int32 CaptureFrameCount = 8;
                                    static constexpr float CaptureTimesSeconds[CaptureFrameCount] =
                                    {
                                        0.2f, 1.5f, 3.0f, 4.3f,
                                        5.3f, 6.5f, 9.5f, 12.2f
                                    };
                                    constexpr float CaptureDurationSeconds = 13.2f;
                                    const FString SequenceDirectory = FPaths::Combine(
                                        FPaths::ProjectDir(), TEXT("Artifacts/ExcavationSequence"));
                                    IFileManager::Get().MakeDirectory(*SequenceDirectory, true);
                                    TSharedRef<int32, ESPMode::ThreadSafe> FrameIndex =
                                        MakeShared<int32, ESPMode::ThreadSafe>(0);
                                    TSharedRef<double, ESPMode::ThreadSafe> CaptureStartSeconds =
                                        MakeShared<double, ESPMode::ThreadSafe>(FPlatformTime::Seconds());
                                    FTSTicker::GetCoreTicker().AddTicker(
                                        FTickerDelegate::CreateLambda(
                                            [SequenceDirectory, FrameIndex, CaptureStartSeconds](const float)
                                            {
                                                const double CaptureElapsedSeconds =
                                                    FPlatformTime::Seconds() - *CaptureStartSeconds;
                                                if (CaptureElapsedSeconds < CaptureTimesSeconds[*FrameIndex])
                                                {
                                                    return true;
                                                }
                                                const FString FramePath = FPaths::Combine(
                                                    SequenceDirectory,
                                                    FString::Printf(TEXT("frame_%03d.png"), *FrameIndex));
                                                FScreenshotRequest::RequestScreenshot(FramePath, true, false);
                                                ++*FrameIndex;
                                                return *FrameIndex < CaptureFrameCount;
                                            }),
                                        0.1f);
                                    FTSTicker::GetCoreTicker().AddTicker(
                                        FTickerDelegate::CreateLambda(
                                            [](float)
                                            {
                                                FPlatformMisc::RequestExit(false);
                                                return false;
                                            }),
                                        CaptureDurationSeconds + 1.5f);
                                }
                                else
                                {
                                    float CaptureDelaySeconds = 0.5f;
                                    FParse::Value(
                                        FCommandLine::Get(),
                                        TEXT("SandCaptureDelaySeconds="),
                                        CaptureDelaySeconds);
                                    CaptureDelaySeconds = FMath::Clamp(CaptureDelaySeconds, 0.1f, 30.0f);
                                    const FString ScreenshotPath = FPaths::Combine(
                                        FPaths::ProjectDir(),
                                        TEXT("Artifacts/SandSurfaceUEPreview.png"));
                                    FTSTicker::GetCoreTicker().AddTicker(
                                        FTickerDelegate::CreateLambda(
                                            [ScreenshotPath](float)
                                            {
                                                FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
                                                return false;
                                            }),
                                        CaptureDelaySeconds);
                                    FTSTicker::GetCoreTicker().AddTicker(
                                        FTickerDelegate::CreateLambda(
                                            [](float)
                                            {
                                                FPlatformMisc::RequestExit(false);
                                                return false;
                                            }),
                                        CaptureDelaySeconds + 1.5f);
                                }
                            }
                        });
                });
        });
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandSurfaceReconstructionTest,
    "SandSimulation.GPU.SurfaceReconstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandSurfaceReconstructionTest::RunTest(const FString& Parameters)
{
    using namespace Sand::Surface;

    FDensityFieldDescription Field;
    Field.MinimumMeters = FVector3f(-0.4f, -0.4f, -0.1f);
    Field.Resolution = FIntVector(33, 33, 25);
    Field.VoxelSizeMeters = 0.025f;
    Field.KernelRadiusMeters = 0.055f;
    Field.IsoDensity = 0.85f;
    TArray<FSandDensityParticleData> Particles = MakePreviewPile(0.05f);
    TArray<uint32> FixedDensity;
    FixedDensity.SetNumZeroed(Field.NodeCount());
    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    ENQUEUE_RENDER_COMMAND(SandSurfaceReconstructionTest)(
        [CompletionEvent, Particles, Field, &FixedDensity](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef DensityBuffer = nullptr;
            AddDensityPass(GraphBuilder, Particles, Field, DensityBuffer);
            FDensityReadbackParameters* ReadbackParameters = GraphBuilder.AllocParameters<FDensityReadbackParameters>();
            ReadbackParameters->SourceBuffer = DensityBuffer;
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("Sand.Surface.TestReadback"),
                ReadbackParameters,
                ERDGPassFlags::Readback,
                [&FixedDensity, ReadbackParameters](FRHICommandListImmediate& ReadbackCommandList)
                {
                    const uint32 NumBytes = FixedDensity.Num() * sizeof(uint32);
                    FRHIGPUBufferReadback Readback(TEXT("Sand.Surface.TestReadback"));
                    Readback.EnqueueCopy(ReadbackCommandList, ReadbackParameters->SourceBuffer->GetRHI(), NumBytes);
                    ReadbackCommandList.SubmitAndBlockUntilGPUIdle();
                    const void* Source = Readback.Lock(NumBytes);
                    FMemory::Memcpy(FixedDensity.GetData(), Source, NumBytes);
                    Readback.Unlock();
                });
            GraphBuilder.Execute();
            CompletionEvent->Trigger();
        });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    const FSurfaceMeshData Mesh = BuildSurfaceMesh(FixedDensity, Field);

    bool bAllVerticesFinite = true;
    for (const FVector& Vertex : Mesh.Vertices)
    {
        bAllVerticesFinite &= !Vertex.ContainsNaN();
    }
    const bool bAllIndicesValid = Mesh.Indices.ContainsByPredicate(
        [&Mesh](const int32 Index) { return Index < 0 || Index >= Mesh.Vertices.Num(); }) == false;
    TestTrue(TEXT("GPU density field contains occupied nodes"),
        FixedDensity.ContainsByPredicate([](const uint32 Density) { return Density > 0; }));
    TestTrue(TEXT("Density isosurface produces a non-trivial continuous mesh"), Mesh.Indices.Num() >= 300);
    TestEqual(TEXT("Surface index buffer contains complete triangles"), Mesh.Indices.Num() % 3, 0);
    TestTrue(TEXT("Marching cubes reuses shared surface vertices"), Mesh.Vertices.Num() < Mesh.Indices.Num());
    TestTrue(TEXT("Every surface index addresses a generated vertex"), bAllIndicesValid);
    TestTrue(TEXT("Extracted surface vertices remain finite"), bAllVerticesFinite);
    return true;
}

#endif
