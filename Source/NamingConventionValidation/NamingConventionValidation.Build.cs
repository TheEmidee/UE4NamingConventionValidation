namespace UnrealBuildTool.Rules
{
    public class NamingConventionValidation : ModuleRules
    {
        public NamingConventionValidation( ReadOnlyTargetRules Target )
            : base( Target )
        {
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            bEnforceIWYU = true;
            
            PrivateIncludePaths.Add("NamingConventionValidation/Private");

            PublicDependencyModuleNames.AddRange(
                new string[] { 
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "TargetPlatform",
                    "AssetRegistry"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Slate",
                    "SlateCore",
                    "UnrealEd",
                    "AssetRegistry",
                    /*"InputCore",
                    "Json",
                    "CollectionManager",
                    "ContentBrowser",
                    "WorkspaceMenuStructure",
                    "EditorStyle",
                    "AssetTools",
                    "PropertyEditor",
                    "GraphEditor",
                    "BlueprintGraph",
                    "KismetCompiler",
                    "SandboxFile"*/
                }
            );
        }
    }
}
