<#
    Aegis Wing PC - Setup

    Started by "Setup Aegis Wing.exe" through a hidden Windows PowerShell host,
    from the temporary folder Setup unpacks its payload into. The payload is
    an image of the installed Game folder: the game executable and runtime
    DLLs, resources\ (these scripts and the default settings) and
    release-manifest.json, which lists every one of those files.

    Setup installs that payload, plus the game data from the player's own
    Xbox 360 package, into a Game folder beside Setup (-ReleaseRoot). Game\ is
    only created once the package has been unpacked and verified, and a Game\
    this run created is removed again if a later step fails. The release
    folder itself only ever holds Setup, README.txt, licenses\ and Game\.

    No original game content ships with the release. Everything that ends up in
    Game\assets comes from the package the player selects, and that package is
    only ever opened for reading.

    Running Setup again: the files listed in Game\release-manifest.json belong
    to Setup and are replaced (or removed, when a newer release no longer
    ships them and they are unchanged); Game\assets is rebuilt from the
    package; the player's Game\userdata, Game\aegis_wing.toml and logs are
    kept. Files Setup cannot positively identify as its own are never deleted.

      -ReleaseRoot <dir>    the release folder (Setup passes its own folder)
      -PackagePath <file>   preselect a package (the window still opens)
      -Unattended           install -PackagePath without a window; exit code 0/1
      -Extras a,b           with -Unattended: extras to switch on (stageselect, fullscreen, shortcut)
      -ShortcutFolder <dir> testing: put the desktop shortcut here instead
#>
[CmdletBinding()]
param(
    [string]$ReleaseRoot,
    [string]$PackagePath,
    [switch]$Unattended,
    [string[]]$Extras = @(),
    [string]$ShortcutFolder
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# ------------------------------------------------------------------ paths --
# The payload Setup unpacked: <payload>\resources\installer\<this script>.
$installerDir = $PSScriptRoot
$resourcesDir = Split-Path -Parent $installerDir
$payloadDir = Split-Path -Parent $resourcesDir
$payloadManifest = Join-Path $payloadDir 'release-manifest.json'
$defaultConfig = Join-Path $resourcesDir 'defaults\aegis_wing.toml'
$extractorPath = Join-Path $installerDir 'Extract-STFS.ps1'
$patcherPath = Join-Path $installerDir 'Patch-PcMenuText.ps1'

# The release folder: Setup passes its own folder. "D:" alone would mean the
# current folder on D:, so a drive root gets its backslash back here.
if ([string]::IsNullOrWhiteSpace($ReleaseRoot)) {
    $ReleaseRoot = Split-Path -Parent $payloadDir
}
if ($ReleaseRoot.EndsWith(':')) { $ReleaseRoot += '\' }
$releaseRoot = [System.IO.Path]::GetFullPath($ReleaseRoot)
$setupExe = Join-Path $releaseRoot 'Setup Aegis Wing.exe'
$gameDir = Join-Path $releaseRoot 'Game'
$gameExe = Join-Path $gameDir 'Aegis Wing.exe'
$assetsDir = Join-Path $gameDir 'assets'
$installedManifest = Join-Path $gameDir 'release-manifest.json'
$previousAssetsDir = Join-Path $gameDir '.setup-previous-assets'
$gameLogDir = Join-Path $gameDir 'logs'

# Work files live beside the payload, in Setup's temporary folder, so nothing
# appears in the release folder before the install succeeds.
$workDir = Join-Path $payloadDir '.work'
$stagingDir = Join-Path $workDir 'import'
$progressFile = Join-Path $workDir 'progress'

# Setup logs go to Game\logs once Game\ exists. Until then they go to a fixed
# folder under the user's temp folder, so a failed first install still leaves
# a log; a successful one moves it into Game\logs.
$logName = 'setup-{0}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
$pendingLogDir = Join-Path ([System.IO.Path]::GetTempPath()) 'Aegis Wing Setup logs'
$script:logFile = Join-Path $pendingLogDir $logName
if (Test-Path -LiteralPath $gameDir -PathType Container) {
    $script:logFile = Join-Path $gameLogDir $logName
}
$powershellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

$versionFile = Join-Path $installerDir 'version.txt'
$releaseVersion = 'dev'
if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
    $releaseVersion = (Get-Content -LiteralPath $versionFile -TotalCount 1).Trim()
}

# The port is a static recompilation of one specific build of the game
# program: its code is compiled into Aegis Wing.exe, and the package supplies
# the data that build expects. Only a package carrying that exact build works.
$expectedTitleId = [uint32]0x5841083C
$expectedContentType = [uint32]0x000D0000
$expectedXexSha256 = 'C57F6A8136EC0C7D966ED83717D1F211CACF64F7D756182E7183C416A55F2873'
$requiredGameFiles = @('default.xex', 'Media\Wingmen.xzp', 'Media\UI\OptionSlots.xml')

# Built from code points so this file stays plain ASCII for PowerShell 5.1.
$dot = [string][char]0x00B7
$ellipsis = [string][char]0x2026
$check = [string][char]0x2713

# ---------------------------------------------------------------- logging --
function Write-Log {
    param([string]$Message)
    try {
        $folder = Split-Path -Parent $script:logFile
        if (-not (Test-Path -LiteralPath $folder)) {
            New-Item -ItemType Directory -Path $folder -Force | Out-Null
        }
        $line = '{0}  {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Message
        [System.IO.File]::AppendAllText($script:logFile, $line + [Environment]::NewLine)
    }
    catch {
        # Logging must never be the reason Setup fails.
    }
}

# ------------------------------------------------------ package inspection --
function Read-UInt32BE {
    param([byte[]]$Bytes, [int]$Offset)
    return ([uint32]$Bytes[$Offset] -shl 24) -bor ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor [uint32]$Bytes[$Offset + 3]
}

# Reads only the STFS header: magic, content type, title ID and display name.
function Get-PackageInfo {
    param([string]$Path)

    $info = @{ Ok = $false; Message = ''; Name = ''; Bytes = [long]0 }
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $info.Message = 'The selected file could not be found.'
        return $info
    }

    try {
        $info.Bytes = (Get-Item -LiteralPath $Path).Length
        if ($info.Bytes -lt 0xC000) {
            $info.Message = 'This file is too small to be an Xbox 360 package.'
            return $info
        }

        $header = New-Object byte[] 0x1000
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
        try {
            [void]$stream.Read($header, 0, $header.Length)
        }
        finally {
            $stream.Dispose()
        }
    }
    catch {
        $info.Message = 'The package could not be read: ' + $_.Exception.Message
        return $info
    }

    $magic = [System.Text.Encoding]::ASCII.GetString($header, 0, 4)
    if (@('LIVE', 'PIRS', 'CON') -notcontains $magic.TrimEnd()) {
        $info.Message = 'This is not an Xbox 360 package. Choose the Aegis Wing package file itself.'
        return $info
    }

    $contentType = Read-UInt32BE $header 0x344
    $titleId = Read-UInt32BE $header 0x360
    if ($titleId -ne $expectedTitleId) {
        $info.Message = ('This package is for a different game (title ID {0:X8}). Aegis Wing is 5841083C.' -f $titleId)
        return $info
    }
    if ($contentType -ne $expectedContentType) {
        $info.Message = 'This Aegis Wing package is not the game itself (it may be a title update or a save). Choose the Xbox LIVE Arcade game package.'
        return $info
    }

    $info.Name = [System.Text.Encoding]::BigEndianUnicode.GetString($header, 0x411, 0x80).Trim([char]0).Trim()
    $info.Ok = $true
    return $info
}

