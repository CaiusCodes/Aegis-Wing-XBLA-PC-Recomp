<#
.SYNOPSIS
  Saves the edits made in rexglue-sdk\ as patches\rexglue.patch.

.DESCRIPTION
  The project builds ReXGlue in-tree: patches\rexglue.patch is applied to the
  rexglue-sdk submodule's working tree at every configure (cmake\RexgluePatch.cmake).
  After editing SDK files in place, run this script. It

    1. writes the diff against the pinned upstream commit (src, include,
       thirdparty\CMakeLists.txt and cmake\rex_version.cmake) to patches\rexglue.patch,
    2. proves it: the patch must apply cleanly to a pristine copy of those files from
       the pinned commit, and the result must equal the SDK's working files
       (line endings ignored),
    3. refreshes the copy of the applied patch kept in the SDK's git directory, so the
       next configure sees the tree as already patched instead of applying the patch
       a second time.

  Nothing in the working tree is reset or deleted.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$pin = '3eb9b511b4140d2769e27be63eae57d41bfa2afa'
$paths = @('src', 'include', 'thirdparty/CMakeLists.txt', 'cmake/rex_version.cmake')
$projectRoot = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $projectRoot 'rexglue-sdk'
$patch = Join-Path $projectRoot 'patches\rexglue.patch'

function Invoke-GitCommand {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git.exe @args 2>&1
        if ($LASTEXITCODE -ne 0) { throw ('git ' + ($args -join ' ') + " failed:`n" + ($output -join "`n")) }
        return $output
    }
    finally { $ErrorActionPreference = $previous }
}

$head = ((Invoke-GitCommand -C $sdk rev-parse HEAD) -join '').Trim()
if ($head -ne $pin) { throw "rexglue-sdk is at $head, not the pinned $pin." }

# 1. The diff. New files need intent-to-add to appear in it.
Invoke-GitCommand -C $sdk add -N src include | Out-Null
$temp = Join-Path ([IO.Path]::GetTempPath()) ('rexglue-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $newPatch = Join-Path $temp 'new.patch'
    Invoke-GitCommand -C $sdk -c core.autocrlf=false diff --binary --full-index "--output=$newPatch" $pin -- @paths | Out-Null
    $files = @(Select-String -LiteralPath $newPatch -Pattern '^diff --git' | ForEach-Object { ($_.Line -split ' ')[2].Substring(2) })
    if ($files.Count -eq 0) { throw 'The SDK has no changes against the pinned commit.' }

    # 2. Proof against a pristine copy of the pinned files.
    $pristine = Join-Path $temp 'pristine'
    $zip = Join-Path $temp 'pin.zip'
    Invoke-GitCommand -C $sdk archive --format=zip "--output=$zip" $pin @paths | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $pristine
    Invoke-GitCommand -C $pristine init -q | Out-Null
    Invoke-GitCommand -C $pristine -c core.autocrlf=false apply --whitespace=nowarn $newPatch | Out-Null
    $differ = @()
    foreach ($relative in $files) {
        $a = Join-Path $pristine $relative
        $b = Join-Path $sdk $relative
        if (-not (Test-Path -LiteralPath $a) -or -not (Test-Path -LiteralPath $b)) { $differ += $relative; continue }
        $x = [IO.File]::ReadAllText($a).Replace("`r`n", "`n")
        $y = [IO.File]::ReadAllText($b).Replace("`r`n", "`n")
        if ($x -ne $y) { $differ += $relative }
    }
    if ($differ.Count -gt 0) { throw "The patch does not reproduce these SDK files: $($differ -join ', ')" }

    # 3. Save it, and tell the next configure that this version is the one applied.
    Copy-Item -LiteralPath $newPatch -Destination $patch -Force
    $gitDir = ((Invoke-GitCommand -C $sdk rev-parse --absolute-git-dir) -join '').Trim()
    Copy-Item -LiteralPath $patch -Destination (Join-Path $gitDir 'aegis-wing-applied.patch') -Force
    Write-Host ("patches\rexglue.patch: {0:N0} bytes, {1} files; applies cleanly to the pinned commit and reproduces the SDK sources." -f (Get-Item -LiteralPath $patch).Length, $files.Count)
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
