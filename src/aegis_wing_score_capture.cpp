#include "aegis_wing_score_capture.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include <rex/filesystem.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

#include "generated/default/aegis_wing_init.h"
#include "aegis_wing_app.h"

namespace aegis_wing {
namespace {

constexpr uint32_t kViewSize = 12;
constexpr uint32_t kPropertySize = 24;
constexpr uint32_t kMaximumViews = 8;
constexpr uint32_t kMaximumProperties = 16;
constexpr uint8_t kInt64Type = 2;

std::mutex capture_mutex;
std::atomic_uint64_t next_submission{1};

uint32_t ReadU32(rex::memory::Memory* memory, uint32_t address) {
  const auto* value = memory->TranslateVirtual<const uint32_t*>(address);
  return rex::memory::load_and_swap<uint32_t>(value);
}

uint64_t ReadU64(rex::memory::Memory* memory, uint32_t address) {
  const auto* value = memory->TranslateVirtual<const uint64_t*>(address);
  return rex::memory::load_and_swap<uint64_t>(value);
}

uint8_t ReadU8(rex::memory::Memory* memory, uint32_t address) {
  const auto* value = memory->TranslateVirtual<const uint8_t*>(address);
  return *value;
}

std::string UtcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream text;
  text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return text.str();
}

}  // namespace

void CaptureSessionStats(uint64_t xuid, uint32_t view_count,
                         uint32_t views_address) {
  auto* memory = AegisWingApp::GetGuestMemory();
  if (!memory || !views_address || !view_count ||
      view_count > kMaximumViews) {
    return;
  }

  std::lock_guard lock(capture_mutex);

  const auto user_data =
      rex::filesystem::GetExecutableFolder() / "userdata";
  std::error_code error;
  std::filesystem::create_directories(user_data, error);
  if (error) {
    return;
  }

  const auto journal_path = user_data / "high_score_diagnostics.tsv";
  const bool add_header = !std::filesystem::exists(journal_path, error) ||
                          std::filesystem::file_size(journal_path, error) == 0;
  std::ofstream journal(journal_path, std::ios::app);
  if (!journal) {
    return;
  }

  if (add_header) {
    journal << "timestamp_utc\tsubmission\txuid\tview_id\tproperty_id"
               "\ttype\tvalue_int64\n";
  }

  const uint64_t submission =
      next_submission.fetch_add(1, std::memory_order_relaxed);
  const std::string timestamp = UtcTimestamp();

  for (uint32_t view_index = 0; view_index < view_count; ++view_index) {
    const uint32_t view_address = views_address + view_index * kViewSize;
    const uint32_t view_id = ReadU32(memory, view_address);
    const uint32_t property_count = ReadU32(memory, view_address + 4);
    const uint32_t properties_address = ReadU32(memory, view_address + 8);
    if (!properties_address || property_count > kMaximumProperties) {
      continue;
    }

    for (uint32_t property_index = 0; property_index < property_count;
         ++property_index) {
      const uint32_t property_address =
          properties_address + property_index * kPropertySize;
      const uint32_t property_id = ReadU32(memory, property_address);
      const uint8_t type = ReadU8(memory, property_address + 8);
      const int64_t value =
          type == kInt64Type
              ? static_cast<int64_t>(ReadU64(memory, property_address + 16))
              : 0;

      journal << timestamp << '\t' << submission << '\t' << "0x"
              << std::uppercase << std::hex << std::setw(16)
              << std::setfill('0') << xuid << '\t' << std::dec << view_id
              << '\t' << "0x" << std::uppercase << std::hex << std::setw(8)
              << std::setfill('0') << property_id << '\t' << std::dec
              << static_cast<uint32_t>(type) << '\t' << value << '\n';
    }
  }
}

}  // namespace aegis_wing
