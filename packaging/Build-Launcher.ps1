# Builds the launcher:
#   Aegis Wing.exe     the release's only program at the top level (packaging\launcher\*.cpp)
# It starts the game in resources\runtime, and shows the setup window first when
# the game data has not been imported yet. A plain native Win32 program: static
# CRT (it needs nothing installed to start), no packing, no embedded payload,
# no scripts. It carries the default aegis_wing.toml and the player-name list as
# resources, plus the game icon and version information.
#
# The output goes to out\launcher (not into the source tree); Make-Release.ps1
# calls this and copies the result into the release.
[CmdletBinding()]
param(
    [string]$Version,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$packaging = $PSScriptRoot
$projectRoot = Split-Path -Parent $packaging
if (-not $Version) {
    $Version = ([System.IO.File]::ReadAllText((Join-Path $projectRoot 'VERSION'))).Trim()
}
if ($Version -notmatch '^[0-9][0-9A-Za-z.-]*$') { throw "Unexpected version text: $Version" }
if (-not $OutputDir) { $OutputDir = Join-Path $projectRoot 'out\launcher' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$llvm = 'C:\Program Files\LLVM\bin'
$clang = Join-Path $llvm 'clang-cl.exe'
$rc = Join-Path $llvm 'llvm-rc.exe'
foreach ($tool in @($clang, $rc)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool" }
}

# The game's own icon (the roundel with the four-ship formation), the same one the game window uses.
$icon = Join-Path $packaging 'aegis_wing.ico'
if (-not (Test-Path -LiteralPath $icon)) { throw "Missing icon: $icon" }

# "0.9.1" -> 0,9,1,0 for the binary version fields.
$parts = @(($Version -replace '[^0-9.].*$', '').Split('.') | Where-Object { $_ -ne '' })
while ($parts.Count -lt 4) { $parts += '0' }
$fileVersion = ($parts[0..3]) -join ','

$sourceDir = Join-Path $packaging 'launcher'
$output = Join-Path $OutputDir 'Aegis Wing.exe'
$work = Join-Path $env:TEMP ('aw-launcher-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    Copy-Item -LiteralPath $icon -Destination (Join-Path $work 'app.ico')
    Copy-Item -LiteralPath (Join-Path $packaging 'aegis_wing.toml') -Destination (Join-Path $work 'aegis_wing.toml')
    Copy-Item -LiteralPath (Join-Path $packaging 'player_names.txt') -Destination (Join-Path $work 'player_names.txt')
    # asInvoker: never asks for administrator rights. PerMonitorV2: the setup
    # window scales itself for each monitor's DPI instead of being stretched.
    Set-Content -LiteralPath (Join-Path $work 'app.manifest') -Encoding ASCII -Value @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="AegisWing.Launcher" version="1.0.0.0" processorArchitecture="amd64"/>
  <description>Aegis Wing XBLA Recomp</description>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application>
  </compatibility>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </windowsSettings>
  </application>
</assembly>
'@
    $rcPath = Join-Path $work 'launcher.rc'
    Set-Content -LiteralPath $rcPath -Encoding ASCII -Value @"
1 ICON "app.ico"
1 24 "app.manifest"
101 RCDATA "aegis_wing.toml"
102 RCDATA "player_names.txt"
1 VERSIONINFO
 FILEVERSION $fileVersion
 PRODUCTVERSION $fileVersion
 FILEOS 0x40004
 FILETYPE 0x1
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "Aegis Wing XBLA Recomp project"
      VALUE "FileDescription", "Aegis Wing"
      VALUE "ProductName", "Aegis Wing XBLA Recomp"
      VALUE "ProductVersion", "$Version"
      VALUE "FileVersion", "$Version"
      VALUE "InternalName", "Aegis Wing launcher"
      VALUE "OriginalFilename", "Aegis Wing.exe"
      VALUE "LegalCopyright", "Open source (GPL-3.0 launcher; BSD-3-Clause game), see licenses folder"
      VALUE "Comments", "https://github.com/CaiusCodes/Aegis-Wing-XBLA-PC-Recomp"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@
    $resPath = Join-Path $work 'launcher.res'
    Push-Location $work
    try {
        & $rc /nologo /FO $resPath $rcPath
        if ($LASTEXITCODE -ne 0) { throw 'llvm-rc failed for the launcher' }
    }
    finally {
        Pop-Location
    }

    $sources = @(Get-ChildItem -LiteralPath $sourceDir -Filter *.cpp | ForEach-Object FullName)
    & $clang /nologo /std:c++17 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DNOMINMAX "/DAW_VERSION=$Version" `
        "/I$sourceDir" "/Fo$work\" @sources "/Fe$output" /link /SUBSYSTEM:WINDOWS `
        user32.lib gdi32.lib gdiplus.lib shell32.lib ole32.lib uuid.lib bcrypt.lib $resPath
    if ($LASTEXITCODE -ne 0) { throw 'clang-cl failed for the launcher' }
    Write-Host ('Built {0} ({1:N0} bytes)' -f $output, (Get-Item -LiteralPath $output).Length)
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
