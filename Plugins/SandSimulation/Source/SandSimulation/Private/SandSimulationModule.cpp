#include "SandSimulationModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void FSandSimulationModule::StartupModule()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SandSimulation"));
    check(Plugin.IsValid());

    const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/SandSimulation"), ShaderDirectory);
}

void FSandSimulationModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FSandSimulationModule, SandSimulation)

