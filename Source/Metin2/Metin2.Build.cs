/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using UnrealBuildTool;

public class Metin2 : ModuleRules
{
	public Metin2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Json",
			"ImageWrapper",
			"ImageCore",
			"SQLiteCore",
			"Networking",
			"Sockets",
			"Voice",
			"AudioMixer",
			"AudioCapture",
			"AudioCaptureCore",
			"AIModule",
			"NavigationSystem",
			"NetCore",
			"ReplicationGraph",
			"Slate",
			"SlateCore",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new[] { "AssetRegistry", "SSL" });
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Comdlg32.lib");
		}
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

	}
}
