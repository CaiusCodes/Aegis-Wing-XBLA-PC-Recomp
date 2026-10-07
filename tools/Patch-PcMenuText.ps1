param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath,
    [string]$ProgressPath
)

$resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
$bytes = [IO.File]::ReadAllBytes($resolvedArchive)
$encoding = [Text.Encoding]::BigEndianUnicode

function Write-PatchProgress {
    param([int]$Percent)

    if ([string]::IsNullOrWhiteSpace($ProgressPath)) {
        return
    }
    try {
        [IO.File]::WriteAllText($ProgressPath, "patch|$Percent")
    }
    catch {
        # Progress reporting must never interfere with the menu patch itself.
    }
}

Write-PatchProgress 0

function Get-UInt16BigEndian {
    param([int]$Offset)

    return ([int]$bytes[$Offset] -shl 8) -bor [int]$bytes[$Offset + 1]
}

function Get-UInt32BigEndian {
    param([int]$Offset)

    return ([uint32]$bytes[$Offset] -shl 24) -bor
        ([uint32]$bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$bytes[$Offset + 2] -shl 8) -bor
        [uint32]$bytes[$Offset + 3]
}

function Get-XzpEntry {
    param([Parameter(Mandatory = $true)] [string]$Name)

    if ([Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'XUIZ') {
        throw "$resolvedArchive is not an XUIZ archive."
    }

    $tableSize = [int](Get-UInt32BigEndian 0x10)
    $entryCount = Get-UInt16BigEndian 0x14
    $dataStart = 0x16 + $tableSize
    $cursor = 0x16

    for ($entryIndex = 0; $entryIndex -lt $entryCount; $entryIndex++) {
        $entrySize = [int](Get-UInt32BigEndian $cursor)
        $entryOffset = [int](Get-UInt32BigEndian ($cursor + 4))
        $nameLength = [int]$bytes[$cursor + 8]
        $entryName = $encoding.GetString($bytes, $cursor + 9, $nameLength * 2)

        if ($entryName -eq $Name) {
            return @{
                Offset = $dataStart + $entryOffset
                Size = $entrySize
            }
        }

        $cursor += 9 + ($nameLength * 2)
    }

    throw "XUIZ entry '$Name' was not found in $resolvedArchive."
}

function Set-FixedWidthText {
    param(
        [Parameter(Mandatory = $true)] [string]$OldText,
        [Parameter(Mandatory = $true)] [string]$NewText
    )

    if ($NewText.Length -gt $OldText.Length) {
        throw "Replacement '$NewText' is longer than '$OldText'."
    }

    $oldBytes = $encoding.GetBytes($OldText)
    $replacement = $NewText.PadRight($OldText.Length)
    $newBytes = $encoding.GetBytes($replacement)
    $matches = 0

    for ($offset = 0; $offset -le $bytes.Length - $oldBytes.Length; $offset++) {
        $isMatch = $true
        for ($index = 0; $index -lt $oldBytes.Length; $index++) {
            if ($bytes[$offset + $index] -ne $oldBytes[$index]) {
                $isMatch = $false
                break
            }
        }

        if ($isMatch) {
            [Array]::Copy($newBytes, 0, $bytes, $offset, $newBytes.Length)
            $matches++
            $offset += $oldBytes.Length - 1
        }
    }

    return $matches
}

function Clear-XurCaption {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [string]$Caption
    )

    $entry = Get-XzpEntry $EntryName
    if ([Text.Encoding]::ASCII.GetString($bytes, $entry.Offset, 4) -ne 'XUIB') {
        throw "XUIZ entry '$EntryName' is not an XUIB resource."
    }

    if ($Caption.Length -gt 255) {
        throw "Caption '$Caption' is too long for an XUIB string-table entry."
    }

    $captionBytes = $encoding.GetBytes($Caption)
    $blankBytes = $encoding.GetBytes((' ' * $Caption.Length))
    $stringSection = $entry.Offset + [int](Get-UInt32BigEndian ($entry.Offset + 0x18))
    $stringSize = [int](Get-UInt32BigEndian ($entry.Offset + 0x1C))
    $matches = 0
    $lastOffset = $stringSection + $stringSize - $captionBytes.Length - 1

    for ($offset = $stringSection; $offset -le $lastOffset; $offset++) {
        if ($bytes[$offset] -ne $Caption.Length) {
            continue
        }

        $isMatch = $true
        for ($index = 0; $index -lt $captionBytes.Length; $index++) {
            if ($bytes[$offset + 1 + $index] -ne $captionBytes[$index]) {
                $isMatch = $false
                break
            }
        }

        if ($isMatch) {
            [Array]::Copy($blankBytes, 0, $bytes, $offset + 1, $blankBytes.Length)
            $matches++
            $offset += $captionBytes.Length
        }
    }

    if ($matches -ne 1) {
        throw "Expected one '$Caption' caption in '$EntryName', found $matches."
    }

    return $matches
}

