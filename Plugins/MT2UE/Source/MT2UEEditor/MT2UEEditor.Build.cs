/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using UnrealBuildTool;
using System.IO;

public class MT2UEEditor : ModuleRules
{
	public MT2UEEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Metin2",
				"UnrealEd"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"AnimGraphRuntime",
				"AnimationDataController",
				"AudioEditor",
				"AssetRegistry",
				"AssetTools",
				"BlueprintGraph",
				"ContentBrowser",
				"Foliage",
				"ImageCore",
				"InputCore",
				"InterchangeEngine",
				"InterchangePipelines",
				"Landscape",
				"LevelEditor",
				"Projects",
				"SkeletalMeshUtilitiesCommon",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UMG",
				"UMGEditor"
			}
		);

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string PluginDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../"));
			string GrannyRoot = Path.Combine(PluginDir, "ThirdParty/Granny");
			string GrannyDll = Path.Combine(GrannyRoot, "bin/Win64/granny2_x64.dll");
			string RuntimeGrannyDll = Path.Combine(PluginDir, "Binaries/ThirdParty/Granny/Win64/granny2_x64.dll");

			PublicSystemIncludePaths.Add(Path.Combine(GrannyRoot, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(GrannyRoot, "lib/Win64/granny2_x64.lib"));
			PublicDelayLoadDLLs.Add("granny2_x64.dll");
			RuntimeDependencies.Add(RuntimeGrannyDll, GrannyDll);

			// Legacy SpeedTreeRT 1.6 is x86-only. The Win64 editor invokes this staged Win32
			// bridge out of process instead of loading the incompatible DLL into Unreal.
			RuntimeDependencies.Add(Path.Combine(PluginDir, "Binaries/ThirdParty/SpeedTree/Win32/SpeedTreeToObj.exe"));
			RuntimeDependencies.Add(Path.Combine(PluginDir, "Binaries/ThirdParty/SpeedTree/Win32/SpeedTreeRT.dll"));
		}
	}
}
