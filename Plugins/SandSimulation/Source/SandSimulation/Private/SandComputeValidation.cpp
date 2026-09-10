#include "GlobalShader.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"

namespace Sand::Validation
{
constexpr uint32 ElementCount = 4;
constexpr uint32 Seed = 41;

class FSandValidationCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FSandValidationCS);
    SHADER_USE_PARAMETER_STRUCT(FSandValidationCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, Seed)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, Output)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(
    FSandValidationCS,
    "/SandSimulation/Private/SandValidation.usf",
    "MainCS",
    SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackParameters, )
    RDG_BUFFER_ACCESS(SourceBuffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSandGPUValidationTest,
    "SandSimulation.GPU.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSandGPUValidationTest::RunTest(const FString& Parameters)
{
    using namespace Sand::Validation;

    TArray<uint32> Results;
    Results.SetNumZeroed(ElementCount);

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    ENQUEUE_RENDER_COMMAND(SandGPUValidation)(
        [CompletionEvent, &Results](FRHICommandListImmediate& RHICmdList)
        {
            if (RHICmdList.GetPipeline() == ERHIPipeline::None)
            {
                RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), ElementCount),
                TEXT("Sand.Validation.Output"));

            FSandValidationCS::FParameters* ComputeParameters =
                GraphBuilder.AllocParameters<FSandValidationCS::FParameters>();
            ComputeParameters->Seed = Seed;
            ComputeParameters->Output = GraphBuilder.CreateUAV(OutputBuffer);

            const TShaderMapRef<FSandValidationCS> ComputeShader(
                GetGlobalShaderMap(GMaxRHIFeatureLevel));

            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("Sand.Validation.Dispatch"),
                ComputeShader,
                ComputeParameters,
                FIntVector(1, 1, 1));

            FReadbackParameters* ReadbackParameters =
                GraphBuilder.AllocParameters<FReadbackParameters>();
            ReadbackParameters->SourceBuffer = OutputBuffer;

            GraphBuilder.AddPass(
                RDG_EVENT_NAME("Sand.Validation.Readback"),
                ReadbackParameters,
                ERDGPassFlags::Readback,
                [&Results, ReadbackParameters](FRHICommandListImmediate& ReadbackCommandList)
                {
                    constexpr uint32 NumBytes = ElementCount * sizeof(uint32);
                    FRHIGPUBufferReadback Readback(TEXT("Sand.Validation.Readback"));
                    Readback.EnqueueCopy(
                        ReadbackCommandList,
                        ReadbackParameters->SourceBuffer->GetRHI(),
                        NumBytes);
                    ReadbackCommandList.SubmitAndBlockUntilGPUIdle();

                    const void* SourceData = Readback.Lock(NumBytes);
                    FMemory::Memcpy(Results.GetData(), SourceData, NumBytes);
                    Readback.Unlock();
                });

            GraphBuilder.Execute();
            CompletionEvent->Trigger();
        });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    for (uint32 Index = 0; Index < ElementCount; ++Index)
    {
        const uint32 Expected = Seed + Index * 17u;
        TestEqual(FString::Printf(TEXT("Output[%u]"), Index), Results[Index], Expected);
    }

    return true;
}

#endif