function Set-XurCaption {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [string]$OldCaption,
        [Parameter(Mandatory = $true)] [string]$NewCaption
    )

    if ($NewCaption.Length -gt $OldCaption.Length) {
        throw "Replacement '$NewCaption' is longer than '$OldCaption'."
    }
    $entry = Get-XzpEntry $EntryName
    $oldBytes = $encoding.GetBytes($OldCaption)
    $newBytes = $encoding.GetBytes($NewCaption.PadRight($OldCaption.Length))
    $stringSection = $entry.Offset + [int](Get-UInt32BigEndian ($entry.Offset + 0x18))
    $stringSize = [int](Get-UInt32BigEndian ($entry.Offset + 0x1C))
    $matches = 0
    $lastOffset = $stringSection + $stringSize - $oldBytes.Length - 1
    for ($offset = $stringSection; $offset -le $lastOffset; $offset++) {
        if ($bytes[$offset] -ne $OldCaption.Length) { continue }
        $isMatch = $true
        for ($index = 0; $index -lt $oldBytes.Length; $index++) {
            if ($bytes[$offset + 1 + $index] -ne $oldBytes[$index]) {
                $isMatch = $false
                break
            }
        }
        if ($isMatch) {
            [Array]::Copy($newBytes, 0, $bytes, $offset + 1, $newBytes.Length)
            $matches++
            $offset += $oldBytes.Length
        }
    }
    if ($matches -ne 1) {
        throw "Expected one '$OldCaption' caption in '$EntryName', found $matches."
    }
    return $matches
}

function Set-UInt32BigEndian {
    param([int]$Offset, [uint32]$Value)

    $bytes[$Offset] = [byte](($Value -shr 24) -band 0xFF)
    $bytes[$Offset + 1] = [byte](($Value -shr 16) -band 0xFF)
    $bytes[$Offset + 2] = [byte](($Value -shr 8) -band 0xFF)
    $bytes[$Offset + 3] = [byte]($Value -band 0xFF)
}

