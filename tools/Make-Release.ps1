# Builds the Aegis Wing portable release in out\release. The ZIP holds one folder:
#
#   Aegis Wing\
#     Aegis Wing.exe              the launcher: the only program to open
#     README.txt
#     licenses\
#     resources\
#       runtime\                  the game (Aegis Wing.exe, rexruntime.dll,
#                                 rexgpu-xenos.dll, VC++ runtime DLLs)
#
# The first time the launcher runs it imports the game data from the player's
# own Xbox 360 package into resources\assets; saves and settings go to
# resources\userdata. The release carries no original game content; the audit
# below refuses to package anything that looks like game data, debug output or
# user state.
[CmdletBinding()]
param(
    [string]$BuildPreset = "win-amd64-release",
    [string]$Version
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# VERSION is compiled into the game (it shows on the main menu), so the
# release number must come from the same file.
$versionPath = Join-Path $PSScriptRoot "..\VERSION"
$builtVersion = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if (-not $Version) { $Version = $builtVersion }
if ($Version -ne $builtVersion) {
    throw "Version $Version does not match VERSION ($builtVersion), which the game was built with. Edit VERSION and rebuild."
}
if ($Version -notmatch '^[A-Za-z0-9._-]+$') {
    throw "Version may contain only letters, numbers, dots, underscores and hyphens."
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = Join-Path $projectRoot "out\build\$BuildPreset"
$packagingRoot = Join-Path $projectRoot "packaging"
$releaseRoot = Join-Path $projectRoot "out\release"
$stagingRoot = Join-Path $releaseRoot ".package-staging"
$releaseName = "Aegis Wing"
$stageRoot = Join-Path $stagingRoot $releaseName
$zipPath = Join-Path $releaseRoot ("Aegis-Wing-XBLA-Recomp-v{0}.zip" -f $Version)
$exeName = "Aegis Wing.exe"

$gameExePath = Join-Path $buildRoot $exeName
if (-not (Test-Path -LiteralPath $gameExePath -PathType Leaf)) {
    throw "Required release file is missing: $gameExePath. Build $BuildPreset first."
}
# A release must never ship a binary older than the source it claims to be.
$exeTime = (Get-Item -LiteralPath $gameExePath).LastWriteTimeUtc
$newestSource = Get-ChildItem -LiteralPath (Join-Path $projectRoot "src") -File -Recurse |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if ($newestSource.LastWriteTimeUtc -gt $exeTime) {
    throw "$exeName is older than src\$($newestSource.Name). Rebuild before packaging."
}

# Include only the runtime family imported by this exact game executable.
# Development builds use the rd suffix; Release builds use the plain names.
$gameBinaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($gameExePath))
$runtimeFiles = @($exeName)
if ($gameBinaryText.Contains("rexruntimerd.dll")) {
    throw "The game was built with the rd runtime family; the launcher expects rexruntime.dll and rexgpu-xenos.dll. Build $BuildPreset."
}
elseif ($gameBinaryText.Contains("rexruntime.dll")) {
    $runtimeFiles += @("rexruntime.dll", "rexgpu-xenos.dll")
}
else {
    throw "The Aegis Wing runtime dependency family could not be identified."
}
foreach ($file in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $file) -PathType Leaf)) {
        throw "Required release file is missing: $(Join-Path $buildRoot $file)"
    }
}
foreach ($file in @("README.txt", "aegis_wing.toml", "player_names.txt", "aegis_wing.ico")) {
    if (-not (Test-Path -LiteralPath (Join-Path $packagingRoot $file) -PathType Leaf)) {
        throw "Required packaging file is missing: packaging\$file"
    }
}

# The launcher copies a previous install's menu archive as it is only when it is
# exactly the archive this release's menu patch produces; its constant must match
# the development build's patched copy (made by tools\Patch-PcMenuText.ps1).
$patchedMenus = Join-Path $buildRoot "assets\Media\Wingmen.xzp"
if (Test-Path -LiteralPath $patchedMenus -PathType Leaf) {
    $constant = [regex]::Match([IO.File]::ReadAllText((Join-Path $packagingRoot "launcher\core.cpp")),
        'kPatchedMenuSha256 = "([0-9A-F]{64})"')
    if (-not $constant.Success) { throw "kPatchedMenuSha256 was not found in packaging\launcher\core.cpp." }
    $actual = (Get-FileHash -LiteralPath $patchedMenus -Algorithm SHA256).Hash
    if ($actual -ne $constant.Groups[1].Value) {
        throw "kPatchedMenuSha256 in packaging\launcher\core.cpp ($($constant.Groups[1].Value)) does not match the patched menu archive ($actual). Update the constant."
    }
}

