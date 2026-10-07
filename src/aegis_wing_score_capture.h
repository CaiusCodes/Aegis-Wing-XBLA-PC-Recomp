#pragma once

#include <cstdint>

namespace aegis_wing {

// Records the raw leaderboard values submitted by the retail game. This is a
// temporary diagnostic journal used to identify the final-score field and the
// exact submission timing before local High Scores are enabled for players.
void CaptureSessionStats(uint64_t xuid, uint32_t view_count,
                         uint32_t views_address);

}  // namespace aegis_wing
