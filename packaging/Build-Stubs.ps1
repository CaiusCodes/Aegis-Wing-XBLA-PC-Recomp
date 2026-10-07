# Builds "Setup Aegis Wing.exe" (installer_stub.cpp), the only program in the
# release folder. tools/Make-Release.ps1 appends the install payload to it.
# The game itself is Game\Aegis Wing.exe, which Setup installs. Static CRT, so
# Setup needs no Visual C++ runtime to start.
[CmdletBinding()]
param(
    [string]$Version = '1.0.0',
    [string]$OutputDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'out\stubs')
)

$ErrorActionPreference = 'Stop'
$packaging = $PSScriptRoot
$llvm = 'C:\Program Files\LLVM\bin'
$clang = Join-Path $llvm 'clang-cl.exe'
$rc = Join-Path $llvm 'llvm-rc.exe'
foreach ($tool in @($clang, $rc)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool" }
}

$icon = Join-Path $packaging 'aegis_wing.ico'
if (-not (Test-Path -LiteralPath $icon)) {
    & (Join-Path $packaging 'Make-Icon.ps1') -OutputPath $icon
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# "1.0.26-preview" -> 1,0,26,0 for the binary version fields.
$parts = @(($Version -replace '[^0-9.].*$', '').Split('.') | Where-Object { $_ -ne '' })
while ($parts.Count -lt 4) { $parts += '0' }
$fileVersion = ($parts[0..3]) -join ','

$work = Join-Path $env:TEMP ('aegis-stubs-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    Copy-Item -LiteralPath $icon -Destination (Join-Path $work 'app.ico')
    Set-Content -LiteralPath (Join-Path $work 'app.manifest') -Encoding ASCII -Value @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application>
  </compatibility>
</assembly>
'@

    $stubs = @(
        @{ Source = 'installer_stub.cpp'; Output = 'Setup Aegis Wing.exe'; Description = 'Aegis Wing PC Setup' }
    )
    foreach ($stub in $stubs) {
        $name = [IO.Path]::GetFileNameWithoutExtension($stub.Source)
        $rcPath = Join-Path $work "$name.rc"
        # Windows "installer detection" forces elevation on programs named
        # like setup tools; the asInvoker manifest (RT_MANIFEST = 24) stops it.
        Set-Content -LiteralPath $rcPath -Encoding ASCII -Value @"
1 ICON "app.ico"
1 24 "app.manifest"
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
      VALUE "FileDescription", "$($stub.Description)"
      VALUE "ProductName", "Aegis Wing PC"
      VALUE "ProductVersion", "$Version"
      VALUE "FileVersion", "$Version"
      VALUE "OriginalFilename", "$($stub.Output)"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@
        $resPath = Join-Path $work "$name.res"
        Push-Location $work
        try {
            & $rc /nologo /FO $resPath $rcPath
            if ($LASTEXITCODE -ne 0) { throw "llvm-rc failed for $($stub.Source)" }
        }
        finally {
            Pop-Location
        }

        $output = Join-Path $OutputDir $stub.Output
        & $clang /nologo /std:c++17 /O2 /MT /EHsc /DUNICODE /D_UNICODE "/Fo$work\$name.obj" (Join-Path $packaging $stub.Source) "/Fe$output" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib $resPath
        if ($LASTEXITCODE -ne 0) { throw "clang-cl failed for $($stub.Source)" }
        Write-Host ('Built {0} ({1:N0} bytes)' -f $stub.Output, (Get-Item -LiteralPath $output).Length)
    }
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
