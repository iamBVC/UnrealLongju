<#
    Writes manifest.json describing a staged client build, for the patcher to consume.

    Params:
      -StageDir   Path to the staged WindowsClient folder (contains UnrealLongjuClient.exe, Engine\, UnrealLongju\).
      -BaseUrl    Public URL the client files are hosted under (files are appended to this).
      -Version    Build version string (e.g. a timestamp).
      -LaunchExe  Executable the patcher runs to start the game (relative to StageDir).
      -LaunchArgs Arguments passed to it (typically the gateway address).

    The manifest is written to <StageDir>\manifest.json and lists every game file with its size and
    lowercase SHA-256 hash. manifest.json and UnrealLongjuPatcher.exe are excluded because a running
    patcher cannot safely replace itself.
#>
param(
    [Parameter(Mandatory = $true)] [string] $StageDir,
    [Parameter(Mandatory = $true)] [string] $BaseUrl,
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [string] $LaunchExe,
    [Parameter(Mandatory = $true)] [string] $LaunchArgs
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $StageDir)) {
    Write-Error "Stage directory not found: $StageDir"
    exit 1
}

$root = (Resolve-Path -LiteralPath $StageDir).Path.TrimEnd('\')
$rootLen = $root.Length + 1

function Get-Sha256Hex([string] $Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        }
        finally {
            $sha256.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

$files = Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Name -ne 'manifest.json' -and
    $_.Name -ne 'UnrealLongjuPatcher.exe' -and
    $_.Name -ne 'Metin2Patcher.exe'
}

$entries = foreach ($f in $files) {
    $rel = $f.FullName.Substring($rootLen).Replace('\', '/')
    $hash = Get-Sha256Hex $f.FullName
    [ordered]@{
        path   = $rel
        size   = $f.Length
        sha256 = $hash
    }
}

$manifest = [ordered]@{
    version = $Version
    baseUrl = $BaseUrl
    launch  = [ordered]@{ exe = $LaunchExe; args = $LaunchArgs }
    files   = @($entries)
}

$outPath = Join-Path $root 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Out-File -LiteralPath $outPath -Encoding utf8

Write-Host "[GenerateManifest] Wrote $outPath  (version $Version, $($entries.Count) files)"
