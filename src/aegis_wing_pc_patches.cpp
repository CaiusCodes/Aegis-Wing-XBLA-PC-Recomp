#include "aegis_wing_pc_patches.h"

#include <cstdint>
#include <string_view>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

namespace aegis_wing {
namespace {

void PatchUtf16Be(rex::Runtime* runtime, uint32_t address,
                  size_t original_length, std::string_view replacement) {
  auto* memory = runtime->memory();
  auto* heap = memory->LookupHeap(address);
  const auto byte_count =
      static_cast<uint32_t>((original_length + 1) * 2);
  uint32_t old_protect = 0;
  if (!heap || !heap->Protect(
                   address, byte_count,
                   rex::memory::kMemoryProtectRead |
                       rex::memory::kMemoryProtectWrite,
                   &old_protect)) {
    REXLOG_ERROR("Could not unprotect PC menu text at {:#010x}", address);
    return;
  }

  auto* destination = memory->TranslateVirtual<uint8_t*>(address);
  const size_t count =
      replacement.size() < original_length ? replacement.size()
                                           : original_length;

  // Clear the complete fixed-width guest slot first. Some XUI paths retain
  // the original string's capacity, so leaving bytes beyond the new null
  // terminator can produce intermittent stale or missing glyphs.
  for (uint32_t index = 0; index < byte_count; ++index) {
    destination[index] = 0;
  }
  for (size_t index = 0; index < count; ++index) {
    destination[index * 2 + 1] =
        static_cast<uint8_t>(replacement[index]);
  }
  heap->Protect(address, byte_count, old_protect);
}

void PatchUtf16BeByPrefix(rex::Runtime* runtime, uint32_t search_begin,
                          uint32_t search_end, std::string_view prefix,
                          std::string_view replacement) {
  auto* memory = runtime->memory();
  const auto* source =
      memory->TranslateVirtual<const uint8_t*>(search_begin);
  const size_t search_size = search_end - search_begin;
  const size_t prefix_bytes = prefix.size() * 2;

  for (size_t offset = 0; offset + prefix_bytes <= search_size;
       offset += 2) {
    bool matches = true;
    for (size_t index = 0; index < prefix.size(); ++index) {
      if (source[offset + index * 2] != 0 ||
          source[offset + index * 2 + 1] !=
              static_cast<uint8_t>(prefix[index])) {
        matches = false;
        break;
      }
    }
    if (!matches) {
      continue;
    }

    size_t original_length = 0;
    while (offset + (original_length + 1) * 2 <= search_size &&
           (source[offset + original_length * 2] != 0 ||
            source[offset + original_length * 2 + 1] != 0)) {
      ++original_length;
    }
    if (replacement.size() > original_length) {
      REXLOG_ERROR("Replacement for PC text at {:#010x} is too long",
                   search_begin + static_cast<uint32_t>(offset));
      return;
    }

    const uint32_t address =
        search_begin + static_cast<uint32_t>(offset);
    PatchUtf16Be(runtime, address, original_length, replacement);
    REXLOG_INFO("Patched PC text at {:#010x}", address);
    return;
  }

  REXLOG_ERROR("Could not find requested PC text in the guest image");
}

}  // namespace

void ApplyPcPatches(rex::Runtime* runtime) {
  // Main-menu tooltip strings. The button caption itself lives in Wingmen.xzp
  // and is patched in the staged archive by tools/Patch-PcMenuText.ps1.
  PatchUtf16Be(runtime, 0x92059780, 23, "Exit game");
  PatchUtf16Be(runtime, 0x920597B2, 16, "Desktop");
  PatchUtf16Be(runtime, 0x92059660, 50, "Co-op play with 1-4 friends");

  // The pause scene sets its title caption from this guest string at runtime,
  // so clearing the static XUR caption is not enough. The host overlay draws
  // "GAME PAUSED" itself; blank the string the title would render (same
  // length, so the fixed-width slot stays valid).
  PatchUtf16BeByPrefix(
      runtime, 0x92050000, 0x92070000, "GAME PAUSED",
      "           ");

  PatchUtf16BeByPrefix(
      runtime, 0x92050000, 0x92070000,
      "Are you sure you want to exit now? Your progress will not be saved",
      "Are you sure you want to exit now? Your progress will not be saved and "
      "Europa will fall.");

  // The multiplayer scene's descriptions for the two rows the PC port turns
  // into LAN play. They are set from these guest strings as the focus moves,
  // so the staged archive cannot carry them.
  PatchUtf16BeByPrefix(runtime, 0x92050000, 0x92400000,
                       "Choose an Xbox Live Player",
                       "Find a game on your LAN or virtual LAN");
  PatchUtf16BeByPrefix(runtime, 0x92050000, 0x92400000,
                       "Host a custom Xbox Live Player",
                       "Host a game on your LAN or virtual LAN");
  REXLOG_INFO("Applied Aegis Wing PC menu text patches");
}

}  // namespace aegis_wing
