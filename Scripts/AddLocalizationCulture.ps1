param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z]{2,3}([_-][A-Za-z]{2,4})?$')]
    [string]$Culture
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Culture = $Culture.Replace('_', '-')

try {
    [void][System.Globalization.CultureInfo]::GetCultureInfo($Culture)
}
catch {
    throw "Unknown culture code '$Culture'. Use an IETF code such as de, fr, es, pt-BR or zh-Hans."
}

function Add-LineAfterLastMatch {
    param([string]$Path, [string]$Pattern, [string]$Line)

    $Lines = [System.Collections.Generic.List[string]]::new()
    $Lines.AddRange([string[]](Get-Content -LiteralPath $Path))
    if ($Lines -contains $Line) {
        return
    }

    $LastIndex = -1
    for ($Index = 0; $Index -lt $Lines.Count; ++$Index) {
        if ($Lines[$Index] -match $Pattern) {
            $LastIndex = $Index
        }
    }
    if ($LastIndex -lt 0) {
        throw "Could not find insertion point '$Pattern' in $Path."
    }

    $Lines.Insert($LastIndex + 1, $Line)
    [System.IO.File]::WriteAllLines($Path, $Lines, [System.Text.UTF8Encoding]::new($false))
}

foreach ($ConfigName in @('Game_Gather.ini', 'Game_Export.ini', 'Game_Import.ini')) {
    Add-LineAfterLastMatch `
        -Path (Join-Path $Root "..\Config\Localization\$ConfigName") `
        -Pattern '^CulturesToGenerate=' `
        -Line "CulturesToGenerate=$Culture"
}

$GameConfig = Join-Path $Root '..\Config\DefaultGame.ini'
Add-LineAfterLastMatch -Path $GameConfig -Pattern '^\+CulturesToStage=' -Line "+CulturesToStage=$Culture"
Add-LineAfterLastMatch -Path $GameConfig -Pattern '^\+SupportedCultures=' -Line "+SupportedCultures=$Culture"

New-Item -ItemType Directory -Force -Path (Join-Path $Root "..\Content\Localization\Game\$Culture") | Out-Null

Write-Host "Added culture '$Culture'."
Write-Host 'Run Scripts\GatherLocalization.bat, translate its Game.po, validate, then import.'