# -------------------------------------------------------------- install ---
function Remove-Tree {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Test-GameRunning {
    $running = @(Get-Process -Name 'Aegis Wing' -ErrorAction SilentlyContinue | Where-Object {
            try { $_.Path -and $_.Path.StartsWith($gameDir, [StringComparison]::OrdinalIgnoreCase) }
            catch { $false }
        })
    return $running.Count -gt 0
}

# Reads the small progress file the workers rewrite: the extractor writes
# "doneBytes|totalBytes|doneFiles|totalFiles", the menu patcher "patch|percent".
function Read-WorkerProgress {
    if (-not (Test-Path -LiteralPath $progressFile -PathType Leaf)) {
        return $null
    }
    try {
        $share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
        $stream = [System.IO.File]::Open($progressFile, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, $share)
        try {
            $text = (New-Object System.IO.StreamReader($stream)).ReadToEnd()
        }
        finally {
            $stream.Dispose()
        }
        return $text.Split('|')
    }
    catch {
        # Mid-rewrite; the next poll will get it.
        return $null
    }
}

# Portable builds before this Setup kept the game, saves and settings in the
# folder root. When this release is extracted over one of those, carry the
# saves and settings into Game\ once so nothing is lost.
function Move-LegacyUserData {
    $legacy = Join-Path $releaseRoot 'userdata'
    $target = Join-Path $gameDir 'userdata'
    if ((Test-Path -LiteralPath $legacy -PathType Container) -and -not (Test-Path -LiteralPath $target)) {
        Move-Item -LiteralPath $legacy -Destination $target
        Write-Log 'Moved saves and high scores from an earlier portable build into Game\userdata.'
    }
    $legacyConfig = Join-Path $releaseRoot 'aegis_wing.toml'
    $configTarget = Join-Path $gameDir 'aegis_wing.toml'
    if ((Test-Path -LiteralPath $legacyConfig -PathType Leaf) -and -not (Test-Path -LiteralPath $configTarget)) {
        Move-Item -LiteralPath $legacyConfig -Destination $configTarget
        Write-Log 'Moved the settings from an earlier portable build into Game\aegis_wing.toml.'
    }
}

# ------------------------------------------------------- release manifest --
# release-manifest.json lists every file Setup installs into Game\ (path
# relative to Game\, size, SHA-256). The copy inside Setup's payload says what
# this release installs; the copy in Game\ says what the last install put
# there, which is how a later Setup knows which files are its own.
function Read-Manifest([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        return [System.IO.File]::ReadAllText($Path) | ConvertFrom-Json
    }
    catch {
        Write-Log ('Could not read {0}: {1}' -f $Path, $_.Exception.Message)
        return $null
    }
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

# The unpacked payload must match its manifest exactly, or the download (or
# the temporary copy) is damaged and nothing is installed.
function Test-Payload {
    $manifest = Read-Manifest $payloadManifest
    if (-not $manifest -or -not $manifest.files) {
        throw 'Setup''s install data is incomplete (release-manifest.json is missing). Download the release again.'
    }
    foreach ($entry in $manifest.files) {
        $source = Join-Path $payloadDir $entry.path
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
            (Get-Item -LiteralPath $source).Length -ne [long]$entry.bytes -or
            (Get-FileSha256 $source) -ne $entry.sha256) {
            throw ('Setup''s install data is damaged ({0}). Download the release again.' -f $entry.path)
        }
    }
    return $manifest
}

# Copies this release's files into Game\, then removes files an earlier
# install put there that this release no longer ships - but only when the
# earlier manifest lists them and they are still exactly as installed.
function Install-PayloadFiles($Manifest) {
    $previous = Read-Manifest $installedManifest
    $current = @{}
    foreach ($entry in $Manifest.files) {
        $current[$entry.path.ToLowerInvariant()] = $true
        $target = Join-Path $gameDir $entry.path
        $folder = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $folder)) {
            New-Item -ItemType Directory -Path $folder -Force | Out-Null
        }
        Copy-Item -LiteralPath (Join-Path $payloadDir $entry.path) -Destination $target -Force
    }
    if ($previous -and $previous.files) {
        foreach ($entry in $previous.files) {
            if ($current.ContainsKey($entry.path.ToLowerInvariant())) { continue }
            $old = Join-Path $gameDir $entry.path
            if (-not (Test-Path -LiteralPath $old -PathType Leaf)) { continue }
            if ((Get-FileSha256 $old) -eq $entry.sha256) {
                Remove-Item -LiteralPath $old -Force
                Write-Log ('Removed {0}, which this release no longer uses.' -f $entry.path)
            }
            else {
                Write-Log ('Kept {0}: no longer used, but changed since it was installed.' -f $entry.path)
            }
        }
    }

    # Settings belong to the player: the default is only used when there are none.
    $configTarget = Join-Path $gameDir 'aegis_wing.toml'
    if (Test-Path -LiteralPath $configTarget) {
        Write-Log 'Kept the existing aegis_wing.toml.'
    }
    else {
        Copy-Item -LiteralPath $defaultConfig -Destination $configTarget
    }
}

# Game\release-manifest.json: this release's manifest plus what was installed.
function Write-InstalledManifest($Manifest, $Package, [string]$XexHash) {
    $installed = [ordered]@{
        schema           = $Manifest.schema
        product          = $Manifest.product
        game             = $Manifest.game
        version          = $Manifest.version
        setupExecutable  = $Manifest.setupExecutable
        gameExecutable   = $Manifest.gameExecutable
        installed        = [ordered]@{
            time             = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
            setupVersion     = $releaseVersion
            titleId          = '5841083C'
            packageTitle     = $Package.Name
            defaultXexSha256 = $XexHash
        }
        files            = @($Manifest.files)
        generated        = @($Manifest.generated)
        userData         = @($Manifest.userData)
    }
    $json = $installed | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($installedManifest, $json, (New-Object System.Text.UTF8Encoding($false)))
}

# Move-Item cannot move a folder to another drive (Setup's temporary folder
# may be on C: while the release is on D:); copy it across instead.
function Move-Directory([string]$Source, [string]$Destination) {
    try {
        Move-Item -LiteralPath $Source -Destination $Destination -ErrorAction Stop
    }
    catch {
        Remove-Tree $Destination
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force -ErrorAction Stop
        Remove-Tree $Source
    }
}

