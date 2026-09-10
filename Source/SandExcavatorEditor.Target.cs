using UnrealBuildTool;
using System.Collections.Generic;

public class SandExcavatorEditorTarget : TargetRules
{
    public SandExcavatorEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("SandExcavator");
    }
}