# --- 1. The launcher ---------------------------------------------------------------
$launcherDir = Join-Path $projectRoot "out\launcher"
& (Join-Path $packagingRoot "Build-Launcher.ps1") -Version $Version -OutputDir $launcherDir
$launcher = Join-Path $launcherDir $exeName
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "The launcher was not built: $launcher" }

# --- 2. Stage ------------------------------------------------------------------------
if (Test-Path -LiteralPath $stagingRoot) { Remove-Item -LiteralPath $stagingRoot -Recurse -Force }
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
$licensesStage = Join-Path $stageRoot "licenses"
$runtimeStage = Join-Path $stageRoot "resources\runtime"
foreach ($dir in @($stageRoot, $licensesStage, $runtimeStage)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

# Release root: the launcher, README and licences.
Copy-Item -LiteralPath $launcher -Destination $stageRoot
$readme = [System.IO.File]::ReadAllText((Join-Path $packagingRoot "README.txt")).Replace("@VERSION@", $Version)
$readme = $readme -replace "`r?`n", "`r`n"  # Notepad-friendly line endings
[System.IO.File]::WriteAllText((Join-Path $stageRoot "README.txt"), $readme, (New-Object System.Text.UTF8Encoding($false)))

# resources\runtime: the game and its DLLs. The game reads its data from
# resources\assets and resources\userdata (next to runtime) when it runs from
# a folder named runtime.
foreach ($file in $runtimeFiles) { Copy-Item -LiteralPath (Join-Path $buildRoot $file) -Destination $runtimeStage }

# Every DLL the shipped binaries import must either come with Windows or be
# shipped beside them. Microsoft's C++ runtime is shipped app-local (allowed
# by the Visual Studio redistributable terms), so players do not need the
# Visual C++ Redistributable installed. Any other missing import stops the
# release. The launcher itself is statically linked and imports only Windows DLLs.
$readobj = "C:\Program Files\LLVM\bin\llvm-readobj.exe"
if (-not (Test-Path -LiteralPath $readobj)) { throw "Missing build tool: $readobj" }
$windowsDlls = @("kernel32", "user32", "gdi32", "advapi32", "shell32", "ole32", "oleaut32", "bcrypt",
    "dxgi", "d3d12", "dbghelp", "hid", "imm32", "setupapi", "iphlpapi", "version", "winmm", "ws2_32", "dinput8",
    "comdlg32", "shlwapi", "ntdll", "xinput1_4", "cfgmgr32", "gdiplus", "uxtheme")
$stagedNames = @(Get-ChildItem -LiteralPath $runtimeStage -File | ForEach-Object { $_.Name.ToLowerInvariant() })
$missingImports = New-Object System.Collections.Generic.List[string]
foreach ($binary in @(Get-ChildItem -LiteralPath $runtimeStage -File | Where-Object { $_.Extension -in ".exe", ".dll" }) + @(Get-Item -LiteralPath (Join-Path $stageRoot $exeName))) {
    foreach ($line in (& $readobj --coff-imports $binary.FullName)) {
        if ($line -match 'Name:\s*(\S+\.dll)\s*$') {
            $dll = $Matches[1].ToLowerInvariant()
            if ($dll -like "api-ms-win-*" -or $windowsDlls -contains [IO.Path]::GetFileNameWithoutExtension($dll) -or
                $stagedNames -contains $dll -or $missingImports -contains $dll) { continue }
            $missingImports.Add($dll)
        }
    }
}
if ($missingImports.Count -gt 0) {
    # Get-Item with the wildcard in the last segment: Get-ChildItem -Filter
    # under a wildcard parent path silently returns nothing.
    $crtDir = @(Get-Item "C:\Program Files\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -ErrorAction SilentlyContinue) |
        Sort-Object { [version]([regex]::Match($_.FullName, '\\MSVC\\([\d\.]+)\\').Groups[1].Value) } -Descending |
        Select-Object -First 1
    if (-not $crtDir) { throw "No Visual C++ redistributable folder was found; cannot ship: $($missingImports -join ', ')" }
    foreach ($dll in $missingImports) {
        $source = Join-Path $crtDir.FullName $dll
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "The game imports $dll, which is neither part of Windows nor in $($crtDir.FullName)."
        }
        Copy-Item -LiteralPath $source -Destination $runtimeStage
    }
    Write-Host ("Bundled {0} from {1}" -f ($missingImports -join ", "), $crtDir.FullName)
}
# The launcher starts the game only when these are all present.
foreach ($needed in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeStage $needed))) {
        throw "$needed is not in resources\runtime, so the launcher would not start the game."
    }
}

