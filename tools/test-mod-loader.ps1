param(
    [Parameter(Mandatory=$true)][string]$Cxx,
    [Parameter(Mandatory=$true)][string]$JsonInclude,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'Output directory exists; choose a fresh path to preserve earlier evidence.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$compiler = (Resolve-Path -LiteralPath $Cxx).Path
$previousPath = $env:PATH
$results = @()
try {
    $env:PATH = "$(Split-Path $compiler -Parent);$previousPath"
    foreach ($case in @('mod_loader_contracts', 'texture_mod_lookup_contracts', 'mod_menu_contracts',
                        'mod_assets_contracts', 'mod_character_contracts', 'weapon_evolution_contracts')) {
        $exe = Join-Path $outputRoot ($case + '.exe')
        $arguments = @('-std=c++20', '-O2', '-I', "$projectRoot/port/src", '-I',
            "$projectRoot/tools/kazarov/port", '-I', $JsonInclude,
            "$projectRoot/port/src/mod_loader.cpp")
        if ($case -eq 'texture_mod_lookup_contracts') {
            $arguments += "$projectRoot/port/src/texture_asset_lookup.cpp"
        }
        if ($case -eq 'mod_menu_contracts') {
            $arguments += @("$projectRoot/port/src/mod_menu_state.cpp", "$projectRoot/port/src/options/mods.cpp",
                "$projectRoot/port/src/platform/config.cpp")
        }
        if ($case -eq 'mod_assets_contracts') { $arguments += "$projectRoot/port/src/mod_assets.cpp" }
        if ($case -eq 'mod_character_contracts') { $arguments += "$projectRoot/port/src/mod_character.cpp" }
        if ($case -eq 'weapon_evolution_contracts') { $arguments += "$projectRoot/port/src/weapon_evolution.cpp" }
        $arguments += @("$projectRoot/tools/tests/$case.cpp", '-o', $exe)
        & $compiler @arguments *> (Join-Path $outputRoot ($case + '-build.log'))
        if ($LASTEXITCODE -ne 0) { throw "$case failed to compile" }
        & $exe (Join-Path $outputRoot ($case + '-files')) *> (Join-Path $outputRoot ($case + '-run.log'))
        if ($LASTEXITCODE -ne 0) { throw "$case failed; evidence preserved in $outputRoot" }
        $results += @{test=$case; result='PASS'; executable_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash}
        Write-Output "$case PASS"
    }
    @{compiler=$compiler; source=$projectRoot; results=$results} | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $outputRoot 'test-evidence.json') -Encoding utf8
} finally {
    $env:PATH = $previousPath
}