# Replaces a caption with one of any length, with no padding spaces. XUIB
# strings are referenced by ordinal, never by offset, so resizing one only
# shifts what follows it: the resource's later sections and total size, then
# the archive's later entries
# and file size. The XUIB header is 'XUIB', version, flags, u16, u32 total
# size at +0x0E, u16 section count at +0x12, then 12-byte sections (name,
# offset, size) from +0x14. The XUIZ header holds the file size at +0x08 and
# the entry table (u32 size, u32 offset from the data start, u8 name length,
# UTF-16BE name) from +0x16.
function Set-XurCaptionResized {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [string]$OldCaption,
        [Parameter(Mandatory = $true)] [string]$NewCaption
    )

    if ($NewCaption.Length -eq $OldCaption.Length) {
        return (Set-XurCaption $EntryName $OldCaption $NewCaption)
    }
    if ($NewCaption.Length -gt 255) {
        throw "Caption '$NewCaption' is too long for an XUIB string-table entry."
    }

    $entry = Get-XzpEntry $EntryName
    $base = $entry.Offset
    if ([Text.Encoding]::ASCII.GetString($bytes, $base, 4) -ne 'XUIB') {
        throw "XUIZ entry '$EntryName' is not an XUIB resource."
    }
    $stringSection = $base + [int](Get-UInt32BigEndian ($base + 0x18))
    $stringSize = [int](Get-UInt32BigEndian ($base + 0x1C))

    # Walk the table record by record (u16 length, UTF-16BE text).
    $record = $null
    $cursor = $stringSection
    while ($cursor -lt $stringSection + $stringSize) {
        $length = Get-UInt16BigEndian $cursor
        if ($length -eq $OldCaption.Length -and
            $encoding.GetString($bytes, $cursor + 2, $length * 2) -eq $OldCaption) {
            if ($null -ne $record) {
                throw "Expected one '$OldCaption' caption in '$EntryName', found more."
            }
            $record = $cursor
        }
        $cursor += 2 + $length * 2
    }
    if ($null -eq $record) {
        throw "Expected one '$OldCaption' caption in '$EntryName', found 0."
    }

    $newText = $encoding.GetBytes($NewCaption)
    $delta = ($NewCaption.Length - $OldCaption.Length) * 2
    $oldEnd = $record + 2 + $OldCaption.Length * 2
    $resized = New-Object byte[] ($bytes.Length + $delta)
    [Array]::Copy($bytes, 0, $resized, 0, $record)
    $resized[$record] = 0
    $resized[$record + 1] = [byte]$NewCaption.Length
    [Array]::Copy($newText, 0, $resized, $record + 2, $newText.Length)
    [Array]::Copy($bytes, $oldEnd, $resized, $oldEnd + $delta, $bytes.Length - $oldEnd)
    $script:bytes = $resized

    # The resource: its total size, the string section's size, and the
    # offsets of the sections stored after it.
    $stringOffset = [int](Get-UInt32BigEndian ($base + 0x18))
    Set-UInt32BigEndian ($base + 0x0E) ((Get-UInt32BigEndian ($base + 0x0E)) + $delta)
    Set-UInt32BigEndian ($base + 0x1C) ($stringSize + $delta)
    $sectionCount = Get-UInt16BigEndian ($base + 0x12)
    for ($section = 0; $section -lt $sectionCount; $section++) {
        $field = $base + 0x14 + $section * 12 + 4
        $offset = [int](Get-UInt32BigEndian $field)
        if ($offset -gt $stringOffset) {
            Set-UInt32BigEndian $field ($offset + $delta)
        }
    }

    # The archive: this entry's size, the offsets of the entries stored after
    # it, and the file size.
    $tableSize = [int](Get-UInt32BigEndian 0x10)
    $entryCount = Get-UInt16BigEndian 0x14
    $dataStart = 0x16 + $tableSize
    $entryOffset = $base - $dataStart
    $cursor = 0x16
    for ($entryIndex = 0; $entryIndex -lt $entryCount; $entryIndex++) {
        $offset = [int](Get-UInt32BigEndian ($cursor + 4))
        if ($offset -eq $entryOffset) {
            Set-UInt32BigEndian $cursor ((Get-UInt32BigEndian $cursor) + $delta)
        }
        elseif ($offset -gt $entryOffset) {
            Set-UInt32BigEndian ($cursor + 4) ($offset + $delta)
        }
        $cursor += 9 + ([int]$bytes[$cursor + 8] * 2)
    }
    Set-UInt32BigEndian 0x08 ([uint32]$bytes.Length)
    return 1
}

function Copy-XurVectorY {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [int]$DestinationVector,
        [Parameter(Mandatory = $true)] [int]$SourceVector
    )

    $entry = Get-XzpEntry $EntryName
    $vectorSection = $entry.Offset + [int](Get-UInt32BigEndian ($entry.Offset + 0x24))
    $vectorSize = [int](Get-UInt32BigEndian ($entry.Offset + 0x28))
    $vectorCount = [math]::Floor($vectorSize / 12)

    if ($DestinationVector -ge $vectorCount -or $SourceVector -ge $vectorCount) {
        throw "Vector index is out of range for '$EntryName'."
    }

    $destinationY = $vectorSection + ($DestinationVector * 12) + 4
    $sourceY = $vectorSection + ($SourceVector * 12) + 4
    [Array]::Copy($bytes, $sourceY, $bytes, $destinationY, 4)
}

