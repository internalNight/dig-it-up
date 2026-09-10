using UnrealBuildTool;
using System.Collections.Generic;

public class SandExcavatorTarget : TargetRules
{
    public SandExcavatorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("SandExcavator");
    }
}

