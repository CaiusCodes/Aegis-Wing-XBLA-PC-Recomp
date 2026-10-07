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
$projectName = "Aegis Wing XBLA Recomp"
$stageRoot = Join-Path $stagingRoot $projectName
$payloadRoot = Join-Path $stagingRoot "payload"
$stubRoot = Join-Path $releaseRoot ".stubs"
$zipPath = Join-Path $releaseRoot ("{0}-v{1}.zip" -f ($projectName -replace ' ', '-'), $Version)
$setupExecutable = "Setup Aegis Wing.exe"

# The standard XBLA recomp release layout:
#
#   Aegis Wing XBLA Recomp\       (the ZIP holds exactly this)
#     Setup Aegis Wing.exe        Setup, with the install payload appended
#     README.txt
#     licenses\
#
# Setup creates Game\ beside itself from its payload plus the player's own
# package. The payload is an image of what Setup installs into Game\:
#
#   Aegis Wing.exe, rex*.dll, the Visual C++ runtime DLLs
#   resources\installer\          Setup script, extractor, menu patcher, version.txt
#   resources\defaults\           aegis_wing.toml (used when the player has none),
#                                 player_names.txt (Setup picks a random player name)
#   release-manifest.json         every file above, with size and SHA-256
#
# and Setup adds Game\assets (the unpacked, PC-patched game data), keeps
# Game\userdata, Game\aegis_wing.toml and Game\logs, and writes its own
# Game\release-manifest.json recording the install.
$gameExecutable = "Aegis Wing.exe"
$gameExecutablePath = Join-Path $buildRoot $gameExecutable
if (-not (Test-Path -LiteralPath $gameExecutablePath -PathType Leaf)) {
    throw "Required release file is missing: $gameExecutablePath"
}
# Include only the runtime family imported by this exact game executable.
# Development builds use the rd suffix; Release builds use the plain names.
$gameBinaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($gameExecutablePath))
$runtimeFiles = @($gameExecutable)
if ($gameBinaryText.Contains("rexruntimerd.dll")) {
    $runtimeFiles += @("rexruntimerd.dll", "rexgpu-xenosrd.dll")
}
elseif ($gameBinaryText.Contains("rexruntime.dll")) {
    $runtimeFiles += @("rexruntime.dll", "rexgpu-xenos.dll")
}
else {
    throw "The Aegis Wing runtime dependency family could not be identified."
}
$installerTools = @("Install-AegisWing.ps1", "Extract-STFS.ps1", "Patch-PcMenuText.ps1")

foreach ($file in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $file) -PathType Leaf)) {
        throw "Required release file is missing: $(Join-Path $buildRoot $file)"
    }
}
foreach ($file in $installerTools) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "tools\$file") -PathType Leaf)) {
        throw "Required release tool is missing: tools\$file"
    }
}
foreach ($file in @("README.txt", "aegis_wing.toml", "player_names.txt")) {
    if (-not (Test-Path -LiteralPath (Join-Path $packagingRoot $file) -PathType Leaf)) {
        throw "Required packaging file is missing: packaging\$file"
    }
}

# Setup, with the release version stamped in (the payload is appended below).
if (Test-Path -LiteralPath $stubRoot) { Remove-Item -LiteralPath $stubRoot -Recurse -Force }
& (Join-Path $packagingRoot "Build-Stubs.ps1") -Version $Version -OutputDir $stubRoot

if (Test-Path -LiteralPath $stagingRoot) { Remove-Item -LiteralPath $stagingRoot -Recurse -Force }
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
$installerStage = Join-Path $payloadRoot "resources\installer"
$defaultsStage = Join-Path $payloadRoot "resources\defaults"
$gameStage = $payloadRoot
foreach ($dir in @($installerStage, $defaultsStage, (Join-Path $stageRoot "licenses"))) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

