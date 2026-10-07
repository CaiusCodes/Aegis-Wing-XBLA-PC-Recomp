<#
.SYNOPSIS
    Regenerates generated\default from the supported default.xex and applies
    the PC port's patches.
.DESCRIPTION
    1. Checks game\default.xex is the build this port supports.
    2. Runs ReXGlue's code generator into a temporary folder.
    3. Applies every PC patch (tools\Apply-GeneratedPcPatches.ps1) there.
    4. Only when both succeed, replaces the output folder with the result.

    A failure at any step leaves the existing generated code untouched. No
    file under generated\ is ever edited by hand.

    The code generator is rexglue.exe from the ReXGlue SDK build. It is built
    together with the game (cmake --build --preset win-amd64-release) into
    <RexSdkDir>\out\win-amd64\rexglue.exe.
.PARAMETER RexSdkDir
    The ReXGlue SDK source folder. Defaults to this project's rexglue-sdk.
.PARAMETER OutputDir
    Where the patched code goes. Defaults to generated\default; point it
    elsewhere to compare a fresh generation with the current one.
#>
[CmdletBinding()]
param(
    [string]$RexSdkDir,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $RexSdkDir) { $RexSdkDir = Join-Path $projectRoot 'rexglue-sdk' }
if (-not $OutputDir) { $OutputDir = Join-Path $projectRoot 'generated\default' }
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

$rexglueExe = Join-Path $RexSdkDir 'out\win-amd64\rexglue.exe'
$manifestPath = Join-Path $projectRoot 'aegis_wing_manifest.toml'
$gameRoot = Join-Path $projectRoot 'game'
$xexPath = Join-Path $gameRoot 'default.xex'
$patcher = Join-Path $PSScriptRoot 'Apply-GeneratedPcPatches.ps1'

# The only build the PC patches were written for (Title 5841083C, XEX 0.0.1.3).
$supportedXexSha256 = 'C57F6A8136EC0C7D966ED83717D1F211CACF64F7D756182E7183C416A55F2873'

if (-not (Test-Path -LiteralPath $rexglueExe -PathType Leaf)) {
    throw "ReXGlue code generator not found: $rexglueExe. Build the project first (it builds rexglue.exe too)."
}
if (-not (Test-Path -LiteralPath $xexPath -PathType Leaf)) {
    throw "game\default.xex not found. Extract your own Aegis Wing package into the game folder first."
}
$xexHash = (Get-FileHash -LiteralPath $xexPath -Algorithm SHA256).Hash
if ($xexHash -ne $supportedXexSha256) {
    throw "game\default.xex is not the supported Aegis Wing build (SHA-256 $xexHash). The PC patches only fit $supportedXexSha256."
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ('aegis-wing-codegen-' + [guid]::NewGuid().ToString('N'))
$workOutput = Join-Path $work 'default'
New-Item -ItemType Directory -Path $workOutput -Force | Out-Null
$previousPath = $env:Path
try {
    # The project manifest with its game and output folders made absolute, so
    # the generator writes into the temporary folder and nowhere else.
    $toml = [System.IO.File]::ReadAllText($manifestPath)
    $toml = $toml.Replace('game_root = "game"', ('game_root = "{0}"' -f $gameRoot.Replace('\', '/')))
    $toml = $toml.Replace('file_path = "game/default.xex"', ('file_path = "{0}"' -f $xexPath.Replace('\', '/')))
    $toml = $toml.Replace('out_directory_path = "generated/default"', ('out_directory_path = "{0}"' -f $workOutput.Replace('\', '/')))
    if ($toml.Contains('"game"') -or $toml.Contains('"generated/default"')) {
        throw "aegis_wing_manifest.toml no longer has the expected game_root / file_path / out_directory_path lines."
    }
    $workManifest = Join-Path $work 'aegis_wing_manifest.toml'
    [System.IO.File]::WriteAllText($workManifest, $toml)

    # A source-built rexglue.exe loads the SDK runtime DLL from the game build.
    $sdkBins = @('win-amd64-release', 'win-amd64-relwithdebinfo', 'win-amd64-debug') |
        ForEach-Object { Join-Path $projectRoot "out\build\$_\sdk-bin" } |
        Where-Object { Test-Path -LiteralPath $_ }
    $env:Path = (@($sdkBins) + $previousPath) -join ';'

    Write-Host "Generating code from default.xex ($xexHash)..."
    & $rexglueExe --force codegen $workManifest
    if ($LASTEXITCODE -ne 0) {
        throw "ReXGlue code generation failed with exit code $LASTEXITCODE. The existing generated code was not changed."
    }

    Write-Host 'Applying the PC patches...'
    & $patcher -GeneratedDir $workOutput

    # Both steps succeeded: swap the result in. It is copied beside the output
    # first (the temporary folder can be on another drive), so the old code is
    # only removed once the new copy is complete.
    $parent = Split-Path -Parent $OutputDir
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $staged = Join-Path $parent ('.regenerated-' + [guid]::NewGuid().ToString('N'))
    Copy-Item -LiteralPath $workOutput -Destination $staged -Recurse
    if (Test-Path -LiteralPath $OutputDir) {
        Remove-Item -LiteralPath $OutputDir -Recurse -Force
    }
    Rename-Item -LiteralPath $staged -NewName (Split-Path -Leaf $OutputDir)
    Write-Host "Generated and patched code is in $OutputDir."
}
finally {
    $env:Path = $previousPath
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force
    }
}
