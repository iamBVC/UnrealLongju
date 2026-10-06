param([string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$taskConfig = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'Config/DefaultGame.ini'))
$taskSection = [regex]::Match($taskConfig, '(?ms)^\[/Script/Metin2\.MT2PathSettings\]\s*\r?\n(.*?)(?=^\[|\z)').Groups[1].Value
$taskEntries = @{}
$taskErrors = [Collections.Generic.List[string]]::new()
foreach ($taskMatch in [regex]::Matches($taskSection, '(?m)^\+(Assets|Locations)=\(Key="([^"]+)",Value="([^"]*)"\)\s*$')) {
    $taskKey = $taskMatch.Groups[2].Value
    if ($taskEntries.ContainsKey($taskKey)) { $taskErrors.Add("Duplicate catalog key: $taskKey") }
    $taskEntries[$taskKey] = $taskMatch.Groups[3].Value
}
if ($taskEntries.Count -eq 0) { throw 'Path catalog is missing or empty.' }

# These are mount/legacy-format identifiers, not configurable asset locations.
# Changing them would change Unreal external-actor or legacy resource parsing semantics.
$taskGrammar = @('/Game/', '/Game', '/Engine/', '/Game/__ExternalActors__/', 'd:/', 'd:/ymir work/')
$taskRoots = @((Join-Path $ProjectRoot 'Source'), (Join-Path $ProjectRoot 'Plugins/MT2UE/Source'))
$taskFiles = & rg --files @taskRoots -g '*.cpp' -g '*.h' -g '!**/ThirdParty/**'
if ($LASTEXITCODE -ne 0) { throw 'rg could not enumerate project source.' }
$taskUses = 0
foreach ($taskFile in $taskFiles) {
    $taskText = [IO.File]::ReadAllText($taskFile)
    foreach ($taskMatch in [regex]::Matches($taskText, 'UMT2PathSettings::(?:Path|Format)\(TEXT\("([^"]+)"\)')) {
        $taskKey = $taskMatch.Groups[1].Value
        if (-not $taskEntries.ContainsKey($taskKey)) { $taskErrors.Add("$taskFile references missing catalog key: $taskKey") }
        $taskUses++
    }
    # Deterministic test fixtures and protocol identifiers are not relocation defaults.
    if ($taskFile -match '[\\/]Tests[\\/]|Tests\.cpp$') { continue }
    foreach ($taskMatch in [regex]::Matches($taskText, 'TEXT\("((?:/Game(?:/|$)|/Engine(?:/|$)|[A-Za-z]:[/\\])[^"\r\n]*)"\)')) {
        $taskValue = $taskMatch.Groups[1].Value
        if ($taskValue -notin $taskGrammar) { $taskErrors.Add("Hardcoded path in ${taskFile}: $taskValue") }
    }
    foreach ($taskMatch in [regex]::Matches($taskText, ' / TEXT\("([^"\r\n]+)"\)')) {
        $taskValue = $taskMatch.Groups[1].Value
        if ($taskValue -notin @('*', 'MT2UE', '../..')) { $taskErrors.Add("Hardcoded joined path in ${taskFile}: $taskValue") }
    }
}
foreach ($taskKey in $taskEntries.Keys) {
    foreach ($taskMatch in [regex]::Matches($taskEntries[$taskKey], '\{([^}]+)\}')) {
        $taskReference = $taskMatch.Groups[1].Value
        if ($taskReference -notin @('ProjectDir', 'ContentDir', 'SavedDir') -and -not $taskEntries.ContainsKey($taskReference)) {
            $taskErrors.Add("$taskKey references missing location: $taskReference")
        }
    }
}
if ($taskErrors.Count -gt 0) {
    $taskErrors | ForEach-Object { Write-Output $_ }
    throw "Configured-path audit failed with $($taskErrors.Count) issue(s)."
}
Write-Output "Configured-path audit passed: $($taskEntries.Count) catalog entries, $taskUses call sites, $($taskFiles.Count) source files."
Write-Output 'Scope: first-party runtime/editor C++; excludes includes, third-party code, deterministic test fixtures and format/mount identifiers.'