# Licences: the project's own, the STFS reader's (the launcher is GPL-3.0),
# ReXGlue's and the third-party notices.
$licenseSource = Join-Path $packagingRoot "licenses"
Get-ChildItem -LiteralPath $licenseSource -File |
    Where-Object Name -ne "LICENSE-Tracy.txt" |
    Copy-Item -Destination $licensesStage
# This project's own BSD-3-Clause licence (it covers the game's host code in
# resources\runtime and the launcher's own code other than its STFS reader).
Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") -Destination (Join-Path $licensesStage "LICENSE-Aegis-Wing-XBLA-Recomp.txt")

# License texts of the third-party code compiled into the ReXGlue runtime.
$sdkLine = Select-String -LiteralPath (Join-Path $buildRoot "CMakeCache.txt") -Pattern '^REXSDK_DIR:PATH=(.+)$' | Select-Object -First 1
if (-not $sdkLine) { throw "REXSDK_DIR is not set in the build's CMakeCache.txt." }
$sdkThirdParty = Join-Path $sdkLine.Matches[0].Groups[1].Value "thirdparty"
$thirdPartyStage = Join-Path $licensesStage "third-party"
New-Item -ItemType Directory -Path $thirdPartyStage -Force | Out-Null
$thirdPartyLicenses = [ordered]@{
    "SDL3"                  = "sdl3\LICENSE.txt"
    "FFmpeg"                = "FFmpeg\LICENSE.md"
    "FFmpeg-LGPL-2.1"       = "FFmpeg\COPYING.LGPLv2.1"
    "libmspack-LGPL-2.1"    = "libmspack\libmspack\COPYING.LIB"
    "fmt"                   = "fmt\LICENSE"
    "spdlog"                = "spdlog\LICENSE"
    "snappy"                = "snappy\COPYING"
    "xxHash"                = "xxHash\LICENSE"
    "o1heap"                = "o1heap\LICENSE"
    "aes_128"               = "aes_128\LICENSE"
    "des"                   = "crypto\des\LICENSE"
    "imgui"                 = "imgui\LICENSE.txt"
    "tomlplusplus"          = "tomlplusplus\LICENSE"
    "simde"                 = "simde\COPYING"
    "utfcpp"                = "utfcpp\LICENSE"
    "volk"                  = "volk\LICENSE.md"
    "VulkanMemoryAllocator" = "vulkan-memory-allocator\LICENSE.txt"
}
foreach ($name in $thirdPartyLicenses.Keys) {
    $source = Join-Path $sdkThirdParty $thirdPartyLicenses[$name]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Third-party license text is missing: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $thirdPartyStage "$name.txt")
}

# --- 3. Audit: only the four root items, nothing from the game package, no
# debug output, no user state ---------------------------------------------------------
foreach ($item in @(Get-ChildItem -LiteralPath $stageRoot -Force)) {
    if (@($exeName, "README.txt", "licenses", "resources") -notcontains $item.Name) {
        throw "Unexpected item in the release root: $($item.Name)"
    }
}
foreach ($required in @($exeName, "README.txt", "licenses", "resources")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $required))) { throw "Missing from the release root: $required" }
}
foreach ($item in @(Get-ChildItem -LiteralPath (Join-Path $stageRoot "resources") -Force)) {
    if ($item.Name -ne "runtime") { throw "Unexpected item in resources: $($item.Name)" }
}
$staged = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -Force)
$forbiddenNames = @("assets", "userdata", "logs", "cache", "Game", "aegis_wing.toml", "default.xex",
    "ArcadeInfo.xml", "Media", "guest_image.bin", "high_scores.tsv")
$forbiddenPatterns = @("*.pdb", "*.xex", "*.bin", "*.log", "*.ilk", "*.exp", "*.lib", "*.ps1", "*.xzp", "*.xwb",
    "*.xur", "*.ttf", "*.dmp", "*.tmp")
