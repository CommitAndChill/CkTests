using UnrealBuildTool;

public class ProceduralActionsSmokeTarget : TargetRules
{
    public ProceduralActionsSmokeTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        BuildEnvironment = TargetBuildEnvironment.Unique;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        bWithPushModel = true;
        ExtraModuleNames.Add("ProceduralActionsSmoke");
    }
}
