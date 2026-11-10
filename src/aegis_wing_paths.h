// Where the game finds its data.
//
// The portable release keeps the game in <release>\resources\runtime (started
// by the launcher in the release root), its game data in resources\assets and
// the player's saves and settings in resources\userdata. A game exe that sits
// in a folder named "runtime" uses that layout; every other build (the
// development output folder, an old release's Game folder) keeps everything
// beside the exe, as before.
#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#include <rex/filesystem.h>

namespace aegis_wing {

// The folder holding assets\ and userdata\: resources\ in the release,
// otherwise the exe's own folder.
inline std::filesystem::path DataRoot() {
  const auto exe = rex::filesystem::GetExecutableFolder();
  std::string name = exe.filename().string();
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name == "runtime" ? exe.parent_path() : exe;
}

inline std::filesystem::path AssetsPath() { return DataRoot() / "assets"; }

// True in the release layout (this exe in resources\runtime).
inline bool ReleaseLayout() { return DataRoot() != rex::filesystem::GetExecutableFolder(); }

// Saves, settings and caches: resources\userdata, or in the release the
// folder the player chose in setup, which the launcher writes as one UTF-8
// line to resources\userdata-location.txt (empty: the default).
inline std::filesystem::path UserDataPath() {
  const auto root = DataRoot();
  if (ReleaseLayout()) {
    std::ifstream file(root / "userdata-location.txt", std::ios::binary);
    std::string line;
    if (file && std::getline(file, line)) {
      if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      const std::filesystem::path chosen(
          std::u8string(reinterpret_cast<const char8_t*>(line.data()), line.size()));
      if (!line.empty() && chosen.is_absolute()) return chosen;
    }
  }
  return root / "userdata";
}

// The settings file: aegis_wing.toml in the save folder in the release,
// beside the exe in every other build.
inline std::filesystem::path ConfigPath() {
  return ReleaseLayout() ? UserDataPath() / "aegis_wing.toml" : DataRoot() / "aegis_wing.toml";
}

}  // namespace aegis_wing