foreach ($item in $staged) {
    if ($forbiddenNames -contains $item.Name) { throw "Forbidden item entered the release: $($item.FullName)" }
    foreach ($pattern in $forbiddenPatterns) {
        if ($item.Name -like $pattern) { throw "Forbidden file type entered the release: $($item.FullName)" }
    }
    if (-not $item.PSIsContainer -and $item.Length -gt 64MB) {
        throw "Unexpectedly large file in the release: $($item.FullName)"
    }
}
$stagedFiles = @($staged | Where-Object { -not $_.PSIsContainer })
foreach ($file in $stagedFiles) {
    # No Xbox 360 package (the project folder keeps the developer's own copy).
    if ($file.Length -ge 0x10000) {
        $stream = [IO.File]::OpenRead($file.FullName)
        try {
            $magic = New-Object byte[] 4
            [void]$stream.Read($magic, 0, 4)
        }
        finally {
            $stream.Dispose()
        }
        if (@("LIVE", "PIRS", "CON ") -contains [Text.Encoding]::ASCII.GetString($magic)) {
            throw "An Xbox 360 package found in the release staging folder: $($file.FullName)"
        }
    }
    # No file may carry a personal folder path (and with it a Windows user
    # name), in either 8-bit or UTF-16 text.
    $content = [IO.File]::ReadAllBytes($file.FullName)
    foreach ($text in @([Text.Encoding]::ASCII.GetString($content), [Text.Encoding]::Unicode.GetString($content))) {
        $leak = [regex]::Match($text, '[A-Za-z]:[\\/]Users[\\/][^\\/\x00]{1,64}[\\/]')
        if ($leak.Success) { throw "A personal folder path ($($leak.Value)) is embedded in $($file.FullName)." }
    }
}

# Byte-level check against the developer's extracted game data, when present:
# no shipped file may be identical to any file from the package.
$assetsRoot = Join-Path $projectRoot "game"
$assetComparisons = 0
if (Test-Path -LiteralPath $assetsRoot) {
    $stagedSizes = @{}
    foreach ($file in $stagedFiles) { $stagedSizes[$file.Length] = $true }
    $stagedHashes = @{}
    foreach ($file in $stagedFiles) {
        $stagedHashes[(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash] = $file.FullName
    }
    foreach ($asset in @(Get-ChildItem -LiteralPath $assetsRoot -File -Recurse)) {
        if (-not $stagedSizes.ContainsKey($asset.Length)) { continue }
        $assetComparisons++
        $hash = (Get-FileHash -LiteralPath $asset.FullName -Algorithm SHA256).Hash
        if ($stagedHashes.ContainsKey($hash)) {
            throw "Release file $($stagedHashes[$hash]) is identical to game data $($asset.FullName)"
        }
    }
}

# --- 4. The release ZIP ---------------------------------------------------------------
# Written entry by entry so every name uses "/" (Windows PowerShell's own
# archive helpers write "\", which other unzip tools do not accept).
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipStream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::CreateNew)
$archive = New-Object System.IO.Compression.ZipArchive($zipStream, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $baseLength = (Split-Path -Parent $stageRoot).Length + 1
    foreach ($file in $stagedFiles | Sort-Object FullName) {
        $name = $file.FullName.Substring($baseLength).Replace('\', '/')
        $entry = $archive.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $file.LastWriteTime
        $input = [System.IO.File]::OpenRead($file.FullName)
        $output = $entry.Open()
        try { $input.CopyTo($output) }
        finally { $output.Dispose(); $input.Dispose() }
    }
}
finally {
    $archive.Dispose()
    $zipStream.Dispose()
}

$releaseSize = ($stagedFiles | Measure-Object -Property Length -Sum).Sum
$zip = Get-Item -LiteralPath $zipPath
$hash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
Write-Host "Portable Aegis Wing release created."
Write-Host "Folder:  $stageRoot"
Write-Host "ZIP:     $zipPath"
Write-Host "Root:    $(@(Get-ChildItem -LiteralPath $stageRoot | ForEach-Object Name) -join ', ')"
Write-Host "Runtime: $(@(Get-ChildItem -LiteralPath $runtimeStage -File).Count) files in resources\runtime"
Write-Host "Audit:   only the four root items; no forbidden items; $assetComparisons same-size game files compared, none identical"
Write-Host ("Size:    {0:N2} MB uncompressed; {1:N2} MB ZIP" -f ($releaseSize / 1MB), ($zip.Length / 1MB))
Write-Host "SHA256:  $($hash.Hash)"