function Set-XurVectorYValue {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [int]$Vector,
        [Parameter(Mandatory = $true)] [single]$Value
    )

    $entry = Get-XzpEntry $EntryName
    $vectorSection = $entry.Offset + [int](Get-UInt32BigEndian ($entry.Offset + 0x24))
    $vectorSize = [int](Get-UInt32BigEndian ($entry.Offset + 0x28))
    $vectorCount = [math]::Floor($vectorSize / 12)
    if ($Vector -ge $vectorCount) {
        throw "Vector index is out of range for '$EntryName'."
    }

    $destination = $vectorSection + ($Vector * 12) + 4
    $valueBytes = [BitConverter]::GetBytes($Value)
    if ([BitConverter]::IsLittleEndian) {
        [Array]::Reverse($valueBytes)
    }

    $changed = 0
    for ($index = 0; $index -lt 4; $index++) {
        if ($bytes[$destination + $index] -ne $valueBytes[$index]) {
            $changed = 1
        }
        $bytes[$destination + $index] = $valueBytes[$index]
    }
    return $changed
}

function Get-XurStringOrdinals {
    param([Parameter(Mandatory = $true)] $Entry)

    $stringSection = $Entry.Offset + [int](Get-UInt32BigEndian ($Entry.Offset + 0x18))
    $stringSize = [int](Get-UInt32BigEndian ($Entry.Offset + 0x1C))
    $cursor = $stringSection
    $ordinal = 0
    $ordinals = @{}

    while ($cursor -lt $stringSection + $stringSize) {
        $length = [int]$bytes[$cursor]
        $cursor++
        $value = $encoding.GetString($bytes, $cursor, $length * 2)
        $cursor += $length * 2

        if ($length -gt 0) {
            $ordinal++
            $ordinals[$value] = $ordinal
        }
    }

    return $ordinals
}

function Set-XurButtonNavigation {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [string]$ButtonId,
        [Parameter(Mandatory = $true)] [string]$ExpectedUp,
        [Parameter(Mandatory = $true)] [string]$ExpectedDown,
        [Parameter(Mandatory = $true)] [string]$NewUp,
        [Parameter(Mandatory = $true)] [string]$NewDown
    )

    $entry = Get-XzpEntry $EntryName
    $ordinals = Get-XurStringOrdinals $entry
    foreach ($required in @('XuiButton', $ButtonId, $ExpectedUp, $ExpectedDown, $NewUp, $NewDown)) {
        if (-not $ordinals.ContainsKey($required)) {
            throw "XUIB string '$required' was not found in '$EntryName'."
        }
    }

    $buttonClasses = @([int]$ordinals['XuiButton'])
    if ($ordinals.ContainsKey('XuiNavButton')) {
        $buttonClasses += [int]$ordinals['XuiNavButton']
    }

    $buttonOrdinal = [int]$ordinals[$ButtonId]
    $expectedUpOrdinal = [int]$ordinals[$ExpectedUp]
    $expectedDownOrdinal = [int]$ordinals[$ExpectedDown]
    $newUpOrdinal = [int]$ordinals[$NewUp]
    $newDownOrdinal = [int]$ordinals[$NewDown]
    $dataSection = $entry.Offset + [int](Get-UInt32BigEndian ($entry.Offset + 0x30))
    $dataSize = [int](Get-UInt32BigEndian ($entry.Offset + 0x34))
    $matches = @()

    # A serialized button record stores its non-empty string-table ordinal at
    # +8. The exact navigation-field offset varies slightly by XUI class and
    # resource version, so locate the expected 00/up/00/down/00 pair below.
    for ($offset = $dataSection; $offset -le $dataSection + $dataSize - 35; $offset++) {
        if ($buttonClasses -notcontains [int]$bytes[$offset] -or
            $bytes[$offset + 1] -ne 1 -or
            $bytes[$offset + 2] -ne 0 -or
            $bytes[$offset + 8] -ne $buttonOrdinal) {
            continue
        }

        $matches += $offset
    }

    if ($matches.Count -ne 1) {
        throw "Expected one serialized '$ButtonId' button in '$EntryName', found $($matches.Count)."
    }

    $record = [int]$matches[0]
    $navigationOffset = $null
    for ($relative = 25; $relative -le 30; $relative++) {
        $up = [int]$bytes[$record + $relative]
        $down = [int]$bytes[$record + $relative + 2]
        $hasSeparators = $bytes[$record + $relative - 1] -eq 0 -and
            $bytes[$record + $relative + 1] -eq 0 -and
            $bytes[$record + $relative + 3] -eq 0
        $matchesExpected = ($up -eq $expectedUpOrdinal -or $up -eq $newUpOrdinal) -and
            ($down -eq $expectedDownOrdinal -or $down -eq $newDownOrdinal)

        if ($hasSeparators -and $matchesExpected) {
            $navigationOffset = $relative
            break
        }
    }

    if ($null -eq $navigationOffset) {
        throw "Could not locate navigation targets on '$ButtonId' in '$EntryName'."
    }

    $currentUp = [int]$bytes[$record + $navigationOffset]
    $currentDown = [int]$bytes[$record + $navigationOffset + 2]
    $changes = 0
    if ($currentUp -ne $newUpOrdinal) {
        $bytes[$record + $navigationOffset] = [byte]$newUpOrdinal
        $changes++
    }
    if ($currentDown -ne $newDownOrdinal) {
        $bytes[$record + $navigationOffset + 2] = [byte]$newDownOrdinal
        $changes++
    }

    return $changes
}

