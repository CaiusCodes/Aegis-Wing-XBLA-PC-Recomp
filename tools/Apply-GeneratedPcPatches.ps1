<#
.SYNOPSIS
    Applies the Aegis Wing PC port's changes to freshly generated ReXGlue code.
.DESCRIPTION
    ReXGlue's code generator turns the supported Aegis Wing default.xex
    (SHA-256 C57F6A81...2873) into generated\default. The PC port then needs
    a few dozen small hooks in that code: menu rows hidden or repurposed, the
    pause menu's host captions, local High Scores, the PC Settings panel and
    input filtering. Every one of them is a patch in the table below; nothing
    under generated\ is edited by hand.

    Each patch names the file, the generated function it belongs to and the
    exact code it expects there (the anchor, or the code it replaces). The
    anchor has to occur exactly once inside that function - never searched
    for across the file - so a patch cannot land in an unrelated function. The
    anchors include the generator's own disassembly comments and call return
    addresses, which pin each site to one instruction.

    All patches are planned in memory first. If any one cannot find what it
    expects, the script stops with an error and writes nothing, so a failed
    run never leaves a half-patched tree. A patch whose result is already in
    place is reported and skipped, so running the script twice is harmless.
    Patches apply in table order; two anchors (pause-hide-labels-on-choice and
    help-child-open) include the end of the patch just above them, because
    those hooks sit two lines apart.

    The table was derived from the difference between a fresh code generation
    and the port's verified generated tree (2026-10-07). If ReXGlue's code
    generator or the game build changes, expect anchors to stop matching:
    the error names the patch and function to look at.
.PARAMETER GeneratedDir
    The generated code to patch. Defaults to generated\default in this
    project; tools\Regenerate-Code.ps1 passes its temporary output instead.
.PARAMETER Check
    Patch nothing; only report whether every patch is already in place. Exits
    with code 1 if any is missing.
#>
[CmdletBinding()]
param(
    [string]$GeneratedDir,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'

if (-not $GeneratedDir) {
    $GeneratedDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'generated\default'
}
$GeneratedDir = [System.IO.Path]::GetFullPath($GeneratedDir)
if (-not (Test-Path -LiteralPath $GeneratedDir -PathType Container)) {
    throw "Generated code folder not found: $GeneratedDir"
}

$patches = @(
    @{
        Id       = 'image-size-padding'
        Purpose  = 'Pad the host image span to 0x4F0000 so ReXGlue''s indirect-call table starts on a 64 KiB boundary.'
        File     = 'aegis_wing_init.h'
        Function = $null
        Kind     = 'Replace'
        Find     = @'
#define REX_IMAGE_SIZE 0x4EB000ull
'@
        Text     = @'
// The retail XEX image is 0x4EB000 bytes. ReXGlue stores its indirect-call
// table immediately after the image and allocates that table with 64 KiB page
// alignment, so pad the host-side image span to the next allocation boundary.
// The XEX data itself is unchanged; the extra 0x5000 bytes are reserved space.
#define REX_IMAGE_SIZE 0x4F0000ull
'@
    }
    @{
        Id       = 'recomp2-host-declarations'
        Purpose  = 'Declare the host hooks the menu, pause and high-score patches in this file call.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = $null
        Kind     = 'InsertAfter'
        Anchor   = @'
#include "aegis_wing_init.h"
'@
        Text     = @'
void AegisWing_ShowPauseMenuLabels();
void AegisWing_HidePauseMenuLabels();
void AegisWing_NotePauseHelpChoice();
void AegisWing_NotePauseConfirmOpen(int from_pause);
void AegisWing_OnPauseSceneInit(uint32_t scene);
void AegisWing_OnPauseSceneDestroyed(uint32_t scene);
void AegisWing_OnPauseNavReturn(uint32_t scene);
void AegisWing_OnMainMenuShown(uint32_t scene);
void AegisWing_OnSceneInit(uint32_t scene);

void AegisWing_CaptureSessionStats(uint64_t xuid, uint32_t view_count,
	uint32_t views_address);
void AegisWing_OpenHighScores();
void AegisWing_RecordCurrentHighScores(uint32_t scene_address);
'@
    }
    @{
        Id       = 'scene-init-hook'
        Purpose  = 'Shared scene init: remember each menu scene class for mouse menus.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B0F88'
        Kind     = 'InsertAfter'
        Anchor   = @'
DEFINE_REX_FUNC(sub_920B0F88) {
	REX_FUNC_PROLOGUE();
'@
        Text     = @'
	// PC port: a menu scene is starting; remember its class for mouse menus.
	AegisWing_OnSceneInit(ctx.r3.u32);
'@
    }
    @{
        Id       = 'main-menu-finalize'
        Purpose  = 'Main menu setup: version label owner; restore Leaderboards as local High Scores, hide Achievements.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B1CF8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.cr6.compare<int32_t>(ctx.r29.s32, 4, ctx.xer);
	// blt cr6,0x920b1de0
	if (ctx.cr6.lt) goto loc_920B1DE0;
'@
        Text     = @'
	// PC port: the main menu is showing; nothing from a gameplay pause session
	// may remain on screen. The scene is remembered for the version label.
	AegisWing_OnMainMenuShown(ctx.r31.u32);
	// PC port: restore Leaderboards as local High Scores while keeping the
	// obsolete Xbox Achievements row unavailable.
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 28);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);  // SetEnable(true).
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 28);
	ctx.r4.s64 = 1;
	sub_923BE3B0(ctx, base);  // SetShow(true).
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 32);
	ctx.r4.s64 = 0;
	sub_923BE208(ctx, base);  // SetEnable(false).
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 32);
	ctx.r4.s64 = 0;
	sub_923BE3B0(ctx, base);  // SetShow(false).
