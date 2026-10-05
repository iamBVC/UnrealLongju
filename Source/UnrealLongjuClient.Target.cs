/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedConfigurations(UnrealTargetConfiguration.Development, UnrealTargetConfiguration.Shipping)]
public class UnrealLongjuClientTarget : TargetRules
{
	public UnrealLongjuClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
		ExtraModuleNames.Add("Metin2");

		// Same reason as the server: a Unique build environment recompiles the engine modules for
		// this target so the shared engine libs' GoogleTest gmock_main() can't hijack the packaged
		// client's entry point.
		BuildEnvironment = TargetBuildEnvironment.Unique;
		// Keep file logging available in Shipping so connection and map-transfer failures can be
		// diagnosed on distributed clients. Logs are written under the user's Saved/Logs folder.
		bUseLoggingInShipping = true;
	}
}