function Set-XurSceneDefaultFocus {
    param(
        [Parameter(Mandatory = $true)] [string]$EntryName,
        [Parameter(Mandatory = $true)] [string]$ExpectedId,
        [Parameter(Mandatory = $true)] [string]$NewId
    )

    $entry = Get-XzpEntry $EntryName
    $ordinals = Get-XurStringOrdinals $entry
    foreach ($required in @($ExpectedId, $NewId)) {
        if (-not $ordinals.ContainsKey($required)) {
            throw "XUIB string '$required' was not found in '$EntryName'."
        }
    }

    $expectedOrdinal = [int]$ordinals[$ExpectedId]
    $newOrdinal = [int]$ordinals[$NewId]
    $dataSection = $entry.Offset + [int](Get-UInt32BigEndian ($entry.Offset + 0x30))
    $dataSize = [int](Get-UInt32BigEndian ($entry.Offset + 0x34))
    $matches = @()

    # The scene record stores its custom class and default-focus ID in the
    # sequence 30 02 00 01 00 <class> 01 01 00 <focus> 00 00.
    for ($offset = $dataSection; $offset -le $dataSection + $dataSize - 12; $offset++) {
        if ($bytes[$offset] -eq 0x30 -and
            $bytes[$offset + 1] -eq 0x02 -and
            $bytes[$offset + 2] -eq 0 -and
            $bytes[$offset + 3] -eq 1 -and
            $bytes[$offset + 4] -eq 0 -and
            $bytes[$offset + 6] -eq 1 -and
            $bytes[$offset + 7] -eq 1 -and
            $bytes[$offset + 8] -eq 0 -and
            ($bytes[$offset + 9] -eq $expectedOrdinal -or $bytes[$offset + 9] -eq $newOrdinal) -and
            $bytes[$offset + 10] -eq 0 -and
            $bytes[$offset + 11] -eq 0) {
            $matches += $offset
        }
    }

    if ($matches.Count -ne 1) {
        throw "Expected one default-focus record in '$EntryName', found $($matches.Count)."
    }

    $focusOffset = [int]$matches[0] + 9
    if ($bytes[$focusOffset] -eq $newOrdinal) {
        return 0
    }

    $bytes[$focusOffset] = [byte]$newOrdinal
    return 1
}