'@
    }
    @{
        Id       = 'main-menu-removed-buttons'
        Purpose  = 'Main menu events: Leaderboards opens local High Scores; swallow stale Achievements events.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B1E80'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// mr r28,r4
	ctx.r28.u64 = ctx.r4.u64;
	// stw r26,0(r6)
'@
        Text     = @'
	// PC port: route the former Xbox leaderboard button to local High Scores.
	if (ctx.r28.u32 == REX_LOAD_U32(ctx.r29.u32 + 28)) {
		AegisWing_OpenHighScores();
		REX_STORE_U32(ctx.r6.u32 + 0, ctx.r26.u32);
		ctx.r3.s64 = 0;
		ctx.r1.s64 = ctx.r1.s64 + 144;
		__restgprlr_25(ctx, base);
		return;
	}
	// Consume any stale event from the removed Xbox Achievements control.
	if (ctx.r28.u32 == REX_LOAD_U32(ctx.r29.u32 + 32)) {
		REX_STORE_U32(ctx.r6.u32 + 0, ctx.r26.u32);
		ctx.r3.s64 = 0;
		ctx.r1.s64 = ctx.r1.s64 + 144;
		__restgprlr_25(ctx, base);
		return;
	}
'@
    }
    @{
        Id       = 'multiplayer-hide-quick-match'
        Purpose  = 'Multiplayer menu setup: hide the Xbox LIVE Quick Match row.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B2898'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920c4f00
	ctx.lr = 0x920B2950;
	sub_920C4F00(ctx, base);
'@
        Text     = @'
	// PC port: Quick Match looked for a game anywhere on Xbox LIVE, which has
	// no PC equivalent, so that row goes. The other two rows stay: they are
	// the port's Join LAN Game and Create LAN Game entries.
	// SetEnable prevents focus/navigation; SetShow removes the button visual.
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 20);
	ctx.r4.s64 = 0;
	sub_923BE208(ctx, base);  // SetEnable(false).
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 20);
	ctx.r4.s64 = 0;
	sub_923BE3B0(ctx, base);  // SetShow(false).
