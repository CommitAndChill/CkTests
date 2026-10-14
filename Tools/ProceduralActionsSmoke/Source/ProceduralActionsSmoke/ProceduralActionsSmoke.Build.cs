using UnrealBuildTool;

public class ProceduralActionsSmoke : ModuleRules
{
    public ProceduralActionsSmoke(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "CkCore", "CkEcs", "CkEcsExt", "CkProceduralAnimation"
        });
        SetupIrisSupport(Target, true);
    }
}