$patched = 0
$patched += Set-FixedWidthText 'Return to Arcade' 'Exit Game'
Write-PatchProgress 4
$patched += Set-FixedWidthText 'Quit game and return to' 'Exit game'
Write-PatchProgress 8
$patched += Set-FixedWidthText 'Xbox Live Arcade' 'Desktop'
Write-PatchProgress 12
$patched += Set-FixedWidthText 'Multiplayer Match' 'Multiplayer'
Write-PatchProgress 16
$patched += Set-XurCaptionResized 'SettingsMenu.xur' 'Audio Settings' 'Settings'
Write-PatchProgress 20

# The PC settings layer replaces this scene's two retail sliders and confirm
# prompt. Keep the scene/background/title and its native save/return behavior,
# but move the superseded visual controls safely outside the viewport.
$patched += Set-XurVectorYValue 'SettingsMenu.xur' 3 2000.0
$patched += Set-XurVectorYValue 'SettingsMenu.xur' 4 2000.0
$patched += Set-XurVectorYValue 'SettingsMenu.xur' 5 2000.0
Write-PatchProgress 30

# These four native button captions intermittently lose their cached glyph
# surface after returning to Help & Options from a child scene while paused.
# Keep the native buttons/focus/highlight intact and draw stable captions from
# the host UI at the same positions.
$patched += Clear-XurCaption 'HelpAndOptionsMenu.xur' 'How To Play'
$patched += Clear-XurCaption 'HelpAndOptionsMenu.xur' 'Controls'
$patched += Clear-XurCaption 'HelpAndOptionsMenu.xur' 'Credits'
$patched += Clear-XurCaption 'HelpAndOptionsMenu.xur' 'Settings'
Write-PatchProgress 42

# Keep the control IDs intact so the recompiled menu code can initialize and
# disable these Xbox-only rows, but remove their visible captions from the two
# menus. Then compact the remaining PC rows into the vacated slots.
$patched += Set-XurCaption 'MainMenu.xur' 'Leaderboards' 'High Scores'
$patched += Clear-XurCaption 'MainMenu.xur' 'Achievements'
$patched += Clear-XurCaption 'PauseMenu.xur' 'Leaderboards'
# The pause Achievements row is reborn as the local High Scores entry; its
# retail caption is blanked like the other rows and the host UI draws the
# stable "High Scores" text.
$patched += Clear-XurCaption 'PauseMenu.xur' 'Achievements'
# The pause scene's own captions also intermittently lose their cached glyph
# surface after gameplay. The host UI draws stable equivalents aligned to the
# compacted focus pills, so blank the native strings to prevent double text
# in runs where the glyph cache survives the transition.
$patched += Clear-XurCaption 'PauseMenu.xur' 'Resume Game'
$patched += Clear-XurCaption 'PauseMenu.xur' "Help & Options`r`n"
$patched += Clear-XurCaption 'PauseMenu.xur' 'Exit Game'
$patched += Clear-XurCaption 'PauseMenu.xur' 'GAME PAUSED'
$patched += Set-XurCaption 'LevelFailMenu.xur' 'Achievements' 'High Scores'
$patched += Set-XurCaption 'GameCompleteMenu.xur' 'Achievements' 'High Scores'
# The main menu's "A Select" legend says nothing a PC player needs (and the
# mouse selects by pointing): move it out of the viewport. Vector 9 is its
# position; the control itself stays, so the scene initializes as before.
$patched += Set-XurVectorYValue 'MainMenu.xur' 9 2000.0
# Place the three local PC rows in the same order as their focus chain:
# High Scores, Help & Options, Exit Game.
Copy-XurVectorY 'MainMenu.xur' 2 3 # Exit Game -> former Help row
Copy-XurVectorY 'MainMenu.xur' 3 7 # Help & Options -> former Achievements row
Copy-XurVectorY 'PauseMenu.xur' 6 4 # Exit Game -> row immediately after Help
# The pause menu carries four rows now: Resume, High Scores, Help & Options,
# Exit Game. Re-space the moved rows evenly; Resume keeps its retail slot.
$patched += Set-XurVectorYValue 'PauseMenu.xur' 5 114.5
$patched += Set-XurVectorYValue 'PauseMenu.xur' 7 191.5
$patched += Set-XurVectorYValue 'PauseMenu.xur' 6 268.5
Write-PatchProgress 58

