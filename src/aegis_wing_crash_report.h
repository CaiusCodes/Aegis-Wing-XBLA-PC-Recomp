#pragma once

struct PPCContext;

namespace aegis_wing {

// Installs a last-chance Windows exception handler that writes diagnostics
// beside the executable. This is intentionally a no-op on other platforms.
void InstallCrashReporter();

// Associates the current host thread with its live guest register context so
// fatal reports can include PPC state alongside the native stack.
void TrackGuestContext(PPCContext* context, const char* checkpoint);

}  // namespace aegis_wing
