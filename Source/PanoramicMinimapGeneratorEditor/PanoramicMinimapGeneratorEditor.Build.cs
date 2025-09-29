using UnrealBuildTool;

public class PanoramicMinimapGeneratorEditor : ModuleRules
{
    public PanoramicMinimapGeneratorEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "Projects",
                "UnrealEd",      // Required for Editor functionality
                "LevelEditor",   // Required to add items to the main menu
                "ToolMenus",
                "ImageWrapper",
                "PanoramicMinimapGeneratorRuntime",
                "RHI",
                "RenderCore"
            }
        );
    }
}