Copy-Item -LiteralPath (Join-Path $packagingRoot "README.txt") -Destination $stageRoot
foreach ($file in $installerTools) { Copy-Item -LiteralPath (Join-Path $projectRoot "tools\$file") -Destination $installerStage }
Set-Content -LiteralPath (Join-Path $installerStage "version.txt") -Value $Version -Encoding ASCII
foreach ($file in $runtimeFiles) { Copy-Item -LiteralPath (Join-Path $buildRoot $file) -Destination $gameStage }
Copy-Item -LiteralPath (Join-Path $packagingRoot "aegis_wing.toml") -Destination $defaultsStage
Copy-Item -LiteralPath (Join-Path $packagingRoot "player_names.txt") -Destination $defaultsStage

# Every DLL the shipped binaries import must either come with Windows or be
# shipped beside them. Microsoft's C++ runtime is shipped app-local (allowed
# by the Visual Studio redistributable terms), so players do not need the
# Visual C++ Redistributable installed. Any other missing import stops the
# release.
$readobj = "C:\Program Files\LLVM\bin\llvm-readobj.exe"
if (-not (Test-Path -LiteralPath $readobj)) { throw "Missing build tool: $readobj" }
$windowsDlls = @("kernel32", "user32", "gdi32", "advapi32", "shell32", "ole32", "oleaut32", "bcrypt",
    "dxgi", "d3d12", "dbghelp", "hid", "imm32", "setupapi", "iphlpapi", "version", "winmm", "ws2_32", "dinput8",
    "comdlg32", "shlwapi", "ntdll", "xinput1_4", "cfgmgr32")
$stagedNames = @(Get-ChildItem -LiteralPath $gameStage -File | ForEach-Object { $_.Name.ToLowerInvariant() })
$missingImports = New-Object System.Collections.Generic.List[string]
foreach ($binary in Get-ChildItem -LiteralPath $gameStage -File | Where-Object { $_.Extension -in ".exe", ".dll" }) {
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
        Copy-Item -LiteralPath $source -Destination $gameStage
    }
    Write-Host ("Bundled {0} from {1}" -f ($missingImports -join ", "), $crtDir.FullName)
}

