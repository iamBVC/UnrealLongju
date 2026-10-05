/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using System.Security.Cryptography;
using System.Text.Json;

namespace UnrealLongjuPatcher;

/// <summary>Progress snapshot pushed to the UI during work.</summary>
internal readonly record struct PatchProgress(string Status, double TotalFraction);

/// <summary>
/// Downloads the manifest, works out which local files are missing/outdated (by size + SHA-256),
/// and downloads only those, verifying each one before swapping it into place.
/// </summary>
internal sealed class Updater
{
    private readonly HttpClient _http;
    private readonly string _installDir;
    private readonly string _patcherPath;

    public Updater(string installDir)
    {
        _installDir = Path.GetFullPath(installDir).TrimEnd(Path.DirectorySeparatorChar)
            + Path.DirectorySeparatorChar;
        _patcherPath = Path.GetFullPath(Environment.ProcessPath
            ?? Path.Combine(_installDir, "UnrealLongjuPatcher.exe"));
        _http = new HttpClient { Timeout = TimeSpan.FromMinutes(60) };
        _http.DefaultRequestHeaders.UserAgent.ParseAdd("UnrealLongjuPatcher/1.0");
    }

    public async Task<Manifest> FetchManifestAsync(CancellationToken ct)
    {
        // Cache-bust so a CDN/browser cache never serves a stale manifest.
        string url = PatchConfig.ManifestUrl + (PatchConfig.ManifestUrl.Contains('?') ? "&" : "?")
            + "t=" + DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        string json = await _http.GetStringAsync(url, ct);
        var manifest = JsonSerializer.Deserialize<Manifest>(json)
            ?? throw new InvalidDataException("Manifest could not be parsed.");
        return manifest;
    }

    /// <summary>Returns the files whose local copy is missing, wrong size, or wrong hash.</summary>
    public List<FileEntry> GetOutdatedFiles(Manifest manifest, IProgress<PatchProgress> progress, CancellationToken ct)
    {
        var outdated = new List<FileEntry>();
        for (int i = 0; i < manifest.Files.Count; i++)
        {
            ct.ThrowIfCancellationRequested();
            FileEntry entry = manifest.Files[i];
            if (IsPatcherExecutable(entry)) continue;
            progress.Report(new PatchProgress($"Verifying files… ({i + 1}/{manifest.Files.Count})",
                manifest.Files.Count > 0 ? (double)(i + 1) / manifest.Files.Count : 1.0));

            string local = ToLocalPath(entry.Path);
            if (!File.Exists(local))
            {
                outdated.Add(entry);
                continue;
            }
            var info = new FileInfo(local);
            if (info.Length != entry.Size ||
                !string.Equals(ComputeSha256(local), entry.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                outdated.Add(entry);
            }
        }
        return outdated;
    }

    public async Task DownloadAsync(Manifest manifest, List<FileEntry> files,
        IProgress<PatchProgress> progress, CancellationToken ct)
    {
        string baseUrl = ResolveBaseUrl(manifest);
        long totalBytes = 0;
        foreach (FileEntry f in files) totalBytes += f.Size;
        long doneBytes = 0;

        foreach (FileEntry entry in files)
        {
            ct.ThrowIfCancellationRequested();
            if (IsPatcherExecutable(entry)) continue;
            string url = CombineUrl(baseUrl, entry.Path);
            string local = ToLocalPath(entry.Path);
            Directory.CreateDirectory(Path.GetDirectoryName(local)!);
            string tempPath = local + ".part";

            using (var response = await _http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, ct))
            {
                response.EnsureSuccessStatusCode();
                await using Stream source = await response.Content.ReadAsStreamAsync(ct);
                await using FileStream dest = File.Create(tempPath);
                byte[] buffer = new byte[1 << 16];
                int read;
                long fileDone = 0;
                while ((read = await source.ReadAsync(buffer, ct)) > 0)
                {
                    await dest.WriteAsync(buffer.AsMemory(0, read), ct);
                    fileDone += read;
                    doneBytes += read;
                    progress.Report(new PatchProgress(
                        $"Downloading {Path.GetFileName(entry.Path)}  ({FormatSize(doneBytes)} / {FormatSize(totalBytes)})",
                        totalBytes > 0 ? (double)doneBytes / totalBytes : 1.0));
                }
            }

            // Verify before committing; a corrupt/tampered download never overwrites the good file.
            if (!string.Equals(ComputeSha256(tempPath), entry.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                File.Delete(tempPath);
                throw new InvalidDataException($"Downloaded file failed hash check: {entry.Path}");
            }
            if (File.Exists(local)) File.Delete(local);
            File.Move(tempPath, local);
        }
    }

    public LaunchInfo ResolveLaunch(Manifest manifest) => manifest.Launch ?? new LaunchInfo
    {
        Exe = PatchConfig.DefaultLaunchExe,
        Args = PatchConfig.DefaultLaunchArgs
    };

    public string InstallDir => _installDir;

    public string ResolveInstallPath(string relative) => ToLocalPath(relative);

    private string ToLocalPath(string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative))
            throw new InvalidDataException($"Invalid manifest path: {relative}");

        string fullPath = Path.GetFullPath(Path.Combine(
            _installDir, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(_installDir, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Manifest path leaves the client directory: {relative}");
        return fullPath;
    }

    private bool IsPatcherExecutable(FileEntry entry) =>
        string.Equals(ToLocalPath(entry.Path), _patcherPath, StringComparison.OrdinalIgnoreCase);

    private static string ResolveBaseUrl(Manifest manifest)
    {
        if (!string.IsNullOrWhiteSpace(manifest.BaseUrl)) return manifest.BaseUrl!;
        // Fall back to the manifest's own directory.
        int slash = PatchConfig.ManifestUrl.LastIndexOf('/');
        return slash > 0 ? PatchConfig.ManifestUrl[..(slash + 1)] : PatchConfig.ManifestUrl;
    }

    private static string CombineUrl(string baseUrl, string path)
    {
        if (!baseUrl.EndsWith('/')) baseUrl += "/";
        return baseUrl + path.TrimStart('/');
    }

    private static string ComputeSha256(string path)
    {
        using var sha = SHA256.Create();
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(sha.ComputeHash(stream)).ToLowerInvariant();
    }

    private static string FormatSize(long bytes)
    {
        string[] units = { "B", "KB", "MB", "GB" };
        double size = bytes;
        int unit = 0;
        while (size >= 1024 && unit < units.Length - 1) { size /= 1024; unit++; }
        return $"{size:0.#} {units[unit]}";
    }
}
