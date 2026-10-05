/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

namespace UnrealLongjuPatcher;

/// <summary>
/// Compile-time defaults. The only value the patcher really needs baked in is where to fetch the
/// manifest from; everything else (download base URL, which exe to launch and with what arguments)
/// comes from the manifest itself, so you can change hosting or the gateway address server-side
/// without shipping a new patcher.
/// </summary>
internal static class PatchConfig
{
    /// <summary>URL of the manifest that describes the current client build.</summary>
    public const string ManifestUrl = "https://client.hosting.com/client/manifest.json";

    /// <summary>Inherited by the game process and matched against its private launch argument.</summary>
    public const string LaunchTokenEnvironmentVariable = "MT2UE_PATCHER_TOKEN";

    /// <summary>Local test scripts can replace manifest launch arguments without bypassing patching.</summary>
    public const string LaunchArgumentsOverrideEnvironmentVariable = "MT2UE_PATCHER_ARGS_OVERRIDE";

    /// <summary>Window title.</summary>
    public const string AppTitle = "UnrealLongju Patcher";

    /// <summary>Fallbacks used only if the manifest omits the launch block.</summary>
    public const string DefaultLaunchExe = "UnrealLongjuClient.exe";
    public const string DefaultLaunchArgs = "game.server.com:11000";
}
