// PresetDump.hpp — DEV-ONLY. Writes the current patch as module-nested JSON (PM-style) to a
// timestamped file in $HOME. Compiled in only when STRUCKROOM_PRESET_DUMP=1 (see CMake option); the
// UI triggers it on a keystroke. Uses the kPresetKeys mapping (module, item pair per param) from
// PresetKeys.hpp, shared with the always-on preset loader. Author new presets by dialing in a
// sound, dumping, then moving the JSON into Presets/ (rename it — the file stem or meta.name
// becomes the preset name) and rebuilding.

#ifndef STRUCKROOM_PRESETDUMP_HPP
#define STRUCKROOM_PRESETDUMP_HPP

#if STRUCKROOM_PRESET_DUMP

#include "StruckRoomParams.h"
#include "PresetKeys.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace struckroom {

// Emit modules in this order; every module in kPresetKeys must appear here.
static constexpr const char* kPresetModuleOrder[] = {
   "Global", "Strike", "ModalBank", "Expression",
};

// Build the module-nested JSON for `values` (kParamCount entries). Values are stringified like PM
// (ints as "0"/"1", floats to 6 dp).
inline std::string buildPresetJson(const float* values, const char* name = "Untitled")
{
   char iso[32] = "";
   const std::time_t t = std::time(nullptr);
   std::strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));

   std::string j;
   j += "{\n";
   j += "  \"app\": { \"name\": \"StruckRoom\", \"version\": \"1.0.0\" },\n";
   j += "  \"plugin\": { \"format\": \"clap\", \"id\": \"com.unlikelyware.struckroom\" },\n";
   j += "  \"meta\": { \"author\": \"\", \"createdAt\": \"" + std::string(iso)
        + "\", \"name\": \"" + name + "\" },\n";
   j += "  \"schema\": \"struckroom.preset\",\n";
   j += "  \"schemaVersion\": 1,\n";
   j += "  \"state\": {\n";
   j += "    \"parameterState\": {\n";

   const int nMods = static_cast<int>(sizeof(kPresetModuleOrder) / sizeof(kPresetModuleOrder[0]));
   for (int m = 0; m < nMods; ++m)
   {
      const char* mod = kPresetModuleOrder[m];
      j += "      \"" + std::string(mod) + "\": {\n";
      bool first = true;
      for (uint32_t i = 0; i < kParamCount; ++i)
      {
         if (std::strcmp(kPresetKeys[i].module, mod) != 0)
            continue;
         if (i == kParamMpe)   // controller setup, not sound: presets leave it alone (PresetKeys.hpp)
            continue;
         char val[32];
         if (kParams[i].integer)
            std::snprintf(val, sizeof val, "%d", static_cast<int>(std::lround(values[i])));
         else
            std::snprintf(val, sizeof val, "%.6f", values[i]);
         if (!first) j += ",\n";
         first = false;
         j += "        \"" + std::string(kPresetKeys[i].item) + "\": \"" + val + "\"";
      }
      j += "\n      }";
      j += (m + 1 < nMods) ? ",\n" : "\n";
   }

   j += "    }\n";
   j += "  }\n";
   j += "}\n";
   return j;
}

// Write the patch to $HOME/struckroom-preset-<epoch>.json. Returns the path, or "" on failure.
inline std::string dumpPresetToFile(const float* values)
{
   const std::string json = buildPresetJson(values);
   const char* home = std::getenv("HOME");
   std::string path = std::string(home ? home : ".") + "/struckroom-preset-"
                    + std::to_string(static_cast<long long>(std::time(nullptr))) + ".json";
   FILE* f = std::fopen(path.c_str(), "w");
   if (f == nullptr)
      return "";
   std::fwrite(json.data(), 1, json.size(), f);
   std::fclose(f);
   return path;
}

} // namespace struckroom

#endif // STRUCKROOM_PRESET_DUMP
#endif // STRUCKROOM_PRESETDUMP_HPP