# ------------------------------------------------------- old root layout --
# Releases before 0.9 kept a play launcher ("Aegis Wing.exe"), resources\
# (the Setup scripts and the runtime) and logs\ in the release folder. When
# this release is extracted over one of those, Setup removes exactly the files
# it can positively identify as that layout's, moves the old Setup logs into
# Game\logs, and leaves anything else where it is.
function Test-LegacyLauncher([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $file = Get-Item -LiteralPath $Path
    if ($file.Length -gt 2MB) { return $false }
    $info = $file.VersionInfo
    return ($info.ProductName -eq 'Aegis Wing PC') -and ($info.OriginalFilename -eq 'Aegis Wing.exe')
}

function Remove-EmptyFolder([string]$Path) {
    if ((Test-Path -LiteralPath $Path -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $Path -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $Path -Force
    }
}

function Remove-LegacyRootFiles {
    $rootLauncher = Join-Path $releaseRoot 'Aegis Wing.exe'
    $hadLauncher = Test-LegacyLauncher $rootLauncher
    if ($hadLauncher) {
        Remove-Item -LiteralPath $rootLauncher -Force
        Write-Log 'Removed the old play launcher from the release folder (the game is Game\Aegis Wing.exe).'
    }

    $legacyResources = Join-Path $releaseRoot 'resources'
    $known = @{
        'installer' = @('Install-AegisWing.ps1', 'Extract-STFS.ps1', 'Patch-PcMenuText.ps1', 'version.txt')
        'game'      = @('Aegis Wing.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'rexruntimerd.dll',
            'rexgpu-xenosrd.dll', 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll',
            'vcruntime140_1.dll', 'aegis_wing.toml')
    }
    foreach ($sub in $known.Keys) {
        $folder = Join-Path $legacyResources $sub
        if (-not (Test-Path -LiteralPath $folder -PathType Container)) { continue }
        foreach ($name in $known[$sub]) {
            $file = Join-Path $folder $name
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                Remove-Item -LiteralPath $file -Force
                Write-Log ('Removed the old resources\{0}\{1} from the release folder.' -f $sub, $name)
            }
        }
        # The old installer folder also carried a copy of the play launcher.
        $launcherCopy = Join-Path $folder 'Aegis Wing.exe'
        if (Test-LegacyLauncher $launcherCopy) { Remove-Item -LiteralPath $launcherCopy -Force }
        Remove-EmptyFolder $folder
    }
    Remove-EmptyFolder $legacyResources

    $rootLogs = Join-Path $releaseRoot 'logs'
    if (Test-Path -LiteralPath $rootLogs -PathType Container) {
        $oldLogs = @(Get-ChildItem -LiteralPath $rootLogs -File -Filter 'setup-*.log')
        if ($oldLogs.Count -gt 0) {
            New-Item -ItemType Directory -Path $gameLogDir -Force | Out-Null
            foreach ($log in $oldLogs) {
                $target = Join-Path $gameLogDir $log.Name
                if (-not (Test-Path -LiteralPath $target)) { Move-Item -LiteralPath $log.FullName -Destination $target }
            }
            Write-Log ('Moved {0} old Setup log(s) into Game\logs.' -f $oldLogs.Count)
        }
        Remove-EmptyFolder $rootLogs
    }

    # A desktop shortcut made by an old Setup pointed at the root launcher.
    if ($hadLauncher -and (Test-OurShortcut)) {
        Set-GameShortcut
        Write-Log 'Pointed the desktop shortcut at Game\Aegis Wing.exe.'
    }
}

# Once Game\ exists the Setup log belongs in Game\logs.
function Complete-SetupLog {
    $target = Join-Path $gameLogDir $logName
    if ($script:logFile -eq $target) { return }
    try {
        New-Item -ItemType Directory -Path $gameLogDir -Force | Out-Null
        if (Test-Path -LiteralPath $script:logFile) {
            Move-Item -LiteralPath $script:logFile -Destination $target -Force
        }
        $script:logFile = $target
    }
    catch {
        # Keep logging where it was.
    }
}

# Runs one worker script in a hidden PowerShell and polls its progress file
# until it exits. $OnPoll receives the parsed progress each tick.
function Invoke-Worker {
    param([string]$Arguments, [string]$Name, [scriptblock]$OnPoll)

    $out = Join-Path $workDir ('{0}-out.txt' -f $Name)
    $err = Join-Path $workDir ('{0}-err.txt' -f $Name)
    $process = Start-Process -FilePath $powershellExe -ArgumentList $Arguments -WindowStyle Hidden `
        -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    $null = $process.Handle  # keeps ExitCode readable once it exits (PowerShell 5.1)
    try {
        while (-not $process.HasExited) {
            & $OnPoll (Read-WorkerProgress)
            Start-Sleep -Milliseconds 60
        }
        $process.WaitForExit()
        foreach ($captured in @($out, $err)) {
            if (Test-Path -LiteralPath $captured) {
                Get-Content -LiteralPath $captured | ForEach-Object { Write-Log ('  {0}: {1}' -f $Name, $_) }
            }
        }
        return $process.ExitCode
    }
    finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        Remove-Tree $out
        Remove-Tree $err
    }
}

# Installs into Game\. $Report receives (percent, phase, detail) as it goes.
function Invoke-AegisInstall {
    param(
        [Parameter(Mandatory = $true)] [string]$PackagePath,
        [scriptblock]$Report
    )

    $script:lastPhase = ''
    function Step([int]$Percent, [string]$Phase, [string]$Detail) {
        if ($Phase -ne $script:lastPhase) {
            Write-Log ('{0} ({1}%)' -f $Phase, $Percent)
            $script:lastPhase = $Phase
        }
        if ($Report) {
            & $Report $Percent $Phase $Detail
        }
    }

    Write-Log ('Setup {0}: installing from {1} into {2}' -f $releaseVersion, $PackagePath, $gameDir)
    Step 1 'PREPARING' ('Checking Setup''s files' + $ellipsis)
    $manifest = Test-Payload

    $package = Get-PackageInfo $PackagePath
    if (-not $package.Ok) {
        throw $package.Message
    }
    if (Test-GameRunning) {
        throw 'Aegis Wing is running from this folder. Close the game, then install again.'
    }

    # Unpacking happens in Setup's temporary folder; Game\ is only touched
    # once the package is known to be the right one.
    Step 2 'PREPARING' ('Getting ready to unpack' + $ellipsis)
    Remove-Tree $workDir
    New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
    $gameCreated = $false

    $succeeded = $false
    try {
        # 1. Unpack into a staging folder. The extractor opens the package read-only.
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Path "{1}" -OutputDir "{2}" -ProgressPath "{3}"' -f
            $extractorPath, $PackagePath, $stagingDir, $progressFile
        $exitCode = Invoke-Worker $arguments 'extractor' {
            param($state)
            if ($state -and $state.Count -eq 4 -and [long]$state[1] -gt 0) {
                $fraction = [Math]::Min(1.0, [long]$state[0] / [double]$state[1])
                # Bands follow real durations: unpacking a 46 MB package
                # takes a few seconds, the menu update about thirty.
                Step ([int](5 + 20 * $fraction)) 'UNPACKING YOUR GAME DATA' (
                    '{0} of {1} files  {4}  {2:N1} of {3:N1} MB' -f $state[2], $state[3],
                    ([long]$state[0] / 1MB), ([long]$state[1] / 1MB), $dot)
            }
            else {
                Step 5 'UNPACKING YOUR GAME DATA' ('Reading the package file table' + $ellipsis)
            }
        }
        if ($exitCode -ne 0) {
            throw ('The package could not be unpacked (extractor exit code {0}). The file may be damaged or incomplete.' -f $exitCode)
        }

        # 2. Check it is the build this port was recompiled from.
        Step 25 'CHECKING THE GAME DATA' ('Verifying the unpacked files' + $ellipsis)
        foreach ($relative in $requiredGameFiles) {
            if (-not (Test-Path -LiteralPath (Join-Path $stagingDir $relative) -PathType Leaf)) {
                throw ('The package unpacked, but {0} is missing from it. The package may be damaged.' -f $relative)
            }
        }
        $xexHash = (Get-FileHash -LiteralPath (Join-Path $stagingDir 'default.xex') -Algorithm SHA256).Hash
        Write-Log ('default.xex SHA-256 ' + $xexHash)
        if ($xexHash -ne $expectedXexSha256) {
            throw ('This Aegis Wing package carries a different build of the game program than the one this port was made from, so it cannot be used. Nothing was changed. (default.xex SHA-256 {0})' -f $xexHash)
        }

        # 3. PC menu wording and layout fixes inside the menu archive.
        Step 28 'PREPARING THE PC MENUS' ('Updating the menus for PC' + $ellipsis)
        Remove-Tree $progressFile
        $archive = Join-Path $stagingDir 'Media\Wingmen.xzp'
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -ArchivePath "{1}" -ProgressPath "{2}"' -f
            $patcherPath, $archive, $progressFile
        # The patcher reports 0,4,..,20 about six seconds apart (its slow
        # string scans), then 20..100 almost instantly, so its 0-20 is the
        # whole step. Between checkpoints the estimate glides toward the next
        # one but never reaches it, so the ships keep moving without ever
        # running ahead of real progress by more than one checkpoint.
        $script:menuShare = 0.0
        $script:menuSince = [System.Diagnostics.Stopwatch]::StartNew()
        $exitCode = Invoke-Worker $arguments 'menus' {
            param($state)
            if ($state -and $state.Count -eq 2 -and $state[0] -eq 'patch') {
                $share = [Math]::Min(1.0, [Math]::Max(0, [int]$state[1]) / 20.0)
                if ($share -gt $script:menuShare) {
                    $script:menuShare = $share
                    $script:menuSince.Restart()
                }
            }
            $glide = [Math]::Min(0.19, 0.2 * $script:menuSince.Elapsed.TotalSeconds / 6.0)
            $estimate = [Math]::Min(0.99, $script:menuShare + $glide)
            Step ([int](28 + 65 * $estimate)) 'PREPARING THE PC MENUS' ('Updating the menus for PC' + $ellipsis)
        }
        if ($exitCode -ne 0) {
            throw 'The PC menu update could not be applied. Nothing was changed.'
        }

        # 4. Game\ and the files Setup owns: the game, its runtime DLLs,
        # resources\ and the default settings (only used when there are none,
        # so a reinstall keeps the player's options).
        Step 94 'INSTALLING AEGIS WING' ('Copying the PC runtime' + $ellipsis)
        if (-not (Test-Path -LiteralPath $gameDir -PathType Container)) {
            New-Item -ItemType Directory -Path $gameDir -Force | Out-Null
            $gameCreated = $true
            Write-Log ('Created {0}' -f $gameDir)
        }
        Install-PayloadFiles $manifest

        # 5. Swap the game data in, restoring the previous copy if the move fails.
        Step 97 'INSTALLING AEGIS WING' ('Moving the game data into place' + $ellipsis)
        Remove-Tree $previousAssetsDir
        if (Test-Path -LiteralPath $assetsDir) {
            Move-Item -LiteralPath $assetsDir -Destination $previousAssetsDir
        }
        try {
            Move-Directory $stagingDir $assetsDir
        }
        catch {
            if (Test-Path -LiteralPath $previousAssetsDir) {
                Remove-Tree $assetsDir
                Move-Item -LiteralPath $previousAssetsDir -Destination $assetsDir
            }
            throw
        }
        Remove-Tree $previousAssetsDir
        New-Item -ItemType Directory -Path (Join-Path $gameDir 'userdata') -Force | Out-Null

        # 6. Record what is installed, then tidy the release folder: saves and
        # settings from an old portable build move in, and the files of the
        # old root layout (launcher, resources\, logs\) go.
        Write-InstalledManifest $manifest $package $xexHash
        Move-LegacyUserData
        Remove-LegacyRootFiles
        # Keyboard controls are always on. Older Setups offered them as an
        # extra, so a kept settings file may still have them switched off.
        Set-ConfigValue 'mnk_mode' 'true'
        # A player without a name of their own gets a random one from the
        # list; it can be changed in Help & Options, Settings.
        if ([string]::IsNullOrWhiteSpace((Get-ConfigValue 'net_player_name'))) {
            $name = Get-RandomPlayerName
            if ($name) {
                Set-ConfigValue 'net_player_name' ('"' + $name + '"')
                Write-Log "Player name: $name"
            }
        }
        Complete-SetupLog

        Step 100 'AEGIS WING IS READY' 'Installed in the Game folder.'
        $succeeded = $true
        return $package
    }
    finally {
        Remove-Tree $workDir
        if (-not $succeeded -and $gameCreated) {
            # Game\ did not exist before this run, so everything in it is
            # Setup's own: leave the release folder as it was.
            Remove-Tree $gameDir
            Write-Log 'Removed the incomplete Game folder.'
        }
    }
}

function Start-Game {
    if (Test-Path -LiteralPath $gameExe -PathType Leaf) {
        Start-Process -FilePath $gameExe -WorkingDirectory $gameDir
    }
}

# ----------------------------------------------------------------- extras --
# Optional features offered after the install. Fullscreen is a key in
# Game\aegis_wing.toml, which the game reads when it starts; Setup applies the
# stage select unlock (a flag in the save) and the desktop shortcut itself.
# None is on after a fresh install. The ticks show what is set now, so
# re-running Setup shows the current choices.
$configPath = Join-Path $gameDir 'aegis_wing.toml'
$shortcutName = 'Aegis Wing.lnk'
$extraDefinitions = @(
    @{ Id = 'stageselect'; Title = 'Unlock Stage Select'; Short = 'Stage Select'
        Detail = 'Unlocked by completing the game on Insane.'; Setting = $null },
    @{ Id = 'fullscreen'; Title = 'Start in fullscreen'; Short = 'Fullscreen'
        Detail = 'Switch at any time from Help and Options, Settings.'; Setting = 'fullscreen' },
    @{ Id = 'shortcut'; Title = 'Desktop shortcut'; Short = 'Desktop shortcut'
        Detail = 'Adds an Aegis Wing icon to your desktop. It stops working if you move this folder.'; Setting = $null }
)

# Aegis Wing keeps its options in one 8-byte title setting (id 63E83FFF) per
# profile: bytes 4 and 5 are the sound and music volumes, and byte 6 is set
# once the game has been completed on Insane, which unlocks the level
# selector on the single-player options screen. ReXGlue's local profile is
# always named "User".
$profileRoot = Join-Path $gameDir 'userdata\5841083C\profile'
$stageSelectByte = 6
$defaultSettingsRecord = [byte[]](0, 0, 0, 0, 0x7F, 0x7F, 0, 0)

function Get-SettingsRecords {
    if (-not (Test-Path -LiteralPath $profileRoot -PathType Container)) { return @() }
    return @(Get-ChildItem -LiteralPath $profileRoot -Directory | ForEach-Object {
            Join-Path $_.FullName '63E83FFF'
        } | Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and (Get-Item -LiteralPath $_).Length -eq 8 })
}

function Test-StageSelectUnlocked {
    foreach ($path in Get-SettingsRecords) {
        if ([System.IO.File]::ReadAllBytes($path)[$stageSelectByte] -ne 0) { return $true }
    }
    return $false
}

# Unlocking writes the flag into every profile's record, creating the default
# profile's record (game defaults otherwise) when there is none yet; locking
# clears it again. Volumes and anything else in the record are kept.
function Set-StageSelect([bool]$On) {
    $records = @(Get-SettingsRecords)
    if ($On -and $records.Count -eq 0) {
        $folder = Join-Path $profileRoot 'User'
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        $record = [byte[]]$defaultSettingsRecord.Clone()
        $record[$stageSelectByte] = 1
        [System.IO.File]::WriteAllBytes((Join-Path $folder '63E83FFF'), $record)
        return
    }
    foreach ($path in $records) {
        $record = [System.IO.File]::ReadAllBytes($path)
        $record[$stageSelectByte] = $(if ($On) { 1 } else { 0 })
        [System.IO.File]::WriteAllBytes($path, $record)
    }
}

function Get-ShortcutPath {
    $folder = if ($ShortcutFolder) { $ShortcutFolder } else { [Environment]::GetFolderPath('Desktop') }
    return Join-Path $folder $shortcutName
}

function Get-ConfigValue([string]$Key) {
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { return $null }
    foreach ($line in [System.IO.File]::ReadAllLines($configPath)) {
        if ($line -match ('^\s*' + [regex]::Escape($Key) + '\s*=\s*(.+?)\s*$')) { return $Matches[1].Trim('"') }
    }
    return $null
}

# Replaces the key's line, or appends it; every other line is left as it was.
function Set-ConfigValue([string]$Key, [string]$Value) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
        $lines.AddRange([string[]][System.IO.File]::ReadAllLines($configPath))
    }
    $pattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    $found = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $pattern) { $lines[$i] = "$Key = $Value"; $found = $true }
    }
    if (-not $found) { $lines.Add("$Key = $Value") }
    [System.IO.File]::WriteAllLines($configPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

# packaging\player_names.txt, shipped in resources\defaults: one name per
# line, # starts a comment. The game builds its Player Name list from the
# same file.
$playerNamesPath = Join-Path $resourcesDir 'defaults\player_names.txt'

function Get-RandomPlayerName {
    if (-not (Test-Path -LiteralPath $playerNamesPath -PathType Leaf)) { return $null }
    $names = @([System.IO.File]::ReadAllLines($playerNamesPath) | ForEach-Object { $_.Trim() } |
            Where-Object { $_ -and -not $_.StartsWith('#') -and $_.Length -le 15 -and $_ -notmatch '["\\]' })
    if ($names.Count -eq 0) { return $null }
    return $names[(Get-Random -Maximum $names.Count)]
}

# True only for a shortcut that starts this install's game - or the play
# launcher an older Setup put in the release folder - so an unrelated
# "Aegis Wing" shortcut is never touched.
function Test-OurShortcut {
    $path = Get-ShortcutPath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    try {
        $shell = New-Object -ComObject WScript.Shell
        $target = $shell.CreateShortcut($path).TargetPath
        foreach ($ours in @($gameExe, (Join-Path $releaseRoot 'Aegis Wing.exe'))) {
            if ([string]::Equals($target, $ours, [StringComparison]::OrdinalIgnoreCase)) { return $true }
        }
        return $false
    }
    catch {
        return $false
    }
}

# The shortcut starts Game\Aegis Wing.exe with Game\ as its working folder.
function Set-GameShortcut {
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut((Get-ShortcutPath))
    $link.TargetPath = $gameExe
    $link.WorkingDirectory = $gameDir
    $link.IconLocation = $gameExe + ',0'
    $link.Description = 'Aegis Wing (XBLA Recomp)'
    $link.Save()
}

function Get-ExtraState([string]$Id) {
    $def = $extraDefinitions | Where-Object { $_.Id -eq $Id }
    if ($def.Setting) { return (Get-ConfigValue $def.Setting) -eq 'true' }
    if ($Id -eq 'stageselect') { return (Test-StageSelectUnlocked) }
    return (Test-OurShortcut)
}

# $Chosen holds the ids of the ticked extras. Every extra is written, so
# unticking one turns it back off.
function Set-Extras([string[]]$Chosen) {
    foreach ($def in $extraDefinitions) {
        $on = $Chosen -contains $def.Id
        if ($def.Setting) {
            Set-ConfigValue $def.Setting $(if ($on) { 'true' } else { 'false' })
        }
        elseif ($def.Id -eq 'stageselect') {
            # Only touch the save when the choice changes, so a player who
            # earned the unlock keeps it untouched.
            if ($on -ne (Test-StageSelectUnlocked)) { Set-StageSelect $on }
        }
        elseif ($def.Id -eq 'shortcut') {
            $path = Get-ShortcutPath
            if ($on) {
                Set-GameShortcut
            }
            elseif (Test-OurShortcut) {
                Remove-Item -LiteralPath $path -Force
            }
        }
        Write-Log ('Extra {0}: {1}' -f $def.Id, $(if ($on) { 'on' } else { 'off' }))
    }
}

# ------------------------------------------------------------ unattended --
if ($Unattended) {
    if ([string]::IsNullOrWhiteSpace($PackagePath)) {
        Write-Log 'Unattended install needs -PackagePath.'
        exit 2
    }
    try {
        [void](Invoke-AegisInstall -PackagePath $PackagePath)
        Write-Log 'Install finished.'
        # From a command line "-Extras a,b" arrives as one string.
        $chosen = @($Extras | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if ($chosen.Count -gt 0) { Set-Extras $chosen }
        exit 0
    }
    catch {
        Write-Log ('FAILED: ' + $_.Exception.Message)
        [Console]::Error.WriteLine($_.Exception.Message)
        exit 1
    }
}

# --------------------------------------------------------------- window ---
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace AegisSetup -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool ReleaseCapture();
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);
'@
[void][AegisSetup.Native]::SetProcessDPIAware()
[System.Windows.Forms.Application]::EnableVisualStyles()

# Layout is written in 96-DPI units and scaled, so the window stays sharp on
# high-DPI displays instead of being bitmap-stretched by Windows.
$probe = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero)
$script:scale = $probe.DpiX / 96.0
$probe.Dispose()
function Px([double]$Value) { return [int][Math]::Round($Value * $script:scale) }
function Color([string]$Hex) { return [System.Drawing.ColorTranslator]::FromHtml($Hex) }
function Alpha([int]$A, [string]$Hex) { return [System.Drawing.Color]::FromArgb($A, (Color $Hex)) }
function Mix($A, $B, [double]$T) {
    $ca = if ($A -is [string]) { Color $A } else { $A }
    $cb = if ($B -is [string]) { Color $B } else { $B }
    return [System.Drawing.Color]::FromArgb(
        [int]($ca.R + ($cb.R - $ca.R) * $T), [int]($ca.G + ($cb.G - $ca.G) * $T), [int]($ca.B + ($cb.B - $ca.B) * $T))
}

# "Formation run": a side-scrolling stage over the orange Europa nebula,
# rebuilt from primitives (no game artwork). The four player ships fly linked
# along a gold rail through the steps; the controls sit in one dark glass
# panel below. Colours were sampled from the game's own screens.
$palette = @{
    Gold = '#E4B56C'; GoldHi = '#FFE2B0'; Ember = '#E08A3A'; Cream = '#FFF1DA'; Sand = '#C9AE8E'
    Dim = '#8C7358'; Ok = '#9BE3A0'; Bad = '#FF8F80'; Beam = '#9CE6FF'
}
$shipColors = @{ Red = '#FF5A6E'; Blue = '#4E8DF5'; Green = '#46C85A'; Yellow = '#F2CF3A' }
$W = 720
$H = 560

# Geometry (96-DPI units).
$rail = @{ X0 = 70.0; X1 = 650.0; Y = 236.0 }
$panel = @{ X = 40; Y = 300; W = 640; H = 222; R = 16 }
$fieldBox = @{ X = 64; Y = 376; W = 462; H = 40 }
$stageNames = @('PACKAGE', 'INSTALL', 'EXTRAS', 'PLAY')
$extrasTop = 352
$rowPitch = 38

function New-RoundedPath([single]$X, [single]$Y, [single]$Width, [single]$Height, [single]$Radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Max([single]1, [Math]::Min($Radius * 2, [Math]::Min($Width, $Height)))
    $path.AddArc($X, $Y, $d, $d, 180, 90)
    $path.AddArc($X + $Width - $d, $Y, $d, $d, 270, 90)
    $path.AddArc($X + $Width - $d, $Y + $Height - $d, $d, $d, 0, 90)
    $path.AddArc($X, $Y + $Height - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return , $path
}

# Multi-stop gradient. The rectangle is padded by a pixel because GDI+ wraps
# the first colour onto the last row otherwise.
function New-Blend([double]$X, [double]$Y, [double]$Width, [double]$Height, [object[]]$Colors, [single[]]$Positions, [single]$Angle = 90) {
    $rect = New-Object System.Drawing.RectangleF(($X - 1), ($Y - 1), ($Width + 2), ($Height + 2))
    $resolved = [System.Drawing.Color[]]@($Colors | ForEach-Object { if ($_ -is [string]) { Color $_ } else { $_ } })
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $resolved[0], $resolved[-1], $Angle)
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend($resolved.Count)
    $blend.Colors = $resolved
    $blend.Positions = $Positions
    $brush.InterpolationColors = $blend
    return $brush
}

# A soft radial glow: full colour at the centre, transparent at the edge.
function Paint-Glow($g, [double]$Cx, [double]$Cy, [double]$Rx, [double]$Ry, [int]$A, [string]$Hex) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse([single]($Cx - $Rx), [single]($Cy - $Ry), [single]($Rx * 2), [single]($Ry * 2))
    $brush = New-Object System.Drawing.Drawing2D.PathGradientBrush($path)
    $brush.CenterColor = Alpha $A $Hex
    $brush.SurroundColors = [System.Drawing.Color[]]@((Alpha 0 $Hex))
    $g.FillPath($brush, $path)
    $brush.Dispose(); $path.Dispose()
}

# The ship silhouette used throughout Setup and in the icon: an arrowhead
# pointing right, with a cockpit glint and a small engine glow behind it.
function Paint-Ship($g, [double]$Cx, [double]$Cy, [double]$Length, [string]$Hex) {
    Paint-Glow $g ($Cx - $Length * 0.55) $Cy ($Length * 0.32) ($Length * 0.16) 170 $palette.Ember
    $h = $Length * 0.55
    $points = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF(($Cx - $Length / 2), $Cy)),
        (New-Object System.Drawing.PointF(($Cx + $Length * 0.18), ($Cy - $h / 2))),
        (New-Object System.Drawing.PointF(($Cx + $Length / 2), $Cy)),
        (New-Object System.Drawing.PointF(($Cx + $Length * 0.18), ($Cy + $h / 2))))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddPolygon($points)
    $fill = New-Blend ($Cx - $Length / 2) ($Cy - $h / 2) $Length $h @((Mix $Hex '#FFFFFF' 0.45), $Hex, (Mix $Hex '#000000' 0.4)) @(0, 0.5, 1)
    $g.FillPath($fill, $path)
    $pen = New-Object System.Drawing.Pen((Mix $Hex '#000000' 0.65), [single]1)
    $g.DrawPath($pen, $path)
    $glint = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF(($Cx - $Length * 0.12), $Cy)),
        (New-Object System.Drawing.PointF(($Cx + $Length * 0.2), ($Cy - $h * 0.18))),
        (New-Object System.Drawing.PointF(($Cx + $Length * 0.2), ($Cy + $h * 0.18))))
    $shine = New-Object System.Drawing.SolidBrush((Alpha 170 '#FFFFFF'))
    $g.FillPolygon($shine, $glint)
    foreach ($o in @($shine, $pen, $fill, $path)) { $o.Dispose() }
}

# The first installed family wins; GDI+ silently substitutes a missing one,
# so the returned name is checked.
function New-PixelFont([string[]]$Families, [single]$Pixels, [System.Drawing.FontStyle]$Style = [System.Drawing.FontStyle]::Regular) {
    foreach ($family in $Families) {
        try {
            $font = New-Object System.Drawing.Font($family, $Pixels, $Style, [System.Drawing.GraphicsUnit]::Pixel)
            if ($font.Name -eq $family) { return $font }
            $font.Dispose()
        }
        catch { }
    }
    return New-Object System.Drawing.Font('Segoe UI', $Pixels, $Style, [System.Drawing.GraphicsUnit]::Pixel)
}

# Painted text runs under the DPI scale transform, so it is sized in 96-DPI
# pixels rather than points, which Windows would scale a second time.
$script:wordFont = New-PixelFont @('Bahnschrift SemiBold Condensed', 'Bahnschrift Condensed', 'Bahnschrift', 'Arial Narrow') 52 ([System.Drawing.FontStyle]::Bold)
$script:tagFont = New-PixelFont @('Bahnschrift SemiBold Condensed', 'Bahnschrift', 'Segoe UI Semibold') 13
$script:stationFont = New-PixelFont @('Bahnschrift SemiBold Condensed', 'Bahnschrift', 'Segoe UI Semibold') 13
$script:buttonFont = New-PixelFont @('Bahnschrift SemiBold', 'Bahnschrift', 'Segoe UI Semibold') 15
$script:checkFont = New-PixelFont @('Segoe UI') 14 ([System.Drawing.FontStyle]::Bold)
$script:centerFormat = New-Object System.Drawing.StringFormat
$script:centerFormat.Alignment = [System.Drawing.StringAlignment]::Center
$script:centerFormat.LineAlignment = [System.Drawing.StringAlignment]::Center
$script:rightFormat = New-Object System.Drawing.StringFormat
$script:rightFormat.Alignment = [System.Drawing.StringAlignment]::Far

# "AEGIS WING" in a sheared condensed face, gold from pale to bronze, with a
# dark edge and a drop line - drawn as outlines from a system font.
function Draw-Wordmark($g, [single]$X, [single]$Bottom) {
    $word = New-Object System.Drawing.Drawing2D.GraphicsPath
    # The lean below makes neighbouring glyphs overlap (notably "AE"). Those
    # overlaps must merge into one shape: the default alternate fill rule
    # treats them as holes and punches gaps out of the letters.
    $word.FillMode = [System.Drawing.Drawing2D.FillMode]::Winding
    $format = [System.Drawing.StringFormat]::GenericTypographic
    $word.AddString('AEGIS WING', $script:wordFont.FontFamily, [int]$script:wordFont.Style, [single]56,
        (New-Object System.Drawing.PointF(0, 0)), $format)
    $b = $word.GetBounds()
    $m = New-Object System.Drawing.Drawing2D.Matrix
    $m.Translate([single]($X - $b.X), [single]($Bottom - ($b.Y + $b.Height)))
    $word.Transform($m)
    $m.Dispose()
    # Italic lean: x' = x - 0.18 (y - bottom), so the baseline stays put.
    $lean = New-Object System.Drawing.Drawing2D.Matrix([single]1, [single]0, [single]-0.18, [single]1, [single](0.18 * $Bottom), [single]0)
    $word.Transform($lean)
    $lean.Dispose()
    $wb = $word.GetBounds()

    $shadow = $word.Clone()
    $drop = New-Object System.Drawing.Drawing2D.Matrix
    $drop.Translate(0, 3)
    $shadow.Transform($drop)
    $shadowBrush = New-Object System.Drawing.SolidBrush((Alpha 200 '#1A0803'))
    $g.FillPath($shadowBrush, $shadow)
    $gold = New-Blend $wb.X $wb.Y $wb.Width $wb.Height @('#FFF3D6', '#F0C987', '#D8A460', '#9A5220') @(0, 0.35, 0.55, 1)
    $edge = New-Object System.Drawing.Pen((Color '#3A1A06'), [single]1.1)
    # Stroke first and fill over it, so the outline is never drawn across a
    # seam where two letters overlap.
    $g.DrawPath($edge, $word)
    $g.FillPath($gold, $word)
    foreach ($o in @($edge, $gold, $shadowBrush, $drop, $shadow, $word)) { $o.Dispose() }
}

# Everything that never changes is drawn once into a bitmap at the real pixel
# size; Paint then only adds the rail, the formation, the field and buttons.
function New-Backdrop {
    $bitmap = New-Object System.Drawing.Bitmap((Px $W), (Px $H))
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.ScaleTransform([single]$script:scale, [single]$script:scale)
    $g.Clear((Color '#050304'))

    # The Europa nebula: a warm bank up and to the right, a faint ember low left.
    Paint-Glow $g 560 205 360 250 255 '#6A2A0E'
    Paint-Glow $g 585 190 230 160 210 '#B9551D'
    Paint-Glow $g 600 180 110 80 120 '#F08A3A'
    Paint-Glow $g 110 470 260 180 150 '#5A230C'

    # Stars from a fixed seed, so every launch shows the same sky.
    $seed = 1337
    for ($i = 0; $i -lt 150; $i++) {
        $seed = ($seed * 1103515245 + 12345) % 2147483648
        $sx = ($seed % 7200) / 10.0
        $seed = ($seed * 1103515245 + 12345) % 2147483648
        $sy = ($seed % 3000) / 10.0
        $seed = ($seed * 1103515245 + 12345) % 2147483648
        $size = 0.6 + ($seed % 100) / 100.0
        $alpha = 90 + ($seed % 150)
        $star = New-Object System.Drawing.SolidBrush((Alpha $alpha '#FFF6EA'))
        $g.FillEllipse($star, [single]$sx, [single]$sy, [single]$size, [single]$size)
        $star.Dispose()
    }

    Draw-Wordmark $g 42 80
    $tagBrush = New-Object System.Drawing.SolidBrush((Color '#E8C9A0'))
    $g.DrawString(('SETUP  ' + $dot + '  V' + $releaseVersion.ToUpperInvariant()), $script:tagFont, $tagBrush,
        (New-Object System.Drawing.RectangleF(46, 88, 360, 18)))
    $mutedBrush = New-Object System.Drawing.SolidBrush((Color $palette.Sand))
    $g.DrawString('XBLA RECOMP',$script:tagFont, $mutedBrush, (New-Object System.Drawing.RectangleF(430, 36, 250, 18)), $script:rightFormat)
    $g.DrawString('TITLE 5841083C', $script:tagFont, $mutedBrush, (New-Object System.Drawing.RectangleF(430, 54, 250, 18)), $script:rightFormat)
    $tagBrush.Dispose(); $mutedBrush.Dispose()

    # The unlit rail.
    $track = New-Blend ($rail.X0 - 20) ($rail.Y - 1) ($rail.X1 - $rail.X0 + 40) 3 @((Alpha 0 $palette.Gold), (Alpha 120 $palette.Gold), (Alpha 120 $palette.Gold), (Alpha 0 $palette.Gold)) @(0, 0.06, 0.94, 1) 0
    $g.FillRectangle($track, [single]($rail.X0 - 20), [single]($rail.Y - 1), [single]($rail.X1 - $rail.X0 + 40), [single]2)
    $track.Dispose()

    # Dark glass panel.
    $panelPath = New-RoundedPath $panel.X $panel.Y $panel.W $panel.H $panel.R
    $glass = New-Blend $panel.X $panel.Y $panel.W $panel.H @((Alpha 232 '#160B06'), (Alpha 244 '#0A0503')) @(0, 1)
    $g.FillPath($glass, $panelPath)
    $rim = New-Object System.Drawing.Pen((Alpha 90 $palette.Gold), [single]1)
    $g.DrawPath($rim, $panelPath)
    $hi = New-Object System.Drawing.Pen((Alpha 40 $palette.GoldHi), [single]1)
    $g.DrawLine($hi, [single]($panel.X + 18), [single]($panel.Y + 1), [single]($panel.X + $panel.W - 18), [single]($panel.Y + 1))
    foreach ($o in @($hi, $rim, $glass, $panelPath)) { $o.Dispose() }

    $outline = New-RoundedPath 0.5 0.5 ($W - 1) ($H - 1) 18
    $outlinePen = New-Object System.Drawing.Pen((Alpha 110 $palette.Gold), [single]1)
    $g.DrawPath($outlinePen, $outline)
    $outlinePen.Dispose(); $outline.Dispose()

    $g.Dispose()
    return $bitmap
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Aegis Wing Setup'
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
$form.AutoScaleMode = [System.Windows.Forms.AutoScaleMode]::None
$form.ClientSize = New-Object System.Drawing.Size((Px $W), (Px $H))
$form.BackColor = Color '#050304'
$form.KeyPreview = $true
$form.AllowDrop = $true
$form.ShowInTaskbar = $true
$form.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'NonPublic,Instance').SetValue($form, $true, $null)
$form.Region = New-Object System.Drawing.Region((New-RoundedPath 0 0 (Px $W) (Px $H) (Px 18)))
if (Test-Path -LiteralPath $setupExe -PathType Leaf) {
    try { $form.Icon = [System.Drawing.Icon]::ExtractAssociatedIcon($setupExe) } catch { }
}

$fontTitle = New-Object System.Drawing.Font('Segoe UI Semibold', 14)
$fontCaps = New-Object System.Drawing.Font('Segoe UI Semibold', 8.25)
$fontBody = New-Object System.Drawing.Font('Segoe UI', 10)
$fontSmall = New-Object System.Drawing.Font('Segoe UI', 9)
$fontMono = New-Object System.Drawing.Font('Consolas', 9)
$fontRowTitle = New-Object System.Drawing.Font('Segoe UI Semibold', 10)
$fontRowDetail = New-Object System.Drawing.Font('Segoe UI', 8.5)

function New-Label([string]$Text, [double]$X, [double]$Y, [double]$Width, [double]$Height,
    [System.Drawing.Font]$Font, [string]$ColorHex) {
    $label = New-Object System.Windows.Forms.Label
    $label.AutoSize = $false
    $label.Location = New-Object System.Drawing.Point((Px $X), (Px $Y))
    $label.Size = New-Object System.Drawing.Size((Px $Width), (Px $Height))
    $label.Font = $Font
    $label.ForeColor = Color $ColorHex
    $label.BackColor = [System.Drawing.Color]::Transparent
    $label.UseMnemonic = $false
    $label.Text = $Text
    $form.Controls.Add($label)
    return $label
}

# Buttons are painted onto the form and hit-tested in 96-DPI units.
$script:buttons = @{}
$script:buttonOrder = @('choose', 'extras', 'left', 'right')
$script:hoverId = $null
$script:pressId = $null
function New-PaintedButton([string]$Id, [double]$X, [double]$Y, [double]$Width, [double]$Height, [string]$Text, [string]$Style) {
    $script:buttons[$Id] = @{ X = $X; Y = $Y; W = $Width; H = $Height; Text = $Text; Style = $Style; Visible = $true }
}
New-PaintedButton 'choose' 538 376 118 40 ('Browse' + $ellipsis) 'secondary'
New-PaintedButton 'extras' 296 474 120 36 'Change extras' 'secondary'
New-PaintedButton 'left' 426 474 110 36 'Close' 'secondary'
New-PaintedButton 'right' 546 474 110 36 'Install game' 'disabled'

function Invalidate-Box([double]$X, [double]$Y, [double]$Width, [double]$Height) {
    $form.Invalidate((New-Object System.Drawing.Rectangle((Px ($X - 4)), (Px ($Y - 4)), (Px ($Width + 8)), (Px ($Height + 8)))))
}
function Invalidate-Button([string]$Id) {
    if ($Id -and $script:buttons.ContainsKey($Id)) {
        $b = $script:buttons[$Id]
        Invalidate-Box $b.X $b.Y $b.W $b.H
    }
}
function Invalidate-Stage { Invalidate-Box 20 116 690 160 }
function Set-Button([string]$Id, [string]$Text, [string]$Style, [bool]$Visible = $true) {
    $b = $script:buttons[$Id]
    if ($Text) { $b.Text = $Text }
    if ($Style) { $b.Style = $Style }
    $b.Visible = $Visible
    Invalidate-Button $Id
}
function Find-Button([double]$X, [double]$Y) {
    foreach ($id in $script:buttonOrder) {
        $b = $script:buttons[$id]
        if ($b.Visible -and $X -ge $b.X -and $X -le ($b.X + $b.W) -and $Y -ge $b.Y -and $Y -le ($b.Y + $b.H)) { return $id }
    }
    return $null
}

function Paint-Button($g, [string]$Id) {
    $b = $script:buttons[$Id]
    if (-not $b.Visible) { return }
    $path = New-RoundedPath $b.X $b.Y $b.W $b.H 8
    switch ($b.Style) {
        'primary' {
            $fill = New-Blend $b.X $b.Y $b.W $b.H @('#FFD591', '#E08A3A', '#B45A1E') @(0, 0.6, 1)
            $g.FillPath($fill, $path); $fill.Dispose()
            $gloss = New-Object System.Drawing.Pen((Alpha 140 '#FFFFFF'), [single]1)
            $g.DrawLine($gloss, [single]($b.X + 8), [single]($b.Y + 1.5), [single]($b.X + $b.W - 8), [single]($b.Y + 1.5))
            $gloss.Dispose()
            $text = '#2A1206'
        }
        'disabled' {
            $fill = New-Object System.Drawing.SolidBrush((Alpha 18 '#FFFFFF'))
            $g.FillPath($fill, $path); $fill.Dispose()
            $pen = New-Object System.Drawing.Pen((Alpha 45 $palette.Gold), [single]1)
            $g.DrawPath($pen, $path); $pen.Dispose()
            $text = '#7A6650'
        }
        default {
            $fill = New-Object System.Drawing.SolidBrush((Alpha 22 '#FFFFFF'))
            $g.FillPath($fill, $path); $fill.Dispose()
            $pen = New-Object System.Drawing.Pen((Alpha 120 $palette.Gold), [single]1)
            $g.DrawPath($pen, $path); $pen.Dispose()
            $text = '#F3DFC4'
        }
    }
    if ($b.Style -ne 'disabled' -and -not $script:busy) {
        if ($script:pressId -eq $Id) {
            $shade = New-Object System.Drawing.SolidBrush((Alpha 50 '#000000')); $g.FillPath($shade, $path); $shade.Dispose()
        }
        elseif ($script:hoverId -eq $Id) {
            $shade = New-Object System.Drawing.SolidBrush((Alpha 34 '#FFFFFF')); $g.FillPath($shade, $path); $shade.Dispose()
        }
    }
    $rect = New-Object System.Drawing.RectangleF([single]$b.X, [single]$b.Y, [single]$b.W, [single]$b.H)
    $textBrush = New-Object System.Drawing.SolidBrush((Color $text))
    $g.DrawString($b.Text, $script:buttonFont, $textBrush, $rect, $script:centerFormat)
    $textBrush.Dispose(); $path.Dispose()
}

# Shared by every page.
$lblTitle = New-Label 'Choose your Aegis Wing package' 64 316 560 30 $fontTitle $palette.Cream

# Package page.
$lblPkgHint = New-Label 'The Xbox 360 Live Arcade file you own. Drag it here or browse for it.' 64 346 592 22 $fontBody $palette.Sand
$lblPath = New-Label 'No file selected' 74 377 446 38 $fontMono $palette.Dim
$lblPath.TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
$lblPath.AutoEllipsis = $true
$lblPath.Cursor = [System.Windows.Forms.Cursors]::Hand
$lblPkgStatus = New-Label 'Your package is only read, never changed. No game files ship with Setup.' 64 424 592 20 $fontSmall $palette.Sand
$lblPhase = New-Label '' 64 448 592 18 $fontCaps $palette.Gold
# Live counts sit right-aligned on the phase line while installing. Kept in
# front of the phase label and hidden otherwise.
$lblStats = New-Label '' 356 448 300 18 $fontSmall $palette.Sand
$lblStats.TextAlign = [System.Drawing.ContentAlignment]::TopRight
$lblStats.Visible = $false
$lblStats.BringToFront()
$lblDetail = New-Label '' 64 468 222 40 $fontSmall $palette.Sand
$packageControls = @($lblPkgHint, $lblPath, $lblPkgStatus, $lblPhase, $lblDetail)

# Extras page: a painted toggle, a title and a one-line explanation per extra.
# Clicking any of the three toggles it.
$script:extraRows = New-Object System.Collections.Generic.List[object]
$toggleExtra = {
    param($sender, $e)
    $row = $script:extraRows[[int]$sender.Tag]
    $row.On = -not $row.On
    Invalidate-Box 62 ($row.Y - 2) 28 28
    Update-ApplyButton
}
for ($i = 0; $i -lt $extraDefinitions.Count; $i++) {
    $rowY = $extrasTop + $i * $rowPitch
    $box = New-Label '' 62 ($rowY - 1) 26 26 $fontSmall $palette.Cream
    $title = New-Label $extraDefinitions[$i].Title 98 ($rowY - 3) 540 20 $fontRowTitle $palette.Cream
    $detail = New-Label $extraDefinitions[$i].Detail 98 ($rowY + 15) 560 18 $fontRowDetail $palette.Sand
    foreach ($control in @($box, $title, $detail)) {
        $control.Tag = $i
        $control.Cursor = [System.Windows.Forms.Cursors]::Hand
        $control.Add_Click($toggleExtra)
    }
    $script:extraRows.Add(@{ Def = $extraDefinitions[$i]; On = $false; Saved = $false; Y = $rowY; Controls = @($box, $title, $detail) })
}

# Apply is only lit when the ticks differ from what is set now: after a fresh
# install that means at least one extra is ticked.
function Test-ExtrasChanged {
    return @($script:extraRows | Where-Object { $_.On -ne $_.Saved }).Count -gt 0
}
function Update-ApplyButton {
    if ($script:page -eq 'extras') {
        Set-Button 'right' $null $(if (Test-ExtrasChanged) { 'primary' } else { 'disabled' })
    }
}

# Play page.
$lblPlay1 = New-Label 'Installed in the Game folder beside Setup. Saves stay in Game\userdata.' 64 352 592 20 $fontSmall $palette.Sand
$lblPlay2 = New-Label '' 64 374 592 20 $fontSmall $palette.Sand
$lblPlay3 = New-Label '' 64 404 592 20 $fontSmall $palette.Cream
$lblPlay4 = New-Label 'To play later, open Game\Aegis Wing.exe. Run Setup again to change your extras.' 64 434 350 36 $fontSmall $palette.Dim
$playControls = @($lblPlay1, $lblPlay2, $lblPlay3, $lblPlay4)

$script:page = 'package'
$script:selectedPackage = $null
$script:canInstall = $false
$script:installed = $false
$script:busy = $false
$script:showProgress = $false
$script:progress = 0
$script:existingInstall = $false
$script:shipF = 0.0
$script:shipTarget = 0.0
$script:backdrop = New-Backdrop

# Where the formation should be: Package sits at the first station; the
# install carries it to Extras; Play is the end of the rail.
function Set-ShipTarget {
    $script:shipTarget = switch ($script:page) {
        'extras' { 2.0 / 3.0 }
        'play' { 1.0 }
        default { if ($script:showProgress) { (2.0 / 3.0) * $script:progress / 100.0 } else { 0.0 } }
    }
}

$form.Add_Paint({
        param($sender, $e)
        $g = $e.Graphics
        $g.DrawImageUnscaled($script:backdrop, 0, 0)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $g.ScaleTransform([single]$script:scale, [single]$script:scale)

        # Lit rail up to the formation, with a warm glow under it.
        $f = $script:shipF
        $litX = $rail.X0 + ($rail.X1 - $rail.X0) * $f
        if ($litX -gt $rail.X0 + 1) {
            $glowPen = New-Object System.Drawing.Pen((Alpha 70 $palette.Ember), [single]7)
            $glowPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $glowPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $g.DrawLine($glowPen, [single]$rail.X0, [single]$rail.Y, [single]$litX, [single]$rail.Y)
            $litPen = New-Object System.Drawing.Pen((Color $palette.GoldHi), [single]2.5)
            $g.DrawLine($litPen, [single]$rail.X0, [single]$rail.Y, [single]$litX, [single]$rail.Y)
            $glowPen.Dispose(); $litPen.Dispose()
        }

        # Stations and their labels. The current step is cream and carries
        # the live percentage while installing.
        $stage = switch ($script:page) {
            'extras' { 2 }
            'play' { 3 }
            default { if ($script:showProgress) { 1 } else { 0 } }
        }
        for ($i = 0; $i -lt 4; $i++) {
            $sx = $rail.X0 + ($rail.X1 - $rail.X0) * $i / 3.0
            $lit = ($f -ge ($i / 3.0) - 0.002) -or $i -le $stage
            if ($lit) {
                Paint-Glow $g $sx $rail.Y 16 16 150 $palette.Ember
                $dotBrush = New-Object System.Drawing.SolidBrush((Color $palette.GoldHi))
            }
            else {
                $dotBrush = New-Object System.Drawing.SolidBrush((Color '#1A0C06'))
            }
            $g.FillEllipse($dotBrush, [single]($sx - 7.5), [single]($rail.Y - 7.5), [single]15, [single]15)
            $ring = New-Object System.Drawing.Pen((Color $palette.Gold), [single]1.6)
            $g.DrawEllipse($ring, [single]($sx - 7.5), [single]($rail.Y - 7.5), [single]15, [single]15)
            $dotBrush.Dispose(); $ring.Dispose()

            $label = $stageNames[$i]
            if ($i -lt $stage -or $script:page -eq 'play') { $label = $check + ' ' + $label }
            if ($i -eq 1 -and $script:showProgress) { $label = 'INSTALL ' + $dot + ' ' + $script:progress + '%' }
            $labelColor = if ($i -eq $stage) { $palette.Cream } elseif ($lit) { $palette.Gold } else { $palette.Dim }
            $labelBrush = New-Object System.Drawing.SolidBrush((Color $labelColor))
            $g.DrawString($label, $script:stationFont, $labelBrush,
                (New-Object System.Drawing.RectangleF([single]($sx - 70), [single]($rail.Y + 14), [single]140, [single]18)), $script:centerFormat)
            $labelBrush.Dispose()
        }

        # The formation: blue leads, red and green on the wings, yellow
        # trailing, joined by link beams like attached ships in the game.
        $cx = $rail.X0 + 30 + ($rail.X1 - $rail.X0 - 60) * $f
        $cy = 176.0
        $beam = New-Object System.Drawing.Pen((Alpha 150 $palette.Beam), [single]1.4)
        foreach ($end in @(@(30, 0), @(0, -28), @(0, 28), @(-30, 0))) {
            $g.DrawLine($beam, [single]$cx, [single]$cy, [single]($cx + $end[0]), [single]($cy + $end[1]))
        }
        $beam.Dispose()
        Paint-Ship $g ($cx - 30) $cy 38 $shipColors.Yellow
        Paint-Ship $g $cx ($cy - 28) 38 $shipColors.Red
        Paint-Ship $g $cx ($cy + 28) 38 $shipColors.Green
        Paint-Ship $g ($cx + 30) $cy 38 $shipColors.Blue

        if ($script:page -eq 'package') {
            $field = New-RoundedPath $fieldBox.X $fieldBox.Y $fieldBox.W $fieldBox.H 8
            $well = New-Object System.Drawing.SolidBrush((Alpha 170 '#000000'))
            $g.FillPath($well, $field)
            $fieldPen = New-Object System.Drawing.Pen((Alpha 80 $palette.Gold), [single]1)
            $g.DrawPath($fieldPen, $field)
            foreach ($o in @($fieldPen, $well, $field)) { $o.Dispose() }
        }
        elseif ($script:page -eq 'extras') {
            $sep = New-Object System.Drawing.Pen((Alpha 40 $palette.Gold), [single]1)
            for ($i = 1; $i -lt $script:extraRows.Count; $i++) {
                $y = [single]($extrasTop + $i * $rowPitch - 5)
                $g.DrawLine($sep, [single]64, $y, [single]656, $y)
            }
            $sep.Dispose()
            foreach ($row in $script:extraRows) {
                $box = New-RoundedPath 64 $row.Y 22 22 6
                if ($row.On) {
                    $on = New-Blend 64 $row.Y 22 22 @('#FFD591', '#E08A3A') @(0, 1)
                    $g.FillPath($on, $box); $on.Dispose()
                    $tick = New-Object System.Drawing.SolidBrush((Color '#2A1206'))
                    $g.DrawString($check, $script:checkFont, $tick,
                        (New-Object System.Drawing.RectangleF([single]64, [single]($row.Y + 1), [single]22, [single]22)), $script:centerFormat)
                    $tick.Dispose()
                }
                else {
                    $off = New-Object System.Drawing.SolidBrush((Alpha 150 '#000000'))
                    $g.FillPath($off, $box); $off.Dispose()
                    $edge = New-Object System.Drawing.Pen((Alpha 150 $palette.Gold), [single]1.2)
                    $g.DrawPath($edge, $box); $edge.Dispose()
                }
                $box.Dispose()
            }
        }

        foreach ($id in $script:buttonOrder) { Paint-Button $g $id }
    })

# Eases the formation toward its target, so the ships glide as real progress
# arrives. Only runs while they are moving.
$script:animation = New-Object System.Windows.Forms.Timer
$script:animation.Interval = 16
$script:animation.Add_Tick({
        $delta = $script:shipTarget - $script:shipF
        if ([Math]::Abs($delta) -gt 0.0006) {
            $script:shipF += $delta * 0.14
            Invalidate-Stage
        }
        elseif ($script:shipF -ne $script:shipTarget) {
            $script:shipF = $script:shipTarget
            Invalidate-Stage
        }
    })

function Set-InstallReady([bool]$Ready) {
    $script:canInstall = $Ready
    if ($script:page -eq 'package') {
        Set-Button 'right' $null $(if ($Ready) { 'primary' } else { 'disabled' })
    }
}

# Shows one page's controls and relabels the shared buttons for it.
function Show-Page([string]$Name) {
    $script:page = $Name
    foreach ($control in $packageControls) { $control.Visible = ($Name -eq 'package') }
    if ($Name -ne 'package') { $lblStats.Visible = $false }
    foreach ($row in $script:extraRows) {
        foreach ($control in $row.Controls) { $control.Visible = ($Name -eq 'extras') }
    }
    foreach ($control in $playControls) { $control.Visible = ($Name -eq 'play') }
    Set-Button 'choose' $null $null ($Name -eq 'package')
    Set-Button 'extras' $null $null ($Name -eq 'package' -and $script:existingInstall)
    switch ($Name) {
        'package' {
            $lblTitle.Text = 'Choose your Aegis Wing package'
            Set-Button 'left' 'Close' 'secondary'
            Set-Button 'right' 'Install game' $(if ($script:canInstall) { 'primary' } else { 'disabled' })
        }
        'extras' {
            $lblTitle.Text = 'Choose your extras'
            Set-Button 'left' 'Skip' 'secondary'
            Set-Button 'right' 'Apply' $(if (Test-ExtrasChanged) { 'primary' } else { 'disabled' })
        }
        'play' {
            $lblTitle.Text = 'Aegis Wing is ready'
            Set-Button 'left' 'Close' 'secondary'
            Set-Button 'right' 'Play now' 'primary'
        }
    }
    Set-ShipTarget
    $form.Invalidate()
}

function Enter-Extras {
    foreach ($row in $script:extraRows) {
        $row.Saved = [bool](Get-ExtraState $row.Def.Id)
        $row.On = $row.Saved
    }
    Show-Page 'extras'
}

function Enter-Play {
    $on = @($extraDefinitions | Where-Object { Get-ExtraState $_.Id } | ForEach-Object { $_.Short })
    $playerName = Get-ConfigValue 'net_player_name'
    $lblPlay2.Text = if ($playerName) { 'Your player name is ' + $playerName + '. Change it in Help & Options, Settings.' } else { '' }
    $lblPlay3.Text = if ($on.Count -gt 0) { 'Extras:  ' + ($on -join ('  ' + $dot + '  ')) } else { 'Extras:  none' }
    Show-Page 'play'
}

function Update-Progress([int]$Percent, [string]$Phase, [string]$Detail) {
    $script:progress = [Math]::Max(0, [Math]::Min(100, $Percent))
    if ($lblPhase.Text -ne $Phase) { $lblPhase.Text = $Phase }
    if ($lblStats.Text -ne $Detail) { $lblStats.Text = $Detail }
    $lblStats.Visible = $true
    Set-ShipTarget
    Invalidate-Stage
    [System.Windows.Forms.Application]::DoEvents()
}

function Select-Package([string]$Path) {
    if ($script:busy -or $script:page -ne 'package') { return }
    $script:selectedPackage = $null
    $script:showProgress = $false
    $lblStats.Visible = $false
    $lblPhase.ForeColor = Color $palette.Gold
    $lblPath.Text = $Path
    $lblPath.ForeColor = Color $palette.Cream

    $info = Get-PackageInfo $Path
    if ($info.Ok) {
        $script:selectedPackage = $Path
        $name = 'Aegis Wing'
        if ($info.Name) { $name = $info.Name }
        $lblPkgStatus.Text = ('{0}  {1}  {2}  Xbox LIVE Arcade game  {2}  Title 5841083C  {2}  {3:N0} MB' -f $check, $name, $dot, ($info.Bytes / 1MB))
        $lblPkgStatus.ForeColor = Color $palette.Ok
        Set-InstallReady $true
        $lblPhase.Text = 'READY TO INSTALL INTO THE GAME FOLDER'
        $lblDetail.Text = if ($script:existingInstall) { 'Installing again keeps your saves and settings.' } else { '' }
        Write-Log ('Package accepted: {0} ({1} bytes)' -f $Path, $info.Bytes)
    }
    else {
        $lblPkgStatus.Text = $info.Message
        $lblPkgStatus.ForeColor = Color $palette.Bad
        Set-InstallReady $false
        $lblPhase.Text = if ($script:existingInstall) { 'AEGIS WING IS ALREADY INSTALLED HERE' } else { '' }
        $lblDetail.Text = if ($script:existingInstall) { 'Installing again keeps your saves and settings.' } else { '' }
        Write-Log ('Package rejected: {0} - {1}' -f $Path, $info.Message)
    }
    $form.Invalidate()
}

function Show-PackagePicker {
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Choose your Aegis Wing Xbox 360 package'
    $dialog.Filter = 'Xbox 360 package (any file)|*.*'
    $dialog.CheckFileExists = $true
    $dialog.Multiselect = $false
    # Opens in Setup's own folder, where most players keep the package.
    $dialog.InitialDirectory = if ($script:selectedPackage) { Split-Path -Parent $script:selectedPackage } else { $releaseRoot }
    $dialog.RestoreDirectory = $true
    if ($dialog.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
        Select-Package $dialog.FileName
    }
    $dialog.Dispose()
}

function Start-Install {
    if (-not $script:canInstall -or -not $script:selectedPackage) { return }
    $script:busy = $true
    foreach ($id in $script:buttonOrder) { Set-Button $id $null 'disabled' $script:buttons[$id].Visible }
    $lblPhase.ForeColor = Color $palette.Gold
    $lblDetail.Text = ''
    $script:showProgress = $true
    Update-Progress 0 'PREPARING' ''
    try {
        [void](Invoke-AegisInstall -PackagePath $script:selectedPackage -Report {
                param($p, $phase, $detail)
                Update-Progress $p $phase $detail
            })
        $script:installed = $true
        $script:existingInstall = $true
        $script:showProgress = $false
        Write-Log 'Install finished.'
        $script:busy = $false
        Enter-Extras
    }
    catch {
        $message = $_.Exception.Message
        Write-Log ('FAILED: ' + $message)
        $script:showProgress = $false
        $lblStats.Visible = $false
        $lblPhase.Text = 'INSTALLATION DID NOT COMPLETE'
        $lblPhase.ForeColor = Color $palette.Bad
        $lblDetail.Text = 'Your package was not changed. Details are in the logs folder.'
        Set-ShipTarget
        $form.Invalidate()
        [void][System.Windows.Forms.MessageBox]::Show($form, $message, 'Aegis Wing Setup',
            [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error)
    }
    finally {
        $script:busy = $false
        if ($script:page -eq 'package') { Show-Page 'package' }
    }
}

function Save-Extras {
    $chosen = @($script:extraRows | Where-Object { $_.On } | ForEach-Object { $_.Def.Id })
    # A running game holds its options in memory and writes them back over
    # the save, which would undo a stage select change.
    $stageRow = $script:extraRows | Where-Object { $_.Def.Id -eq 'stageselect' }
    if ($stageRow.On -ne $stageRow.Saved -and (Test-GameRunning)) {
        [void][System.Windows.Forms.MessageBox]::Show($form,
            'Close Aegis Wing first, then choose Apply again.', 'Aegis Wing Setup',
            [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    try {
        Set-Extras $chosen
        Enter-Play
    }
    catch {
        Write-Log ('Extras not saved: ' + $_.Exception.Message)
        [void][System.Windows.Forms.MessageBox]::Show($form,
            ('Your extras could not be saved: ' + $_.Exception.Message + "`n`nThe game is installed and will still run."),
            'Aegis Wing Setup', [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning)
    }
}

function Invoke-Button([string]$Id) {
    if ($script:busy -or -not $Id -or $script:buttons[$Id].Style -eq 'disabled') { return }
    switch ($Id) {
        'choose' { Show-PackagePicker }
        'extras' { Enter-Extras }
        'left' {
            if ($script:page -eq 'extras') {
                Write-Log 'Extras skipped.'
                Enter-Play
            }
            else {
                $form.Close()
            }
        }
        'right' {
            switch ($script:page) {
                'package' { Start-Install }
                'extras' { Save-Extras }
                'play' { Start-Game; $form.Close() }
            }
        }
    }
}

$form.Add_MouseMove({
        param($sender, $e)
        $id = Find-Button ($e.X / $script:scale) ($e.Y / $script:scale)
        if ($id -ne $script:hoverId) {
            $old = $script:hoverId
            $script:hoverId = $id
            Invalidate-Button $old
            Invalidate-Button $id
        }
        $clickable = $id -and $script:buttons[$id].Style -ne 'disabled' -and -not $script:busy
        $form.Cursor = if ($clickable) { [System.Windows.Forms.Cursors]::Hand } else { [System.Windows.Forms.Cursors]::Default }
    })
$form.Add_MouseLeave({
        if ($script:hoverId) {
            $old = $script:hoverId
            $script:hoverId = $null
            Invalidate-Button $old
        }
    })
# Borderless window: a press outside the buttons drags it.
$form.Add_MouseDown({
        param($sender, $e)
        if ($e.Button -ne [System.Windows.Forms.MouseButtons]::Left) { return }
        $id = Find-Button ($e.X / $script:scale) ($e.Y / $script:scale)
        if ($id) {
            if ($script:buttons[$id].Style -ne 'disabled' -and -not $script:busy) {
                $script:pressId = $id
                Invalidate-Button $id
            }
        }
        else {
            [void][AegisSetup.Native]::ReleaseCapture()
            [void][AegisSetup.Native]::SendMessage($form.Handle, 0xA1, [IntPtr]2, [IntPtr]::Zero)
        }
    })
$form.Add_MouseUp({
        param($sender, $e)
        if (-not $script:pressId) { return }
        $id = $script:pressId
        $script:pressId = $null
        Invalidate-Button $id
        if ((Find-Button ($e.X / $script:scale) ($e.Y / $script:scale)) -eq $id) { Invoke-Button $id }
    })
$lblPath.Add_Click({ if (-not $script:busy) { Show-PackagePicker } })
$lblTitle.Add_MouseDown({
        param($sender, $e)
        if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Left) {
            [void][AegisSetup.Native]::ReleaseCapture()
            [void][AegisSetup.Native]::SendMessage($form.Handle, 0xA1, [IntPtr]2, [IntPtr]::Zero)
        }
    })

$form.Add_FormClosing({
        param($sender, $e)
        if ($script:busy) { $e.Cancel = $true }
    })
$form.Add_KeyDown({
        param($sender, $e)
        if ($script:busy) { return }
        if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Escape) { $form.Close() }
        elseif ($e.KeyCode -eq [System.Windows.Forms.Keys]::Enter) { Invoke-Button 'right' }
    })

# Dropping the package anywhere on the package page selects it.
$dragEnter = {
    param($sender, $e)
    if (-not $script:busy -and $script:page -eq 'package' -and
        $e.Data.GetDataPresent([System.Windows.Forms.DataFormats]::FileDrop)) {
        $e.Effect = [System.Windows.Forms.DragDropEffects]::Copy
    }
    else {
        $e.Effect = [System.Windows.Forms.DragDropEffects]::None
    }
}
$dragDrop = {
    param($sender, $e)
    $files = $e.Data.GetData([System.Windows.Forms.DataFormats]::FileDrop)
    if ($files -and $files.Count -gt 0) { Select-Package ([string]$files[0]) }
}
$form.Add_DragEnter($dragEnter)
$form.Add_DragDrop($dragDrop)
foreach ($control in $form.Controls) {
    $control.AllowDrop = $true
    $control.Add_DragEnter($dragEnter)
    $control.Add_DragDrop($dragDrop)
}

$form.Add_Shown({
        $form.Activate()
        if ((Test-Path -LiteralPath $gameExe) -and (Test-Path -LiteralPath (Join-Path $assetsDir 'default.xex'))) {
            $script:existingInstall = $true
            $lblPhase.Text = 'AEGIS WING IS ALREADY INSTALLED HERE'
            $lblDetail.Text = 'Installing again keeps your saves and settings.'
        }
        Show-Page 'package'
        $script:animation.Start()
        if ($PackagePath) { Select-Package $PackagePath }
    })

Show-Page 'package'
Write-Log ('Setup {0} opened in {1}' -f $releaseVersion, $releaseRoot)
[void]$form.ShowDialog()
$script:animation.Stop()
$script:animation.Dispose()
$form.Dispose()
$script:backdrop.Dispose()
exit 0
