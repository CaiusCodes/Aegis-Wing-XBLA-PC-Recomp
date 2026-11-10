// Check for Updates (Help & Options, Settings): asks GitHub for the newest
// release of this port, only when the player chooses it. Nothing but the
// request itself is sent (a User-Agent naming this version).
#pragma once

#include <string>

namespace aegis_wing {

enum class UpdateState { kIdle, kChecking, kUpToDate, kAvailable, kFailed };

// Starts a check on a background thread (ignored while one is running).
void StartUpdateCheck();
UpdateState GetUpdateState();
// The newest release's version ("0.9.3") once a check has finished.
std::string GetLatestVersion();
// Opens the project's releases page in the default browser.
void OpenReleasesPage();

}  // namespace aegis_wing