'@
    }
    @{
        Id       = 'multiplayer-removed-quick-match'
        Purpose  = 'Multiplayer menu events: swallow stale events from the hidden Quick Match row.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B2970'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r29.u64 = ctx.r4.u64;
	// stw r27,0(r6)
	REX_STORE_U32(ctx.r6.u32 + 0, ctx.r27.u32);
'@
        Text     = @'
	// PC port: consume stale events from the removed Quick Match control, so
	// its legacy tooltip and matchmaking action cannot dispatch.
	if (ctx.r29.u32 == REX_LOAD_U32(ctx.r31.u32 + 20)) {
		ctx.r3.s64 = 0;
		ctx.r1.s64 = ctx.r1.s64 + 144;
		__restgprlr_27(ctx, base);
		return;
	}
'@
    }
    @{
        Id       = 'skip-offline-warning'
        Purpose  = 'Gameplay start: skip the obsolete Xbox LIVE offline/leaderboard warning dialog.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B3D30'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920b4dd0
	ctx.lr = 0x920B3D44;
	sub_920B4DD0(ctx, base);
'@
        Text     = @'
	// PC port: bypass the in-game Xbox Live offline/leaderboard warning.
	// The base scene callback above still runs; only the obsolete dialog path is skipped.
	goto loc_920B3DF0;
'@
    }
    @{
        Id       = 'capture-session-stats'
        Purpose  = 'Leaderboard submission: journal the retail score data for local High Scores.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B4B60'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r6.u64 = ctx.r29.u64;
	// li r7,0
	ctx.r7.s64 = 0;
'@
        Text     = @'
	// PC port: journal the retail leaderboard submission so the local High
	// Scores implementation can use the authentic score field and timing.
	AegisWing_CaptureSessionStats(ctx.r4.u64, ctx.r5.u32, ctx.r6.u32);
'@
    }
    @{
        Id       = 'pause-scene-destroyed'
        Purpose  = 'Pause scene destructor: drop the host-drawn pause captions.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6C88'
        Kind     = 'InsertAfter'
        Anchor   = @'
DEFINE_REX_FUNC(sub_920B6C88) {
	REX_FUNC_PROLOGUE();
'@
        Text     = @'
	// PC port: the pause scene is being destroyed; its host captions go with it.
	AegisWing_OnPauseSceneDestroyed(ctx.r3.u32);
'@
    }
    @{
        Id       = 'pause-reveal-controls'
        Purpose  = 'Pause scene setup: explicitly reveal the pause root and its PC controls.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6CD8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x923b6738
	ctx.lr = 0x920B6D80;
	sub_923B6738(ctx, base);
'@
        Text     = @'
	// Some host configurations skip the original scene-show transition.
	// Explicitly reveal the pause root and its PC-relevant controls.
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 4);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 20);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 24);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 28);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 32);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 44);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 36);
	ctx.r4.s64 = 1;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 36);
	ctx.r4.s64 = 1;
	sub_923BE3B0(ctx, base);
'@
    }
    @{
        Id       = 'pause-finalize'
        Purpose  = 'Pause scene setup: register the scene for host captions; hide the Xbox-only row.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6CD8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bctrl 
	ctx.lr = 0x920B6E08;
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
'@
        Text     = @'
	// PC port: the pause scene owns its host-drawn captions (glyph loss).
	AegisWing_OnPauseSceneInit(ctx.r31.u32);
	// PC port: finalize removal of Xbox-only pause-menu rows after scene setup.
	// SetEnable prevents focus/navigation; SetShow removes the button visual.
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 40);
	ctx.r4.s64 = 0;
	sub_923BE208(ctx, base);  // SetEnable(false).
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 40);
	ctx.r4.s64 = 0;
	sub_923BE3B0(ctx, base);  // SetShow(false).
'@
    }
    @{
        Id       = 'pause-removed-buttons'
        Purpose  = 'Pause menu events: swallow stale events from the hidden Xbox-only row.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6E18'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// stw r10,0(r6)
	REX_STORE_U32(ctx.r6.u32 + 0, ctx.r10.u32);
	// lwz r10,24(r31)
'@
        Text     = @'
	// PC port: consume any stale events from removed Xbox-only controls.
	if (ctx.r4.u32 == REX_LOAD_U32(ctx.r31.u32 + 40)) {
		ctx.r3.s64 = 0;
		ctx.r1.s64 = ctx.r1.s64 + 112;
		__restgprlr_29(ctx, base);
		return;
	}
'@
    }
    @{
        Id       = 'pause-hide-labels-on-choice'
        Purpose  = 'Pause menu events: hide host captions only for real choices.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6E18'
        Kind     = 'InsertAfter'
        Anchor   = @'
	}
	ctx.r10.u64 = REX_LOAD_U32(ctx.r31.u32 + 24);
	// lbz r3,0(r11)
