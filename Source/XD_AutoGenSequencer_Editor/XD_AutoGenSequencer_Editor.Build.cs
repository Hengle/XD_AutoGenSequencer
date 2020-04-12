// Some copyright should be here...

using UnrealBuildTool;

public class XD_AutoGenSequencer_Editor : ModuleRules
{
	public XD_AutoGenSequencer_Editor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                "UnrealEd",
                "AssetTools",
                "Kismet",
				"GraphEditor",
				"AdvancedPreviewScene",
                "EditorStyle",
				"InputCore",

				"Sequencer",
                "MovieScene",
                "LevelSequence",
                "MovieSceneTools",
                "MovieSceneTracks",
                "CinematicCamera",

				"XD_AutoGenSequencer",
				// ... add private dependencies that you statically link with here ...
			}
			);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
