// PresetDump.hpp — DEV-ONLY. Writes the current patch as preset JSON (PresetFile.hpp) to a
// timestamped file in $HOME. Compiled in only when STRUCKROOM_PRESET_DUMP=1 (see CMake option); the
// UI triggers it on a keystroke. Author new presets by dialing in a sound, dumping, then moving the
// JSON into Presets/ (rename it — the file stem or meta.name becomes the preset name) and
// rebuilding. The UI's Save writes the same format, so a saved patch works as well.

#ifndef STRUCKROOM_PRESETDUMP_HPP
#define STRUCKROOM_PRESETDUMP_HPP

#if STRUCKROOM_PRESET_DUMP

#include "PresetFile.hpp"

#include <cstdlib>
#include <ctime>
#include <string>

namespace struckroom {

// Write the patch to $HOME/struckroom-preset-<epoch>.json. Returns the path, or "" on failure.
inline std::string dumpPresetToFile(const float* values)
{
   const char* home = std::getenv("HOME");
   std::string path = std::string(home ? home : ".") + "/struckroom-preset-"
                    + std::to_string(static_cast<long long>(std::time(nullptr))) + ".json";
   return writePresetFile(path, values, "Untitled") ? path : "";
}

} // namespace struckroom

#endif // STRUCKROOM_PRESET_DUMP
#endif // STRUCKROOM_PRESETDUMP_HPP
