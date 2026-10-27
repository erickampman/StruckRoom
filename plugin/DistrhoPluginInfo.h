// DistrhoPluginInfo.h — compile-time description of the StruckRoom plugin for DPF.
//
// IMPORTANT: the DISTRHO_PLUGIN_* *identity* macros below are INERT. StruckRoom does NOT use
// dpf_add_plugin: it implements the CLAP entry point itself in src/plugin.cpp. That is deliberate
// and load-bearing — DPF's generated CLAP wrapper ignores CLAP_EVENT_MIDI2 outright
// (dpf/distrho/src/DistrhoPluginCLAP.cpp: "case CLAP_EVENT_MIDI2: break;") and advertises only
// CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE, so a DPF-generated plugin can never receive
// MIDI 2.0 7.9-pitch NoteOns. See src/plugin.cpp's note-ports extension.
//
// The DISTRHO_UI_* macros DO bind: DPF is used here purely as the GUI layer (NanoVG widgets driven
// through UIExporter), exactly as PlainOldSynth does it.

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND       "Unlikelyware"
#define DISTRHO_PLUGIN_NAME        "StruckRoom"
#define DISTRHO_PLUGIN_URI         "https://unlikelyware.com/struckroom"
#define DISTRHO_PLUGIN_CLAP_ID     "com.unlikelyware.struckroom"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_BRAND_ID    Unlk
#define DISTRHO_PLUGIN_UNIQUE_ID   StRm

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_SYNTH        1   // note input + CLAP "instrument"; enables MIDI in
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0
#define DISTRHO_PLUGIN_WANT_STATE      0
// Gates UI::getPluginInstancePointer() (DistrhoUI.hpp), which the GUI shell uses to recover the
// clap_plugin_t* UIExporter was constructed with. Inert for the raw CLAP plugin itself.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1   // knobs/labels drawn with NanoVG vector primitives
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_FILE_BROWSER        1   // Save / Load's file dialog (UI::openFileBrowser)
#define DISTRHO_UI_DEFAULT_WIDTH       1080
#define DISTRHO_UI_DEFAULT_HEIGHT      640   // 2 cols × 3 rows (spectrum takes the top-left cell)

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
