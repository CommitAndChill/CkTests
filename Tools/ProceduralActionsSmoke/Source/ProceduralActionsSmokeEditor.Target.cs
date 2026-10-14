using UnrealBuildTool;

public class ProceduralActionsSmokeEditorTarget : TargetRules
{
    public ProceduralActionsSmokeEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        BuildEnvironment = TargetBuildEnvironment.Unique;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        bWithPushModel = true;
        ExtraModuleNames.Add("ProceduralActionsSmoke");
    }
}
