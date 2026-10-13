// Aegis Wing XBLA PC Recomp - launcher: the PC menu changes in Media\Wingmen.xzp.
//
// A C++ port of tools\Patch-PcMenuText.ps1, which stays for developers (the
// CMake build still runs it on the development copy). It makes the same edits
// and nothing else, in the same order, so both produce identical archives.
//
// Wingmen.xzp is an "XUIZ" archive of "XUIB" resources (the compiled XUI menu
// scenes). The edits are: PC wording for the Xbox menu captions, blanked
// captions the host UI draws itself, moved controls, and rewritten focus
// chains for the rows that PC removes. Every caption and record is checked
// before it is changed, so another build's archive is refused, not damaged.
//
// An archive that already carries the changes is reported as AlreadyPatched
// (LooksPatched); the importer never patches twice.
#include "launcher.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace aw {
namespace {

struct Mismatch : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// UTF-16 big-endian bytes of ASCII/BMP text.
std::vector<uint8_t> Utf16Be(const std::wstring& text) {
  std::vector<uint8_t> out;
  out.reserve(text.size() * 2);
  for (wchar_t c : text) {
    out.push_back(static_cast<uint8_t>((c >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(c & 0xFF));
  }
  return out;
}

std::wstring FromUtf16Be(const uint8_t* data, size_t chars) {
  std::wstring out;
  out.reserve(chars);
  for (size_t i = 0; i < chars; ++i) out.push_back(static_cast<wchar_t>((data[i * 2] << 8) | data[i * 2 + 1]));
  return out;
}

// PowerShell's -eq on strings ignores case, and the script compared names and
// captions that way; the port does too so both accept the same archives.
bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
  return a.size() == b.size() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring Lower(std::wstring text) {
  for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
  return text;
}

class Archive {
 public:
  explicit Archive(std::vector<uint8_t>& bytes) : b_(bytes) {}

  uint32_t U16(size_t at) const { return (uint32_t(Byte(at)) << 8) | Byte(at + 1); }
  uint32_t U32(size_t at) const {
    return (uint32_t(Byte(at)) << 24) | (uint32_t(Byte(at + 1)) << 16) | (uint32_t(Byte(at + 2)) << 8) | Byte(at + 3);
  }
  void SetU32(size_t at, uint32_t value) {
    Need(at, 4);
    for (int k = 0; k < 4; ++k) b_[at + k] = static_cast<uint8_t>((value >> (24 - 8 * k)) & 0xFF);
  }
  uint8_t Byte(size_t at) const {
    if (at >= b_.size()) throw Mismatch("the archive ends where more data was expected");
    return b_[at];
  }

  struct Entry {
    size_t offset;
    size_t size;
  };

  Entry FindEntry(const std::wstring& name) const {
    if (b_.size() < 0x16 || memcmp(b_.data(), "XUIZ", 4) != 0) throw Mismatch("not an XUIZ archive");
    const size_t table_size = U32(0x10);
    const size_t entry_count = U16(0x14);
    const size_t data_start = 0x16 + table_size;
    size_t cursor = 0x16;
    for (size_t i = 0; i < entry_count; ++i) {
      const size_t entry_size = U32(cursor);
      const size_t entry_offset = U32(cursor + 4);
      const size_t name_length = Byte(cursor + 8);
      Need(cursor + 9, name_length * 2);
      const std::wstring entry_name = FromUtf16Be(&b_[cursor + 9], name_length);
      if (EqualNoCase(entry_name, name)) return {data_start + entry_offset, entry_size};
      cursor += 9 + name_length * 2;
    }
    throw Mismatch("XUIZ entry not found");
  }

  // Replaces every occurrence of old_text (UTF-16BE) by new_text padded with
  // spaces to the same length. Returns how many were replaced (may be 0).
  int SetFixedWidthText(const std::wstring& old_text, const std::wstring& new_text) {
    if (new_text.size() > old_text.size()) throw Mismatch("replacement text is longer");
    const std::vector<uint8_t> old_bytes = Utf16Be(old_text);
    std::wstring padded = new_text;
    padded.resize(old_text.size(), L' ');
    const std::vector<uint8_t> new_bytes = Utf16Be(padded);
    int matches = 0;
    for (size_t offset = 0; offset + old_bytes.size() <= b_.size(); ++offset) {
      if (memcmp(&b_[offset], old_bytes.data(), old_bytes.size()) != 0) continue;
      memcpy(&b_[offset], new_bytes.data(), new_bytes.size());
      ++matches;
      offset += old_bytes.size() - 1;
    }
    return matches;
  }

  // The string table of an XUIB resource: [base+0x18] its offset, [base+0x1C] its size.
  struct Strings {
    size_t begin, size;
  };
  Strings StringTable(const Entry& entry) const {
    if (memcmp(&b_.at(entry.offset), "XUIB", 4) != 0) throw Mismatch("entry is not an XUIB resource");
    return {entry.offset + U32(entry.offset + 0x18), U32(entry.offset + 0x1C)};
  }

  // Overwrites the one caption (a length byte, then the text) with `replacement`
  // (same length). Used for both blanking and equal-length renames.
  int ReplaceCaption(const std::wstring& entry_name, const std::wstring& caption, const std::vector<uint8_t>& replacement) {
    const Entry entry = FindEntry(entry_name);
    const Strings table = StringTable(entry);
    const std::vector<uint8_t> text = Utf16Be(caption);
    int matches = 0;
    if (table.size < text.size() + 1) throw Mismatch("string table too small");
    const size_t last = table.begin + table.size - text.size() - 1;
    for (size_t offset = table.begin; offset <= last; ++offset) {
      if (Byte(offset) != caption.size()) continue;
      if (memcmp(&b_.at(offset + 1), text.data(), text.size()) != 0) continue;
      memcpy(&b_[offset + 1], replacement.data(), replacement.size());
      ++matches;
      offset += text.size();
    }
    if (matches != 1) throw Mismatch("expected one caption");
    return matches;
  }

  int ClearCaption(const std::wstring& entry_name, const std::wstring& caption) {
    if (caption.size() > 255) throw Mismatch("caption too long");
    return ReplaceCaption(entry_name, caption, Utf16Be(std::wstring(caption.size(), L' ')));
  }

  int SetCaption(const std::wstring& entry_name, const std::wstring& old_caption, const std::wstring& new_caption) {
    if (new_caption.size() > old_caption.size()) throw Mismatch("replacement caption is longer");
    std::wstring padded = new_caption;
    padded.resize(old_caption.size(), L' ');
    return ReplaceCaption(entry_name, old_caption, Utf16Be(padded));
  }

  // Replaces a caption with one of any length, with no padding. XUIB strings
  // are referenced by ordinal, never by offset, so resizing one only shifts
  // what follows it: the resource's later sections and total size, then the
  // archive's later entries and file size.
  int SetCaptionResized(const std::wstring& entry_name, const std::wstring& old_caption,
                        const std::wstring& new_caption) {
    if (new_caption.size() == old_caption.size()) return SetCaption(entry_name, old_caption, new_caption);
    if (new_caption.size() > 255) throw Mismatch("caption too long");
    const Entry entry = FindEntry(entry_name);
    const size_t base = entry.offset;
    const Strings table = StringTable(entry);

    // Walk the table record by record (u16 length, UTF-16BE text).
    bool found = false;
    size_t record = 0;
    for (size_t cursor = table.begin; cursor < table.begin + table.size;) {
      const size_t length = U16(cursor);
      if (length == old_caption.size() && cursor + 2 + length * 2 <= b_.size() &&
          EqualNoCase(FromUtf16Be(&b_[cursor + 2], length), old_caption)) {
        if (found) throw Mismatch("more than one matching caption");
        record = cursor;
        found = true;
      }
      cursor += 2 + length * 2;
    }
    if (!found) throw Mismatch("caption not found");

    const std::vector<uint8_t> text = Utf16Be(new_caption);
    const int64_t delta = (static_cast<int64_t>(new_caption.size()) - static_cast<int64_t>(old_caption.size())) * 2;
    const size_t old_end = record + 2 + old_caption.size() * 2;
    std::vector<uint8_t> resized(static_cast<size_t>(static_cast<int64_t>(b_.size()) + delta));
    memcpy(resized.data(), b_.data(), record);
    resized[record] = 0;
    resized[record + 1] = static_cast<uint8_t>(new_caption.size());
    memcpy(&resized[record + 2], text.data(), text.size());
    memcpy(&resized[old_end + delta], &b_[old_end], b_.size() - old_end);
    b_.swap(resized);

    // The resource: its total size, the string section's size, and the offsets
    // of the sections stored after it.
    const uint32_t string_offset = U32(base + 0x18);
    SetU32(base + 0x0E, static_cast<uint32_t>(U32(base + 0x0E) + delta));
    SetU32(base + 0x1C, static_cast<uint32_t>(table.size + delta));
    const size_t section_count = U16(base + 0x12);
    for (size_t section = 0; section < section_count; ++section) {
      const size_t field = base + 0x14 + section * 12 + 4;
      const uint32_t offset = U32(field);
      if (offset > string_offset) SetU32(field, static_cast<uint32_t>(offset + delta));
    }

    // The archive: this entry's size, the offsets of the entries stored after
    // it, and the file size.
    const size_t table_size = U32(0x10);
    const size_t entry_count = U16(0x14);
    const size_t data_start = 0x16 + table_size;
    const size_t entry_offset = base - data_start;
    size_t cursor = 0x16;
    for (size_t i = 0; i < entry_count; ++i) {
      const uint32_t offset = U32(cursor + 4);
      if (offset == entry_offset) {
        SetU32(cursor, static_cast<uint32_t>(U32(cursor) + delta));
      } else if (offset > entry_offset) {
        SetU32(cursor + 4, static_cast<uint32_t>(offset + delta));
      }
      cursor += 9 + static_cast<size_t>(Byte(cursor + 8)) * 2;
    }
    SetU32(0x08, static_cast<uint32_t>(b_.size()));
    return 1;
  }

  // Vector tables hold 12-byte (x, y, z) float triples; only y is touched.
  size_t VectorY(const Entry& entry, int vector) const {
    const size_t section = entry.offset + U32(entry.offset + 0x24);
    const size_t count = U32(entry.offset + 0x28) / 12;
    if (vector < 0 || static_cast<size_t>(vector) >= count) throw Mismatch("vector index is out of range");
    return section + static_cast<size_t>(vector) * 12 + 4;
  }

  void CopyVectorY(const std::wstring& entry_name, int destination, int source) {
    const Entry entry = FindEntry(entry_name);
    const size_t to = VectorY(entry, destination);
    const size_t from = VectorY(entry, source);
    Need(to, 4);
    Need(from, 4);
    memmove(&b_[to], &b_[from], 4);
  }

  int SetVectorY(const std::wstring& entry_name, int vector, float value) {
    const Entry entry = FindEntry(entry_name);
    const size_t at = VectorY(entry, vector);
    Need(at, 4);
    uint32_t bits;
    memcpy(&bits, &value, 4);
    int changed = 0;
    for (int k = 0; k < 4; ++k) {
      const uint8_t next = static_cast<uint8_t>((bits >> (24 - 8 * k)) & 0xFF);
      if (b_[at + k] != next) changed = 1;
      b_[at + k] = next;
    }
    return changed;
  }

  // Non-empty strings in table order get ordinals 1, 2, 3...; the table's
  // length bytes are read the way the script read them (see the .ps1).
  std::map<std::wstring, int> StringOrdinals(const Entry& entry) const {
    const Strings table = StringTable(entry);
    std::map<std::wstring, int> ordinals;  // keys lower-cased: PowerShell hashtables ignore case
    int ordinal = 0;
    size_t cursor = table.begin;
    while (cursor < table.begin + table.size) {
      const size_t length = Byte(cursor);
      ++cursor;
      Need(cursor, length * 2);
      const std::wstring value = FromUtf16Be(&b_[cursor], length);
      cursor += length * 2;
      if (length > 0) {
        ++ordinal;
        ordinals[Lower(value)] = ordinal;
      }
    }
    return ordinals;
  }

  int Ordinal(const std::map<std::wstring, int>& ordinals, const std::wstring& name) const {
    const auto it = ordinals.find(Lower(name));
    if (it == ordinals.end()) throw Mismatch("an XUIB string is missing");
    return it->second;
  }

  int SetButtonNavigation(const std::wstring& entry_name, const std::wstring& button, const std::wstring& expected_up,
                          const std::wstring& expected_down, const std::wstring& new_up,
                          const std::wstring& new_down) {
    const Entry entry = FindEntry(entry_name);
    const auto ordinals = StringOrdinals(entry);
    for (const std::wstring* name : {&button, &expected_up, &expected_down, &new_up, &new_down}) Ordinal(ordinals, *name);
    std::vector<int> classes = {Ordinal(ordinals, L"XuiButton")};
    if (ordinals.count(L"xuinavbutton")) classes.push_back(ordinals.at(L"xuinavbutton"));
    const int button_ordinal = Ordinal(ordinals, button);
    const int expected_up_ordinal = Ordinal(ordinals, expected_up);
    const int expected_down_ordinal = Ordinal(ordinals, expected_down);
    const int new_up_ordinal = Ordinal(ordinals, new_up);
    const int new_down_ordinal = Ordinal(ordinals, new_down);
    const size_t data = entry.offset + U32(entry.offset + 0x30);
    const size_t data_size = U32(entry.offset + 0x34);

    // A serialized button record stores its non-empty string-table ordinal at
    // +8. The exact navigation-field offset varies slightly by XUI class and
    // resource version, so the expected 00/up/00/down/00 pattern is located below.
    std::vector<size_t> matches;
    for (size_t offset = data; offset + 35 <= data + data_size; ++offset) {
      const int kind = Byte(offset);
      if (std::find(classes.begin(), classes.end(), kind) == classes.end() || Byte(offset + 1) != 1 ||
          Byte(offset + 2) != 0 || Byte(offset + 8) != button_ordinal)
        continue;
      matches.push_back(offset);
    }
    if (matches.size() != 1) throw Mismatch("expected one serialized button");

    const size_t record = matches[0];
    int navigation = -1;
    for (int relative = 25; relative <= 30; ++relative) {
      const int up = Byte(record + relative);
      const int down = Byte(record + relative + 2);
      const bool separators = Byte(record + relative - 1) == 0 && Byte(record + relative + 1) == 0 &&
                              Byte(record + relative + 3) == 0;
      const bool expected = (up == expected_up_ordinal || up == new_up_ordinal) &&
                            (down == expected_down_ordinal || down == new_down_ordinal);
      if (separators && expected) {
        navigation = relative;
        break;
      }
    }
    if (navigation < 0) throw Mismatch("could not locate the navigation targets");

    int changes = 0;
    if (Byte(record + navigation) != new_up_ordinal) {
      b_[record + navigation] = static_cast<uint8_t>(new_up_ordinal);
      ++changes;
    }
    if (Byte(record + navigation + 2) != new_down_ordinal) {
      b_[record + navigation + 2] = static_cast<uint8_t>(new_down_ordinal);
      ++changes;
    }
    return changes;
  }

  int SetSceneDefaultFocus(const std::wstring& entry_name, const std::wstring& expected_id, const std::wstring& new_id) {
    const Entry entry = FindEntry(entry_name);
    const auto ordinals = StringOrdinals(entry);
    const int expected = Ordinal(ordinals, expected_id);
    const int wanted = Ordinal(ordinals, new_id);
    const size_t data = entry.offset + U32(entry.offset + 0x30);
    const size_t data_size = U32(entry.offset + 0x34);

    // The scene record stores its custom class and default-focus id in the
    // sequence 30 02 00 01 00 <class> 01 01 00 <focus> 00 00.
    std::vector<size_t> matches;
    for (size_t offset = data; offset + 12 <= data + data_size; ++offset) {
      if (Byte(offset) == 0x30 && Byte(offset + 1) == 0x02 && Byte(offset + 2) == 0 && Byte(offset + 3) == 1 &&
          Byte(offset + 4) == 0 && Byte(offset + 6) == 1 && Byte(offset + 7) == 1 && Byte(offset + 8) == 0 &&
          (Byte(offset + 9) == expected || Byte(offset + 9) == wanted) && Byte(offset + 10) == 0 &&
          Byte(offset + 11) == 0)
        matches.push_back(offset);
    }
    if (matches.size() != 1) throw Mismatch("expected one default-focus record");
    const size_t focus = matches[0] + 9;
    if (Byte(focus) == wanted) return 0;
    b_[focus] = static_cast<uint8_t>(wanted);
    return 1;
  }

  // True if the (UTF-16BE) text occurs inside the entry's string table.
  bool TableContains(const std::wstring& entry_name, const std::wstring& text) const {
    const Entry entry = FindEntry(entry_name);
    const Strings table = StringTable(entry);
    const std::vector<uint8_t> needle = Utf16Be(text);
    if (table.begin + table.size > b_.size() || needle.size() > table.size) return false;
    return std::search(b_.begin() + table.begin, b_.begin() + table.begin + table.size, needle.begin(), needle.end()) !=
           b_.begin() + table.begin + table.size;
  }

 private:
  void Need(size_t at, size_t count) const {
    if (at + count > b_.size() || at + count < at) throw Mismatch("the archive ends where more data was expected");
  }

  std::vector<uint8_t>& b_;
};

bool LooksPatched(std::vector<uint8_t>& bytes) {
  try {
    Archive a(bytes);
    return a.TableContains(L"MainMenu.xur", L"High Scores") &&
           a.TableContains(L"MultiplayerMenu.xur", L"Join LAN Game") &&
           a.TableContains(L"MultiplayerMenu.xur", L"Create LAN Game") &&
           !a.TableContains(L"SettingsMenu.xur", L"Audio Settings");
  } catch (const std::exception&) {
    return false;
  }
}

int ApplyAll(std::vector<uint8_t>& bytes) {
  Archive a(bytes);
  int patched = 0;
  patched += a.SetFixedWidthText(L"Return to Arcade", L"Exit Game");
  patched += a.SetFixedWidthText(L"Quit game and return to", L"Exit game");
  patched += a.SetFixedWidthText(L"Xbox Live Arcade", L"Desktop");
  patched += a.SetFixedWidthText(L"Multiplayer Match", L"Multiplayer");
  patched += a.SetCaptionResized(L"SettingsMenu.xur", L"Audio Settings", L"Settings");

  // The PC settings layer replaces this scene's two retail sliders and confirm
  // prompt. The scene, background, title and its native save/return behaviour
  // stay; the superseded controls move safely outside the viewport.
  patched += a.SetVectorY(L"SettingsMenu.xur", 3, 2000.0f);
  patched += a.SetVectorY(L"SettingsMenu.xur", 4, 2000.0f);
  patched += a.SetVectorY(L"SettingsMenu.xur", 5, 2000.0f);

  // These four native button captions intermittently lose their cached glyph
  // surface after returning to Help & Options from a child scene while paused.
  // The native buttons, focus and highlight stay; the host UI draws stable
  // captions at the same positions.
  patched += a.ClearCaption(L"HelpAndOptionsMenu.xur", L"How To Play");
  patched += a.ClearCaption(L"HelpAndOptionsMenu.xur", L"Controls");
  patched += a.ClearCaption(L"HelpAndOptionsMenu.xur", L"Credits");
  patched += a.ClearCaption(L"HelpAndOptionsMenu.xur", L"Settings");

  // Control ids stay intact so the recompiled menu code can initialize and
  // disable the Xbox-only rows, but their visible captions go; the remaining
  // PC rows are compacted into the vacated slots.
  patched += a.SetCaption(L"MainMenu.xur", L"Leaderboards", L"High Scores");
  patched += a.ClearCaption(L"MainMenu.xur", L"Achievements");
  patched += a.ClearCaption(L"PauseMenu.xur", L"Leaderboards");
  // The pause Achievements row is reborn as the local High Scores entry; its
  // retail caption is blanked like the others and the host UI draws "High Scores".
  patched += a.ClearCaption(L"PauseMenu.xur", L"Achievements");
  // The pause scene's own captions also intermittently lose their cached glyph
  // surface after gameplay; the host UI draws stable equivalents, so the
  // native strings are blanked to prevent double text when the cache survives.
  patched += a.ClearCaption(L"PauseMenu.xur", L"Resume Game");
  patched += a.ClearCaption(L"PauseMenu.xur", L"Help & Options\r\n");
  patched += a.ClearCaption(L"PauseMenu.xur", L"Exit Game");
  patched += a.ClearCaption(L"PauseMenu.xur", L"GAME PAUSED");
  patched += a.SetCaption(L"LevelFailMenu.xur", L"Achievements", L"High Scores");
  patched += a.SetCaption(L"GameCompleteMenu.xur", L"Achievements", L"High Scores");
  // The main menu's "A Select" legend says nothing a PC player needs (the
  // mouse selects by pointing): move it out of the viewport (vector 9 is its
  // position; the control stays so the scene initializes as before).
  patched += a.SetVectorY(L"MainMenu.xur", 9, 2000.0f);
  // The three local PC rows in the order of their focus chain.
  a.CopyVectorY(L"MainMenu.xur", 2, 3);   // Exit Game -> former Help row
  a.CopyVectorY(L"MainMenu.xur", 3, 7);   // Help & Options -> former Achievements row
  a.CopyVectorY(L"PauseMenu.xur", 6, 4);  // Exit Game -> row immediately after Help
  // The pause menu carries four rows now: Resume, High Scores, Help & Options,
  // Exit Game. The moved rows are re-spaced evenly; Resume keeps its slot.
  patched += a.SetVectorY(L"PauseMenu.xur", 5, 114.5f);
  patched += a.SetVectorY(L"PauseMenu.xur", 7, 191.5f);
  patched += a.SetVectorY(L"PauseMenu.xur", 6, 268.5f);

  // The compiled focus chains are rewritten so navigation bypasses the removed
  // rows in both directions (visibility alone does not alter XUI's explicit
  // up/down links).
  patched += a.SetButtonNavigation(L"MainMenu.xur", L"btnMultiplayer", L"btnSinglePlayer", L"btnLeaderboards", L"btnSinglePlayer", L"btnLeaderboards");
  patched += a.SetButtonNavigation(L"MainMenu.xur", L"btnLeaderboards", L"btnMultiplayer", L"btnAchievements", L"btnMultiplayer", L"btnHelpOptions");
  patched += a.SetButtonNavigation(L"MainMenu.xur", L"btnHelpOptions", L"btnAchievements", L"btnExit", L"btnLeaderboards", L"btnExit");
  patched += a.SetButtonNavigation(L"PauseMenu.xur", L"btnResume", L"btnMainMenu", L"btnHelpOptions", L"btnMainMenu", L"btnAchievements");
  patched += a.SetButtonNavigation(L"PauseMenu.xur", L"btnHelpOptions", L"btnResume", L"btnLeaderBoards", L"btnAchievements", L"btnMainMenu");
  patched += a.SetButtonNavigation(L"PauseMenu.xur", L"btnAchievements", L"btnLeaderBoards", L"btnMainMenu", L"btnResume", L"btnHelpOptions");
  patched += a.SetButtonNavigation(L"PauseMenu.xur", L"btnMainMenu", L"btnAchievements", L"btnResume", L"btnHelpOptions", L"btnResume");

  // The multiplayer scene carries three PC rows: the two LAN entries that
  // replace the Xbox LIVE matchmaking ones, and local co-op. Quick Match has no
  // PC equivalent; its caption goes and the recompiled scene code hides the row.
  patched += a.ClearCaption(L"MultiplayerMenu.xur", L"Quick Match");
  patched += a.SetCaptionResized(L"MultiplayerMenu.xur", L"Custom Match", L"Join LAN Game");
  patched += a.SetCaptionResized(L"MultiplayerMenu.xur", L"Create Match", L"Create LAN Game");
  patched += a.ClearCaption(L"MultiplayerMenu.xur", L"Game Experience May Change During Online Play");

  // The gap the hidden Quick Match row leaves closes: the three PC rows move up
  // one slot, keeping the retail spacing.
  patched += a.SetVectorY(L"MultiplayerMenu.xur", 3, 403.5f);   // Join LAN Game
  patched += a.SetVectorY(L"MultiplayerMenu.xur", 2, 463.33f);  // Create LAN Game
  patched += a.SetVectorY(L"MultiplayerMenu.xur", 1, 523.17f);  // Local Multiplayer

  // Focus starts on Join LAN Game and cycles through the three visible rows.
  patched += a.SetSceneDefaultFocus(L"MultiplayerMenu.xur", L"btnQuickMatch", L"btnCustomMatch");
  patched += a.SetButtonNavigation(L"MultiplayerMenu.xur", L"btnCustomMatch", L"btnQuickMatch", L"btnCreateGame", L"btnCoop", L"btnCreateGame");
  patched += a.SetButtonNavigation(L"MultiplayerMenu.xur", L"btnCoop", L"btnCreateGame", L"btnQuickMatch", L"btnCreateGame", L"btnCustomMatch");
  return patched;
}

}  // namespace

MenuPatch PatchMenuArchive(std::vector<uint8_t>& archive, int* count, std::wstring* why) {
  *count = 0;
  // Work on a copy, so a mismatch half way leaves the caller's bytes as they were.
  std::vector<uint8_t> work = archive;
  try {
    *count = ApplyAll(work);
  } catch (const std::exception& e) {
    if (LooksPatched(archive)) return MenuPatch::AlreadyPatched;
    *why = Widen(e.what());
    return MenuPatch::Unexpected;
  }
  archive.swap(work);
  return MenuPatch::Patched;
}

}  // namespace aw
