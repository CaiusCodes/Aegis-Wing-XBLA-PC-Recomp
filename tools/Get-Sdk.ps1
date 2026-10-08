<#
.SYNOPSIS
  Gets, patches and builds the ReXGlue SDK that Aegis Wing XBLA PC Recomp compiles
  against, in this project's rexglue-sdk folder.

.DESCRIPTION
  The project builds ReXGlue in-tree: the CMake presets set REXSDK_DIR to
  <project>\rexglue-sdk, and the game's own build compiles the SDK from there (nothing
  is installed anywhere). That folder is a git submodule of
  https://github.com/rexglue/rexglue-sdk pinned to upstream v0.9.0 (commit below).
  This script, which is only needed when the folder is empty or missing (for example
  after cloning without --recurse-submodules, or from a "Download ZIP" of the source):

    1. fetches ReXGlue at the pinned commit and its (large, nested) submodules,
    2. checks libmspack has the real source files the build uses,
    3. applies patches\rexglue.patch (this port's SDK changes),
    4. configures the project and builds the SDK's code generator and runtime.

  Afterwards extract your own Aegis Wing package into game\, then:

      .\tools\Regenerate-Code.ps1
      cmake --preset win-amd64-release
      cmake --build --preset win-amd64-release --parallel

  Nothing that already exists is ever replaced. An SDK folder that is already at the
  pinned commit is kept as it is (the missing parts are filled in and the patch is
  applied if it is not yet); anything else in that folder makes the script stop.

  libmspack keeps some source files as symlinks. Where git may not create symlinks
  (core.symlinks=false, the default on many Windows setups) they are checked out as
  tiny text files. The patch already points the build at the real files, so unlike
  other ports this script does not touch them. Do not run "git checkout" or
  "git clean" inside thirdparty\libmspack.

.PARAMETER SkipBuild
  Stop after the patch is applied; do not configure or build.

.NOTES
  Needs git on PATH; unless -SkipBuild, also cmake, ninja and clang, plus the Visual C++
  build tools and Windows SDK that clang uses. CMake applies the same patch on every
  configure (cmake\RexgluePatch.cmake), so running this script is a convenience, not
  a requirement, once the SDK folder is populated.
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# What patches\rexglue.patch was made against: upstream v0.9.0. Keep in step with the
# submodule's gitlink and with cmake\RexgluePatch.cmake.
$Pin = '3eb9b511b4140d2769e27be63eae57d41bfa2afa'
$Upstream = 'https://github.com/rexglue/rexglue-sdk.git'

$projectRoot = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $projectRoot 'rexglue-sdk'
$patch = Join-Path $projectRoot 'patches\rexglue.patch'

function Write-Step([string]$Text) { Write-Host ''; Write-Host "== $Text" -ForegroundColor Cyan }

# Runs git and stops on failure. (Named Invoke-GitCommand, not Git: a function named
# after the command it wraps would call itself forever.)
function Invoke-GitCommand {
    & git.exe @args
    if ($LASTEXITCODE -ne 0) { throw ('git ' + ($args -join ' ') + " failed (exit $LASTEXITCODE)") }
}

# Runs git quietly and returns its exit code, for yes/no questions.
function Test-GitCommand {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & git.exe @args *> $null; return ($LASTEXITCODE -eq 0) }
    finally { $ErrorActionPreference = $previous }
}

