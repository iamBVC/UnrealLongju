/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using System.Text.Json.Serialization;

namespace UnrealLongjuPatcher;

/// <summary>
/// The JSON document hosted on the website (see GenerateManifest.ps1). It fully describes one client
/// build: a version string, where to download files from, what to launch, and every distributable
/// file with its size and SHA-256 hash.
/// </summary>
internal sealed class Manifest
{
    [JsonPropertyName("version")] public string Version { get; set; } = "";

    /// <summary>Base URL that file paths are appended to. If null, derived from the manifest URL.</summary>
    [JsonPropertyName("baseUrl")] public string? BaseUrl { get; set; }

    [JsonPropertyName("launch")] public LaunchInfo? Launch { get; set; }

    [JsonPropertyName("files")] public List<FileEntry> Files { get; set; } = new();
}

internal sealed class LaunchInfo
{
    [JsonPropertyName("exe")] public string Exe { get; set; } = "";
    [JsonPropertyName("args")] public string Args { get; set; } = "";
}

internal sealed class FileEntry
{
    /// <summary>Path relative to the install root, using forward slashes.</summary>
    [JsonPropertyName("path")] public string Path { get; set; } = "";
    [JsonPropertyName("size")] public long Size { get; set; }
    [JsonPropertyName("sha256")] public string Sha256 { get; set; } = "";
}
