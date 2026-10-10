// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AICompanionDemo : ModuleRules
{
	public AICompanionDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Json",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"Landscape",
			"AudioMixer",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"AICompanionDemo",
			"AICompanionDemo/Variant_Platforming",
			"AICompanionDemo/Variant_Platforming/Animation",
			"AICompanionDemo/Variant_Combat",
			"AICompanionDemo/Variant_Combat/AI",
			"AICompanionDemo/Variant_Combat/Animation",
			"AICompanionDemo/Variant_Combat/Gameplay",
			"AICompanionDemo/Variant_Combat/Interfaces",
			"AICompanionDemo/Variant_Combat/UI",
			"AICompanionDemo/Variant_SideScrolling",
			"AICompanionDemo/Variant_SideScrolling/AI",
			"AICompanionDemo/Variant_SideScrolling/Gameplay",
			"AICompanionDemo/Variant_SideScrolling/Interfaces",
			"AICompanionDemo/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