function Invoke-Tool([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed (exit $LASTEXITCODE)" }
}

function Get-GitOutput {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { return (& git.exe @args 2>$null) } finally { $ErrorActionPreference = $previous }
}

# --- 1. Before touching anything -------------------------------------------------------
Write-Step 'Checking the tools and the patch'
$needed = @('git')
if (-not $SkipBuild) { $needed += @('cmake', 'ninja', 'clang') }
$missing = $needed | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($missing) { throw "Not on PATH: $($missing -join ', '). See README.md, building from source." }
if (-not (Test-Path -LiteralPath $patch -PathType Leaf)) { throw "Missing SDK patch: $patch" }
Write-Host "Project:    $projectRoot"
Write-Host "SDK folder: $sdk"
Write-Host "Patch:      $patch"

$present = (Test-Path -LiteralPath $sdk) -and (Get-ChildItem -LiteralPath $sdk -Force | Select-Object -First 1)
$isCheckout = Test-Path -LiteralPath (Join-Path $sdk '.git')
if ($present -and -not $isCheckout) {
    throw "$sdk exists but is not a git checkout. Existing files are never replaced; move it aside and run again."
}

# --- 2. The SDK checkout at the pinned commit -------------------------------------------
$inProjectRepo = Test-GitCommand -C $projectRoot rev-parse --is-inside-work-tree
$registered = $inProjectRepo -and (Test-GitCommand -C $projectRoot ls-files --error-unmatch -- rexglue-sdk)

if ($isCheckout) {
    Write-Step 'The SDK folder is already a checkout'
}
elseif ($registered) {
    Write-Step 'Fetching ReXGlue at the pinned commit (the project submodule)'
    $gitlink = ((Get-GitOutput -C $projectRoot rev-parse 'HEAD:rexglue-sdk') -join '').Trim()
    if ($gitlink -ne $Pin) {
        throw "The project pins rexglue-sdk at $gitlink but this script expects $Pin. Update Get-Sdk.ps1, cmake\RexgluePatch.cmake and the patch together."
    }
    # core.autocrlf=false so the patch's line endings match the files whatever the
    # machine's own setting; longpaths because some submodules are deeply nested.
    Invoke-GitCommand -C $projectRoot -c core.autocrlf=false -c core.longpaths=true submodule update --init --depth 1 -- rexglue-sdk
}
else {
    Write-Step 'Fetching ReXGlue at the pinned commit (upstream)'
    New-Item -ItemType Directory -Path $sdk -Force | Out-Null
    Invoke-GitCommand -C $sdk init -q
    Invoke-GitCommand -C $sdk config core.autocrlf false
    Invoke-GitCommand -C $sdk config core.longpaths true
    Invoke-GitCommand -C $sdk remote add origin $Upstream
    Invoke-GitCommand -C $sdk fetch --depth 1 origin $Pin
    Invoke-GitCommand -C $sdk -c advice.detachedHead=false checkout --detach $Pin
}

$head = ((Get-GitOutput -C $sdk rev-parse HEAD) -join '').Trim()
if ($head -ne $Pin) {
    throw "$sdk is at $head, not the pinned $Pin. It was left as it is; fix or move it, then run again."
}
Write-Host "ReXGlue at $($Pin.Substring(0, 7)) (v0.9.0)"

Write-Step 'Fetching ReXGlue''s submodules (large: this takes a few minutes)'
Invoke-GitCommand -C $sdk -c core.autocrlf=false -c core.longpaths=true submodule update --init --recursive --depth 1

# --- 3. libmspack -----------------------------------------------------------------------
Write-Step 'Checking libmspack'
$mspackDir = Join-Path $sdk 'thirdparty\libmspack\libmspack\mspack'
$lzxd = Join-Path $mspackDir 'lzxd.c'
if (-not (Test-Path -LiteralPath $lzxd -PathType Leaf) -or (Get-Item -LiteralPath $lzxd).Length -lt 1000) {
    throw "libmspack's real sources are missing or truncated: $mspackDir"
}
$stubDir = Join-Path $sdk 'thirdparty\libmspack\cabextract\mspack'
$stubs = if (Test-Path -LiteralPath $stubDir) { @(Get-ChildItem -LiteralPath $stubDir -File | Where-Object Length -lt 100).Count } else { 0 }
Write-Host "libmspack sources present; $stubs symlink stub file(s) in cabextract\mspack are left alone (the patch bypasses them)"

# --- 4. The patch ------------------------------------------------------------------------
Write-Step 'Applying the Aegis Wing SDK patch'
if (Test-GitCommand -C $sdk apply --check --reverse $patch) {
    Write-Host 'Patch already applied.'
}
else {
    Invoke-GitCommand -C $sdk apply --check $patch
    Invoke-GitCommand -C $sdk apply --whitespace=nowarn $patch
    Write-Host 'Patch applied.'
}
$cmakeFile = Join-Path $sdk 'thirdparty\CMakeLists.txt'
if (-not (Select-String -LiteralPath $cmakeFile -Pattern 'libmspack/libmspack/mspack' -Quiet)) {
    throw 'The patched thirdparty\CMakeLists.txt does not point libmspack at its real sources.'
}
$changed = @(Get-GitOutput -C $sdk status --porcelain --ignore-submodules=all).Count
Write-Host "$changed file(s) differ from upstream (expected 52)"

if ($SkipBuild) {
    Write-Host ''
    Write-Host 'Stopped before the build (-SkipBuild).' -ForegroundColor Yellow
    return
}

# --- 5. Build ----------------------------------------------------------------------------
Push-Location $projectRoot
try {
    Write-Step 'Configuring the project (this also checks the patch)'
    Invoke-Tool 'cmake' @('--preset', 'win-amd64-release')
    Write-Step 'Building the SDK: code generator and runtime - this takes a few minutes'
    Invoke-Tool 'cmake' @('--build', '--preset', 'win-amd64-release', '--target', 'rexglue', 'rexruntime', 'rexgpu-xenos', '--parallel')
}
finally {
    Pop-Location
}

$generator = Join-Path $sdk 'out\win-amd64\rexglue.exe'
if (-not (Test-Path -LiteralPath $generator)) { throw "The build finished but the code generator is missing: $generator" }

Write-Step 'Done'
Write-Host "SDK ready: $sdk"
Write-Host ''
Write-Host 'Next: extract your own Aegis Wing package into game\ (tools\Extract-STFS.ps1), then'
Write-Host '  .\tools\Regenerate-Code.ps1'
Write-Host '  cmake --preset win-amd64-release'
Write-Host '  cmake --build --preset win-amd64-release --parallel'
