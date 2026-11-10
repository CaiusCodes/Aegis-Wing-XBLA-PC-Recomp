// A few log lines at startup that make a stutter or speed report readable:
// Windows, CPU, GPU and driver, every display's exact refresh rate, and the
// settings that change timing. Ported from the Perfect Dark port.
#pragma once

namespace rex::memory {
class Memory;
}

namespace aegis_wing {

void LogSystemInfo();

// vblank_timing_log: every 10 s, the game's own frame counter and the game
// clock (the guest timebase, which the game divides by its queried frequency)
// against real time.
void StartGameStepLog(rex::memory::Memory* memory);

}  // namespace aegis_wing