$licenseSource = Join-Path $packagingRoot "licenses"
if (Test-Path -LiteralPath $licenseSource) {
    Get-ChildItem -LiteralPath $licenseSource -File |
        Where-Object Name -ne "LICENSE-Tracy.txt" |
        Copy-Item -Destination (Join-Path $stageRoot "licenses")
}
# This project's own BSD-3-Clause licence (it covers Game\Aegis Wing.exe's
# host code, Setup and the scripts other than Extract-STFS).
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $packagingRoot) "LICENSE") `
    -Destination (Join-Path $stageRoot "licenses\LICENSE-Aegis-Wing-XBLA-Recomp.txt")

# License texts of the third-party code compiled into the ReXGlue runtime.
$sdkLine = Select-String -LiteralPath (Join-Path $buildRoot "CMakeCache.txt") -Pattern '^REXSDK_DIR:PATH=(.+)$' | Select-Object -First 1
if (-not $sdkLine) { throw "REXSDK_DIR is not set in the build's CMakeCache.txt." }
$sdkThirdParty = Join-Path $sdkLine.Matches[0].Groups[1].Value "thirdparty"
$thirdPartyStage = Join-Path $stageRoot "licenses\third-party"
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

# release-manifest.json: every payload file with its size and SHA-256, by its
# path inside Game\. Setup checks the unpacked payload against it, installs it
# into Game\, and keeps its own copy there to recognise its files on a rerun.
$manifestFiles = @(Get-ChildItem -LiteralPath $payloadRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path   = $_.FullName.Substring($payloadRoot.Length + 1)
            bytes  = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    })
$manifest = [ordered]@{
    schema          = 1
    product         = $projectName
    game            = "Aegis Wing"
    version         = $Version
    setupExecutable = $setupExecutable
    gameExecutable  = $gameExecutable
    files           = $manifestFiles
    generated       = @("assets")
    userData        = @("aegis_wing.toml", "userdata", "logs")
}
[IO.File]::WriteAllText((Join-Path $payloadRoot "release-manifest.json"), ($manifest | ConvertTo-Json -Depth 5),
    (New-Object Text.UTF8Encoding($false)))

# The release must never carry game content: no extracted game files and no
# Xbox 360 package (the project folder keeps the developer's own copy).
foreach ($staged in @(Get-ChildItem -LiteralPath $stageRoot, $payloadRoot, (Join-Path $stubRoot $setupExecutable) -File -Recurse)) {
    if ($staged.Name -eq "default.xex" -or $staged.Extension -eq ".xzp") {
        throw "Game data found in the release staging folder: $($staged.FullName)"
    }
    if ($staged.Length -ge 0x10000) {
        $stream = [IO.File]::OpenRead($staged.FullName)
        try {
            $magic = New-Object byte[] 4
            [void]$stream.Read($magic, 0, 4)
        }
        finally {
            $stream.Dispose()
        }
        if (@("LIVE", "PIRS", "CON ") -contains [Text.Encoding]::ASCII.GetString($magic)) {
            throw "An Xbox 360 package found in the release staging folder: $($staged.FullName)"
        }
    }
    # No file may carry a personal folder path (and with it a Windows user
    # name), in either 8-bit or UTF-16 text.
    $content = [IO.File]::ReadAllBytes($staged.FullName)
    foreach ($text in @([Text.Encoding]::ASCII.GetString($content), [Text.Encoding]::Unicode.GetString($content))) {
        $leak = [regex]::Match($text, '[A-Za-z]:[\\/]Users[\\/][^\\/\x00]{1,64}[\\/]')
        if ($leak.Success) {
            throw "A personal folder path ($($leak.Value)) is embedded in $($staged.FullName)."
        }
    }
}

# Setup = the Setup program with the payload appended (format documented in
# packaging/installer_stub.cpp): per file a u16 UTF-8 path length, the path
# ('/' separators), a u64 size and the bytes; then the u64 offset of the first
# entry and the magic "XBLAPAY1".
$setupPath = Join-Path $stageRoot $setupExecutable
Copy-Item -LiteralPath (Join-Path $stubRoot $setupExecutable) -Destination $setupPath
$stream = [IO.File]::Open($setupPath, [IO.FileMode]::Append, [IO.FileAccess]::Write)
$writer = New-Object IO.BinaryWriter($stream)
try {
    $payloadStart = [uint64]$stream.Position
    foreach ($file in Get-ChildItem -LiteralPath $payloadRoot -File -Recurse | Sort-Object FullName) {
        $relative = $file.FullName.Substring($payloadRoot.Length + 1).Replace('\', '/')
        $pathBytes = [Text.Encoding]::UTF8.GetBytes($relative)
        $writer.Write([uint16]$pathBytes.Length)
        $writer.Write($pathBytes)
        $writer.Write([uint64]$file.Length)
        $writer.Write([IO.File]::ReadAllBytes($file.FullName))
    }
    $writer.Write($payloadStart)
    $writer.Write([Text.Encoding]::ASCII.GetBytes("XBLAPAY1"))
}
finally {
    $writer.Dispose()
}

# The release folder holds exactly Setup, README.txt and licenses\: no Game
# folder, no runtime files, nothing else.
$rootItems = @(Get-ChildItem -LiteralPath $stageRoot -Force | ForEach-Object { $_.Name } | Sort-Object)
$expectedRoot = @("licenses", "README.txt", $setupExecutable) | Sort-Object
if (Compare-Object $rootItems $expectedRoot) {
    throw "The release folder holds more than Setup, README.txt and licenses: $($rootItems -join ', ')"
}

Compress-Archive -LiteralPath $stageRoot -DestinationPath $zipPath -CompressionLevel Optimal
$releaseFiles = Get-ChildItem -LiteralPath $stageRoot -File -Recurse
$releaseSize = ($releaseFiles | Measure-Object Length -Sum).Sum
$zip = Get-Item -LiteralPath $zipPath
$hash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256

Write-Host "Clean Aegis Wing release created successfully."
Write-Host "Folder: $stageRoot"
Write-Host "ZIP:    $zipPath"
Write-Host "Files:  $($releaseFiles.Count)"
Write-Host ("Size:   {0:N2} MB uncompressed; {1:N2} MB ZIP" -f ($releaseSize / 1MB), ($zip.Length / 1MB))
Write-Host "SHA256: $($hash.Hash)"
