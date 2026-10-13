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
inline std::filesystem::path UserDataPath() { return DataRoot() / "userdata"; }

// The settings file: userdata\aegis_wing.toml in the release, beside the exe
// in every other build.
inline std::filesystem::path ConfigPath() {
  const auto root = DataRoot();
  return root == rex::filesystem::GetExecutableFolder() ? root / "aegis_wing.toml"
                                                        : root / "userdata" / "aegis_wing.toml";
}

}  // namespace aegis_wing
