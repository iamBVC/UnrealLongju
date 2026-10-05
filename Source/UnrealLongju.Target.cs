/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealLongjuTarget : TargetRules
{
	public UnrealLongjuTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
		ExtraModuleNames.Add("Metin2");
	}
}