'@
        Text     = @'
	// PC port: hide the pause captions only for real choice dispatches.
	// Other events (child-scene close, stale) must leave them alone, or
	// answering the exit confirm blanks the revealed pause menu.
	bool pc_choice = (ctx.r4.u32 == REX_LOAD_U32(ctx.r31.u32 + 24)) ||
	    (ctx.r4.u32 == REX_LOAD_U32(ctx.r31.u32 + 28)) ||
	    (ctx.r4.u32 == REX_LOAD_U32(ctx.r31.u32 + 32)) ||
	    (ctx.r4.u32 == REX_LOAD_U32(ctx.r31.u32 + 36)) ||
	    (ctx.r4.u32 == REX_LOAD_U32(ctx.r31.u32 + 44));
	if (pc_choice) {
		AegisWing_HidePauseMenuLabels();
	}
'@
    }
    @{
        Id       = 'pause-exit-confirm-open'
        Purpose  = 'Pause menu: note that the exit confirmation opened from the pause menu.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6E18'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r7.s64 = 1;
	// addi r4,r10,-17968
	ctx.r4.s64 = ctx.r10.s64 + -17968;
'@
        Text     = @'
	AegisWing_NotePauseConfirmOpen(1);
'@
    }
    @{
        Id       = 'pause-high-scores-labels'
        Purpose  = 'Pause menu: show host captions again when High Scores is chosen.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6E18'
        Kind     = 'InsertAfter'
        Anchor   = @'
	if (!ctx.cr6.eq) goto loc_920B6E98;
	// bl 0x9212fbf0
	ctx.lr = 0x920B6E94;
'@
        Text     = @'
	AegisWing_ShowPauseMenuLabels();
'@
    }
    @{
        Id       = 'pause-help-choice'
        Purpose  = 'Pause menu: note Help & Options was chosen, so its close restores the captions.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6E18'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r11.u64 = REX_LOAD_U32(ctx.r31.u32 + 44);
	// cmplw cr6,r4,r11
	ctx.cr6.compare<uint32_t>(ctx.r4.u32, ctx.r11.u32, ctx.xer);
'@
        Text     = @'
	// PC port: A on Help & Options - the Help parent that opens lives on top
	// of the pause scene, so closing it must restore the pause captions.
	if (ctx.cr6.eq) {
		AegisWing_NotePauseHelpChoice();
	}
'@
    }
    @{
        Id       = 'pause-nav-return'
        Purpose  = 'Pause scene reveal after a child scene: decide whether its captions return.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B6F60'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bctrl 
	ctx.lr = 0x920B6FAC;
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
'@
        Text     = @'
	// PC port: decide from the pause scene's own exit result whether its host
	// captions return (No / Help closed) or stay hidden (Yes -> main menu).
	AegisWing_OnPauseNavReturn(ctx.r31.u32);
'@
    }
    @{
        Id       = 'record-high-scores'
        Purpose  = 'End-of-game statistics: record the totals as local high scores.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B7B68'
        Kind     = 'InsertAfter'
        Anchor   = @'
DEFINE_REX_FUNC(sub_920B7B68) {
	REX_FUNC_PROLOGUE();
	uint32_t ea{};
'@
        Text     = @'
	// PC port: persist the totals shown on the end-of-game statistics page
	// before Continue, High Scores, or Main Menu handles the selection.
	AegisWing_RecordCurrentHighScores(ctx.r3.u32);
'@
    }
    @{
        Id       = 'lobby-exit-confirm-open'
        Purpose  = 'Lobby: note that the exit confirmation opened outside the pause menu.'
        File     = 'aegis_wing_recomp.2.cpp'
        Function = 'sub_920B9218'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r7.s64 = 1;
	// addi r4,r11,-17968
	ctx.r4.s64 = ctx.r11.s64 + -17968;
'@
        Text     = @'
	AegisWing_NotePauseConfirmOpen(0);
'@
    }
    @{
        Id       = 'recomp3-host-declarations'
        Purpose  = 'Declare the host hooks the Help & Options and Settings patches in this file call.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = $null
        Kind     = 'InsertAfter'
        Anchor   = @'
#include "aegis_wing_init.h"
'@
        Text     = @'

void AegisWing_OpenPcSettings();
void AegisWing_SelectPcSettings();
void AegisWing_ClosePcSettings();
void AegisWing_OnHelpOptionsSceneInit();
void AegisWing_ShowHelpOptionsLabels();
void AegisWing_HideHelpOptionsLabels();
void AegisWing_CloseHelpOptionsLabels();
'@
    }
    @{
        Id       = 'help-options-scene-init'
        Purpose  = 'Help & Options setup: the host draws stable captions over its buttons.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BD710'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x923b6738
	ctx.lr = 0x920BD79C;
	sub_923B6738(ctx, base);
'@
        Text     = @'
	// Draw stable captions over the native Help & Options buttons. The retail
	// button glyph surfaces can be lost when this scene is restored from pause.
	// The scene owns them from here until it closes.
	AegisWing_OnHelpOptionsSceneInit();
'@
    }
    @{
        Id       = 'settings-row-select'
        Purpose  = 'Help & Options: only the Settings row opens the PC Settings panel.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BD7B8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r11.s64 = -1845100544;
	// addi r4,r11,-18412
	ctx.r4.s64 = ctx.r11.s64 + -18412;
'@
        Text     = @'
	// PC port: only the Settings row may open the replacement settings panel.
	AegisWing_SelectPcSettings();
'@
    }
    @{
        Id       = 'help-child-open'
        Purpose  = 'Help & Options: a child page opens, hide the captions but keep the session.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BD7B8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// PC port: only the Settings row may open the replacement settings panel.
	AegisWing_SelectPcSettings();
loc_920BD834:
'@
        Text     = @'
	// A child scene is opening; keep the Help session alive but hide its labels.
	AegisWing_HideHelpOptionsLabels();
'@
    }
    @{
        Id       = 'help-scene-close'
        Purpose  = 'Help & Options: the scene itself closes, drop its captions.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BD7B8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// li r11,255
	ctx.r11.s64 = 255;
loc_920BD870:
'@
        Text     = @'
	// The Help scene itself is closing rather than opening a child page.
	AegisWing_CloseHelpOptionsLabels();
'@
    }
    @{
        Id       = 'help-labels-after-child-1'
        Purpose  = 'Help & Options: show the captions again after a child page closes (920BD9A0).'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BD9A0'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920b1820
	ctx.lr = 0x920BD9D4;
	sub_920B1820(ctx, base);
'@
        Text     = @'
	AegisWing_ShowHelpOptionsLabels();
'@
    }
    @{
        Id       = 'help-labels-after-child-2'
        Purpose  = 'Help & Options: show the captions again after a child page closes (920BDB18).'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BDB18'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920b1820
	ctx.lr = 0x920BDB4C;
	sub_920B1820(ctx, base);
'@
        Text     = @'
	AegisWing_ShowHelpOptionsLabels();
'@
    }
    @{
        Id       = 'settings-disable-sliders'
        Purpose  = 'Settings scene setup: disable the two hidden retail sliders.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BDB70'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920bde08
	ctx.lr = 0x920BDBE0;
	sub_920BDE08(ctx, base);
'@
        Text     = @'
	// The host PC panel owns audio and navigation. Disable the two hidden
	// retail sliders so XUI cannot move between them or play selection sounds.
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 24);
	ctx.r4.s64 = 0;
	sub_923BE208(ctx, base);
	ctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 28);
	ctx.r4.s64 = 0;
	sub_923BE208(ctx, base);
