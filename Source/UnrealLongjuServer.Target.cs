/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedConfigurations(UnrealTargetConfiguration.Development, UnrealTargetConfiguration.Shipping)]
public class UnrealLongjuServerTarget : TargetRules
{
	public UnrealLongjuServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
		ExtraModuleNames.Add("Metin2");

		// Compile the engine modules uniquely for this target instead of linking the shared,
		// precompiled engine libraries. Those shared libs (in this source engine) link GoogleTest's
		// gmock_main() into programs, which steals the program entry point and makes the packaged
		// server run the engine unit-test suite instead of the game. A Unique build environment
		// rebuilds the engine without that test tooling - this is how the sibling project's dedicated
		// server works on the same engine - and it also lets us keep logging in Shipping servers.
		BuildEnvironment = TargetBuildEnvironment.Unique;
		bUseLoggingInShipping = true;
	}
}
