param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
$IniPath = Join-Path $ProjectRoot 'Config\DefaultGame.ini'
$SectionName = '[/Script/EngineSettings.GeneralProjectSettings]'

if (-not (Test-Path -LiteralPath $IniPath)) {
    throw "Project settings not found: $IniPath"
}

$Lines = [System.Collections.Generic.List[string]]::new()
$Lines.AddRange([string[]](Get-Content -LiteralPath $IniPath))
$SectionIndex = $Lines.IndexOf($SectionName)
if ($SectionIndex -lt 0) {
    throw "Missing $SectionName in $IniPath"
}

$VersionIndex = -1
for ($Index = $SectionIndex + 1; $Index -lt $Lines.Count; ++$Index) {
    if ($Lines[$Index].StartsWith('[')) {
        break
    }
    if ($Lines[$Index] -match '^ProjectVersion=(\d+)$') {
        if ($VersionIndex -ge 0) {
            throw "Duplicate ProjectVersion in $SectionName"
        }
        $VersionIndex = $Index
    }
}

$CurrentVersion = 0
if ($VersionIndex -ge 0) {
    if (-not [int]::TryParse($Matches[1], [ref]$CurrentVersion) -or $CurrentVersion -lt 0) {
        throw 'ProjectVersion must be a non-negative 32-bit integer.'
    }
}
if ($CurrentVersion -ge [int]::MaxValue) {
    throw 'ProjectVersion cannot be incremented further.'
}

$NewVersion = $CurrentVersion + 1
if ($VersionIndex -ge 0) {
    $Lines[$VersionIndex] = "ProjectVersion=$NewVersion"
}
else {
    $Lines.Insert($SectionIndex + 1, "ProjectVersion=$NewVersion")
}

[System.IO.File]::WriteAllLines(
    $IniPath, $Lines, [System.Text.UTF8Encoding]::new($false))
Write-Output $NewVersion