'@
    }
    @{
        Id       = 'settings-scene-close'
        Purpose  = 'Settings scene close: close the host PC Settings panel with it.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BDC28'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920b1820
	ctx.lr = 0x920BDCF4;
	sub_920B1820(ctx, base);
'@
        Text     = @'
	// Keep the host PC controls synchronized with the native scene lifetime.
	AegisWing_ClosePcSettings();
'@
    }
    @{
        Id       = 'help-labels-after-child-3'
        Purpose  = 'Help & Options: show the captions again after a child page closes (920BE1A8).'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920BE1A8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x920b1820
	ctx.lr = 0x920BE238;
	sub_920B1820(ctx, base);
'@
        Text     = @'
	AegisWing_ShowHelpOptionsLabels();
'@
    }
    @{
        Id       = 'title-prompt-declarations'
        Purpose  = 'Declare the host hooks the title-screen prompt patches call.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = $null
        Kind     = 'InsertAfter'
        Anchor   = @'
void AegisWing_CloseHelpOptionsLabels();
'@
        Text     = @'
bool AegisWing_TitleShowsGamepadPrompt();
void AegisWing_ApplyTitlePromptText(uint32_t buffer);
'@
    }
    @{
        Id       = 'title-prompt-icon'
        Purpose  = 'Title screen: draw the A icon only while a gamepad is connected.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920C3F68'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// cmpwi cr6,r25,2
	ctx.cr6.compare<int32_t>(ctx.r25.s32, 2, ctx.xer);
	// bne cr6,0x920c447c
	if (!ctx.cr6.eq) goto loc_920C447C;
'@
        Text     = @'
	// PC port: without a gamepad the prompt reads "Press Any Key" (no A icon).
	if (!AegisWing_TitleShowsGamepadPrompt()) goto loc_920C447C;
'@
    }
    @{
        Id       = 'title-prompt-text'
        Purpose  = 'Title screen: replace "Press (A) to continue" with "Press Any Key" without a gamepad.'
        File     = 'aegis_wing_recomp.3.cpp'
        Function = 'sub_920C3F68'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x921344e8
	ctx.lr = 0x920C4750;
	sub_921344E8(ctx, base);
'@
        Text     = @'
	// PC port: the prompt string was just copied to r1+1152; without a
	// gamepad it becomes "Press Any Key".
	AegisWing_ApplyTitlePromptText(ctx.r1.u32 + 1152);
'@
    }
    @{
        Id       = 'recomp7-host-declarations'
        Purpose  = 'Declare the host hooks the input and High Scores patches in this file call.'
        File     = 'aegis_wing_recomp.7.cpp'
        Function = $null
        Kind     = 'InsertAfter'
        Anchor   = @'
#include "aegis_wing_init.h"
'@
        Text     = @'

bool AegisWing_ShouldForcePcSettingsConfirm();
bool AegisWing_IsPcSettingsOpen();
void AegisWing_OpenHighScores();
bool AegisWing_IsHighScoresOpen();
uint32_t AegisWing_EmptyResultCode();
bool AegisWing_ConsumePcSettingsNavKeystroke(uint16_t* out_vk,
                                             uint16_t* out_flags);
void AegisWing_WriteGuestKeystroke(uint32_t address, uint16_t vk,
                                   uint16_t flags);
'@
    }
    @{
        Id       = 'achievements-to-high-scores'
        Purpose  = 'Achievements UI call: open local High Scores instead of the Xbox achievements UI.'
        File     = 'aegis_wing_recomp.7.cpp'
        Function = 'sub_9212FBF0'
        Kind     = 'Replace'
        Find     = @'
	// li r4,0
	ctx.r4.s64 = 0;
	// b 0x923e3bdc
	__imp__XamShowAchievementsUI(ctx, base);
'@
        Text     = @'
	// PC port: Xbox achievements are unavailable in this offline build. Reuse
	// every remaining Achievements action for the local High Scores screen.
	AegisWing_OpenHighScores();
	ctx.r3.s64 = 0;
'@
    }
    @{
        Id       = 'input-state-buffer'
        Purpose  = 'XamInputGetState wrapper: keep the guest state buffer address.'
        File     = 'aegis_wing_recomp.7.cpp'
        Function = 'sub_9212FCD8'
        Kind     = 'InsertAfter'
        Anchor   = @'
DEFINE_REX_FUNC(sub_9212FCD8) {
	REX_FUNC_PROLOGUE();
'@
        Text     = @'
	const uint32_t state_address = ctx.r4.u32;
'@
    }
    @{
        Id       = 'input-state-filter'
        Purpose  = 'XamInputGetState wrapper: hide buttons from XUI while High Scores or PC Settings is open.'
        File     = 'aegis_wing_recomp.7.cpp'
        Function = 'sub_9212FCD8'
        Kind     = 'InsertAfter'
        Anchor   = @'
	ctx.r4.s64 = 0;
	// b 0x923e3c1c
	__imp__XamInputGetState(ctx, base);
'@
        Text     = @'
	// Keep native XUI from reacting to the same buttons used by the host High
	// Scores overlay. The host reads the controller directly.
	if (ctx.r3.u32 == 0 && AegisWing_IsHighScoresOpen()) {
		REX_STORE_U16(state_address + 4, 0);
		REX_STORE_U16(state_address + 8, 0);
		REX_STORE_U16(state_address + 10, 0);
		REX_STORE_U16(state_address + 12, 0);
		REX_STORE_U16(state_address + 14, 0);
	}
	// Keep the hidden retail audio page from responding to the replacement
	// panel's D-pad/stick navigation or its buttons. Inject one A only when the
	// replacement asks the native scene to save and close.
	if (ctx.r3.u32 == 0 && AegisWing_IsPcSettingsOpen()) {
		uint16_t buttons = 0;
		if (AegisWing_ShouldForcePcSettingsConfirm()) {
			buttons |= uint16_t(0x1000);
		}
		REX_STORE_U16(state_address + 4, buttons);
		REX_STORE_U16(state_address + 8, 0);
		REX_STORE_U16(state_address + 10, 0);
		REX_STORE_U16(state_address + 12, 0);
		REX_STORE_U16(state_address + 14, 0);
	}
'@
    }
    @{
        Id       = 'keystroke-buffer'
        Purpose  = 'XamInputGetKeystrokeEx wrapper: keep the guest keystroke buffer address.'
        File     = 'aegis_wing_recomp.7.cpp'
        Function = 'sub_9212FD00'
        Kind     = 'InsertAfter'
        Anchor   = @'
DEFINE_REX_FUNC(sub_9212FD00) {
	REX_FUNC_PROLOGUE();
'@
        Text     = @'
	uint32_t pc_ks_buffer = ctx.r5.u32;
'@
    }
    @{
        Id       = 'keystroke-filter'
        Purpose  = 'XamInputGetKeystrokeEx wrapper: synthesize PC Settings navigation; swallow keys under High Scores.'
        File     = 'aegis_wing_recomp.7.cpp'
        Function = 'sub_9212FD00'
        Kind     = 'InsertAfter'
        Anchor   = @'
	// bl 0x923e3c3c
	ctx.lr = 0x9212FD2C;
	__imp__XamInputGetKeystrokeEx(ctx, base);
'@
        Text     = @'
	// PC port: while the PC settings panel is open, synthesize one D-pad
	// press/release keystroke pair per panel row change so the native scene
	// plays its navigation sound for every move.
	if (AegisWing_IsPcSettingsOpen()) {
		uint16_t ks_vk = 0;
		uint16_t ks_flags = 0;
		if (AegisWing_ConsumePcSettingsNavKeystroke(&ks_vk, &ks_flags)) {
			AegisWing_WriteGuestKeystroke(pc_ks_buffer, ks_vk, ks_flags);
			ctx.r3.u64 = 0;
		}
	}
	// PC port: while the local High Scores overlay is open the host consumes
	// button events itself; report "no keystroke" so the overlay's close key
	// cannot fall through to the pause menu. The real queue is drained above.
	if (AegisWing_IsHighScoresOpen()) {
		ctx.r3.u64 = AegisWing_EmptyResultCode();
	}
'@
    }
)

# Generated code and the here-strings above are compared with LF line endings,
# whatever this script file or a checkout uses.
function ConvertTo-Lf([string]$Text) { return $Text.Replace("`r`n", "`n") }

function Get-OccurrenceCount([string]$Haystack, [string]$Needle) {
    $count = 0
    $start = 0
    while (($index = $Haystack.IndexOf($Needle, $start, [StringComparison]::Ordinal)) -ge 0) {
        $count++
        $start = $index + 1
    }
    return $count
}

# The span of one generated function: from its DEFINE_REX_FUNC line to the
# closing brace in column 0. Without a function, the whole file.
function Get-PatchScope([string]$Source, $Patch) {
    if (-not $Patch.Function) {
        return @{ Start = 0; Length = $Source.Length }
    }
    $header = "DEFINE_REX_FUNC($($Patch.Function)) {`n"
    $found = Get-OccurrenceCount $Source $header
    if ($found -ne 1) {
        $message = ("Patch '{0}': function {1} was found {2} times in {3}; expected once. " +
            'The generated code does not match the build this patch was written for.') -f
        $Patch.Id, $Patch.Function, $found, $Patch.File
        throw $message
    }
    $start = $Source.IndexOf($header, [StringComparison]::Ordinal)
    $end = $Source.IndexOf("`n}`n", $start, [StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "Patch '$($Patch.Id)': the end of function $($Patch.Function) in $($Patch.File) was not found."
    }
    return @{ Start = $start; Length = $end + 3 - $start }
}

# Returns the patched source, or $null when the patch is already in place.
function Invoke-Patch([string]$Source, $Patch) {
    $scope = Get-PatchScope $Source $Patch
    $body = $Source.Substring($scope.Start, $scope.Length)
    $where = if ($Patch.Function) { "function $($Patch.Function) of $($Patch.File)" } else { $Patch.File }
    $text = ConvertTo-Lf ($Patch.Text + "`n")

    switch ($Patch.Kind) {
        'InsertAfter' {
            $anchor = ConvertTo-Lf ($Patch.Anchor + "`n")
            if ($body.Contains($anchor + $text)) { return $null }
            $found = Get-OccurrenceCount $body $anchor
            if ($found -ne 1) {
                $message = ("Patch '{0}' ({1}): its anchor was found {2} times in {3}; expected once. " +
                    'The generated code differs from what this patch was written for.') -f
                $Patch.Id, $Patch.Purpose, $found, $where
                throw $message
            }
            $at = $scope.Start + $body.IndexOf($anchor, [StringComparison]::Ordinal) + $anchor.Length
            return $Source.Substring(0, $at) + $text + $Source.Substring($at)
        }
        'Replace' {
            $find = ConvertTo-Lf ($Patch.Find + "`n")
            $found = Get-OccurrenceCount $body $find
            if ($found -eq 0 -and $body.Contains($text)) { return $null }
            if ($found -ne 1) {
                $message = ("Patch '{0}' ({1}): the code it replaces was found {2} times in {3}; expected once. " +
                    'The generated code differs from what this patch was written for.') -f
                $Patch.Id, $Patch.Purpose, $found, $where
                throw $message
            }
            $at = $scope.Start + $body.IndexOf($find, [StringComparison]::Ordinal)
            return $Source.Substring(0, $at) + $text + $Source.Substring($at + $find.Length)
        }
        default { throw "Patch '$($Patch.Id)' has an unknown kind '$($Patch.Kind)'." }
    }
}

# Plan every patch in memory, file by file, in table order.
$sources = [ordered]@{}
$report = New-Object System.Collections.Generic.List[string]
$applied = 0
$alreadyApplied = 0
foreach ($patch in $patches) {
    if (-not $sources.Contains($patch.File)) {
        $path = Join-Path $GeneratedDir $patch.File
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Patch '$($patch.Id)': $path does not exist. Run the code generator first."
        }
        $original = [System.IO.File]::ReadAllText($path)
        $sources[$patch.File] = @{ Path = $path; Original = $original; Text = (ConvertTo-Lf $original) }
    }
    $entry = $sources[$patch.File]
    $result = Invoke-Patch $entry.Text $patch
    $where = if ($patch.Function) { "$($patch.File) $($patch.Function)" } else { $patch.File }
    if ($null -eq $result) {
        $alreadyApplied++
        $report.Add(("  already in place  {0,-34} {1}" -f $patch.Id, $where))
    }
    else {
        $entry.Text = $result
        $applied++
        $report.Add(("  {0,-16}  {1,-34} {2}" -f $(if ($Check) { 'MISSING' } else { 'patched' }), $patch.Id, $where))
    }
}

# Reached only when every patch found its place.
$report | ForEach-Object { Write-Host $_ }

if ($Check) {
    if ($applied -gt 0) {
        Write-Host "$applied of $($patches.Count) patches are missing from $GeneratedDir."
        exit 1
    }
    Write-Host "All $($patches.Count) patches are in place in $GeneratedDir."
    exit 0
}

# Every patch found its place: write the files that changed.
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($entry in $sources.Values) {
    if ($entry.Text -ne $entry.Original) {
        [System.IO.File]::WriteAllText($entry.Path, $entry.Text, $utf8)
    }
}
Write-Host "Applied $applied patches ($alreadyApplied already in place) to $GeneratedDir."
