using UnrealBuildTool;

public class SandSimulation : ModuleRules
{
    public SandSimulation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GeometryCore",
            "InputCore",
            "ProceduralMeshComponent"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Projects",
            "RenderCore",
            "Renderer",
            "SlateCore",
            "RHI"
        });
    }
}
