param(
    [string[]]$Cultures,
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LocalizationRoot = Join-Path $ProjectRoot 'Content\Localization\Game'
$NativeCulture = 'en'

if (-not $Cultures -or $Cultures.Count -eq 0) {
    $Cultures = Get-ChildItem -LiteralPath $LocalizationRoot -Directory |
        Where-Object { $_.Name -ne $NativeCulture } |
        Select-Object -ExpandProperty Name
}

function Decode-PoString([string]$Value) {
    if ($Value -notmatch '^"(.*)"$') {
        return $Value
    }
    return [System.Text.RegularExpressions.Regex]::Unescape($Matches[1])
}

function Get-Tokens([string]$Text) {
    $Tokens = [System.Collections.Generic.List[string]]::new()
    foreach ($Match in [regex]::Matches($Text, '\{\d+(?::[^}]*)?\}')) {
        $Tokens.Add($Match.Value)
    }
    foreach ($Match in [regex]::Matches($Text, '%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[diuoxXfFeEgGaAcsp]')) {
        $Tokens.Add($Match.Value)
    }
    return @($Tokens | Sort-Object)
}

$HasErrors = $false
foreach ($Culture in $Cultures) {
    $PoPath = Join-Path $LocalizationRoot "$Culture\Game.po"
    if (-not (Test-Path -LiteralPath $PoPath)) {
        Write-Error "Missing translation file: $PoPath" -ErrorAction Continue
        $HasErrors = $true
        continue
    }

    $Entries = 0
    $Translated = 0
    $Missing = 0
    $TokenErrors = 0
    $CurrentId = $null
    $CurrentContext = ''

    foreach ($Line in Get-Content -LiteralPath $PoPath) {
        if ($Line -match '^msgctxt\s+(".*")$') {
            $CurrentContext = Decode-PoString $Matches[1]
        }
        elseif ($Line -match '^msgid\s+(".*")$') {
            $CurrentId = Decode-PoString $Matches[1]
        }
        elseif ($Line -match '^msgstr\s+(".*")$' -and $null -ne $CurrentId -and $CurrentId.Length -gt 0) {
            $Translation = Decode-PoString $Matches[1]
            ++$Entries
            if ([string]::IsNullOrWhiteSpace($Translation)) {
                ++$Missing
            }
            else {
                ++$Translated
                $SourceTokens = @(Get-Tokens $CurrentId)
                $TargetTokens = @(Get-Tokens $Translation)
                if (($SourceTokens -join '|') -ne ($TargetTokens -join '|')) {
                    ++$TokenErrors
                    $HasErrors = $true
                    Write-Warning "[$Culture] Placeholder mismatch in '$CurrentContext': '$CurrentId' -> '$Translation'"
                }
            }
            $CurrentId = $null
            $CurrentContext = ''
        }
    }

    $Percent = if ($Entries -gt 0) { [math]::Round(($Translated * 100.0) / $Entries, 2) } else { 0 }
    Write-Host "[$Culture] $Translated/$Entries translated ($Percent%); missing=$Missing; placeholder-errors=$TokenErrors"
    if ($Strict -and $Missing -gt 0) {
        $HasErrors = $true
    }
}

if ($HasErrors) {
    throw 'Localization validation failed. Fix the reported entries before importing or shipping.'
}
