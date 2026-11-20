using System.IO;
using UnrealBuildTool;

public class CkTestsEditor : CkModuleRules
{
    public CkTestsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "EditorStyle",
            "EditorFramework",
            "UnrealEd",
            "EditorSubsystem",
            "ToolMenus",
            "AssetTools",
            "AssetRegistry",
            "Projects",
            "DeveloperSettings",
            "FunctionalTesting",
            "MessageLog",
            "SourceControl",
            "Landscape",
            "NavigationSystem",

            "CkCore",
            "CkEcs",
            "CkEntitySpawner",
            "CkGroundNav",
            "CkLog",
            "CkPathNetwork",
            "CkShapes",
            "CkTests",
        });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AngelscriptCode",
            "AssetManagerEditor",
            "CkSnapshot",
            "BlueprintGraph",
            "CkGrid",
            "CkGridEditor",
            "CkEcsExt",
            "CkJolt",
            "CkPixelSprite",
            "CkProceduralAnimation",
            "CkProceduralAnimationEditor",
            "CkInsightsAnalyzer",
            "CkResourceLoader",
            "CkRuntimeMesh",
            "CkUsf",
            "CkUsfEditor",
            "DataValidation",
            "GeometryFramework",
            "GameplayTags",
            "InputCore",
            "IrisCore",
            "MaterialEditor",
            "MeshDescription",
            "RHI",
            "RenderCore",
            "StaticMeshDescription",
            "TraceServices",
        });
    }
}