# Rewrite the compiled focus chains so navigation bypasses the removed rows in
# both directions. Visibility and enabled state alone do not alter XUI's
# explicit up/down links.
$patched += Set-XurButtonNavigation 'MainMenu.xur' 'btnMultiplayer' 'btnSinglePlayer' 'btnLeaderboards' 'btnSinglePlayer' 'btnLeaderboards'
$patched += Set-XurButtonNavigation 'MainMenu.xur' 'btnLeaderboards' 'btnMultiplayer' 'btnAchievements' 'btnMultiplayer' 'btnHelpOptions'
$patched += Set-XurButtonNavigation 'MainMenu.xur' 'btnHelpOptions' 'btnAchievements' 'btnExit' 'btnLeaderboards' 'btnExit'
$patched += Set-XurButtonNavigation 'PauseMenu.xur' 'btnResume' 'btnMainMenu' 'btnHelpOptions' 'btnMainMenu' 'btnAchievements'
$patched += Set-XurButtonNavigation 'PauseMenu.xur' 'btnHelpOptions' 'btnResume' 'btnLeaderBoards' 'btnAchievements' 'btnMainMenu'
$patched += Set-XurButtonNavigation 'PauseMenu.xur' 'btnAchievements' 'btnLeaderBoards' 'btnMainMenu' 'btnResume' 'btnHelpOptions'
$patched += Set-XurButtonNavigation 'PauseMenu.xur' 'btnMainMenu' 'btnAchievements' 'btnResume' 'btnHelpOptions' 'btnResume'
Write-PatchProgress 68

# The multiplayer scene carries three PC rows: the two LAN entries that
# replace the Xbox LIVE matchmaking ones, and local co-op. Quick Match looked
# for a game anywhere on LIVE and has no PC equivalent, so its caption goes
# and the recompiled scene code hides the row itself.
$patched += Clear-XurCaption 'MultiplayerMenu.xur' 'Quick Match'
$patched += Set-XurCaptionResized 'MultiplayerMenu.xur' 'Custom Match' 'Join LAN Game'
$patched += Set-XurCaptionResized 'MultiplayerMenu.xur' 'Create Match' 'Create LAN Game'
$patched += Clear-XurCaption 'MultiplayerMenu.xur' 'Game Experience May Change During Online Play'
Write-PatchProgress 80

# Close the gap the hidden Quick Match row leaves: the three PC rows move up
# one slot, keeping the retail spacing.
$patched += Set-XurVectorYValue 'MultiplayerMenu.xur' 3 403.5   # Join LAN Game
$patched += Set-XurVectorYValue 'MultiplayerMenu.xur' 2 463.33  # Create LAN Game
$patched += Set-XurVectorYValue 'MultiplayerMenu.xur' 1 523.17  # Local Multiplayer

# Focus starts on Join LAN Game and cycles through the three visible rows.
$patched += Set-XurSceneDefaultFocus 'MultiplayerMenu.xur' 'btnQuickMatch' 'btnCustomMatch'
$patched += Set-XurButtonNavigation 'MultiplayerMenu.xur' 'btnCustomMatch' 'btnQuickMatch' 'btnCreateGame' 'btnCoop' 'btnCreateGame'
$patched += Set-XurButtonNavigation 'MultiplayerMenu.xur' 'btnCoop' 'btnCreateGame' 'btnQuickMatch' 'btnCreateGame' 'btnCustomMatch'
Write-PatchProgress 96

if ($patched -gt 0) {
    [IO.File]::WriteAllBytes($resolvedArchive, $bytes)
}

Write-PatchProgress 100

Write-Host "Patched $patched PC menu text entries in $resolvedArchive"
