cmake_minimum_required(VERSION 3.21)   # clap-wrapper needs 3.21

# C is needed for DPF's pugl / dgl sources.
# macOS: 26 or later, Apple Silicon only. Set before project();
# ignored on other platforms. Both are cache variables, so -D overrides them.
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "macOS 26 or later")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Apple Silicon only")
project(StruckRoom VERSION 0.9.0 LANGUAGES C CXX)

# Everything is linked into the .clap, a shared module, so every static library in it (the engine,
# the plugin core, DPF's DGL) must be position-independent. clang on Linux defaults to PIE and
# hides this; gcc does not ("relocation R_X86_64_PC32 ... recompile with -fPIC"). As PlainOldSynth.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)          # PM framework uses std::numbers / std::span
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STRUCKROOM_WERROR "Treat warnings as errors (framework + tests)" OFF)
option(STRUCKROOM_PRESET_DUMP "Dev-only: press 'P' in the UI to dump the patch as JSON to \$HOME" OFF)
option(STRUCKROOM_BUILD_VST3 "Build a VST3 wrapper around the same plugin via clap-wrapper." ON)
option(STRUCKROOM_BUILD_AUV2 "Build an AUv2 (.component) wrapper via clap-wrapper (macOS only)." ${APPLE})

if(APPLE)
    # DPF's DistrhoUI_macOS.mm and clap-wrapper's AUv2 sources are Objective-C++.
    enable_language(OBJCXX)
endif()

# --- PM framework port -----------------------------------------------------
# Vendored PolygraphModular framework + our shim/MIDI layer, as a static lib. Compiled with
# relaxed warnings (vendored code, its own style); our glue and tests stay strict. PIC so it
# can be linked into the shared .clap module.
add_library(pm_framework STATIC
    src/Common/pm_midi.cpp
    src/Common/pm_param_data.cpp
    src/Module/SinOscillatorModule.cpp
    src/Module/IFSCore.cpp
    src/Module/IFSOscModule.cpp
    src/Module/LPFilterModule.cpp
    src/Module/LevelModule.cpp
    src/Module/IFSEnvModule.cpp
    src/Module/IFSLFOModule.cpp
    src/Module/CVMixerModule.cpp
    src/Module/ADSRHelper.cpp
    src/Module/ADSRModule.cpp
    src/Module/CCConverterModule.cpp
    src/Module/ChannelPressureModule.cpp
    src/Module/CombineModule.cpp
    src/Module/FinalModule.cpp
    src/Module/SlewModule.cpp
    src/Module/PitchBendConverterModule.cpp
    src/Module/PitchConverterModule.cpp
    src/Module/ModalBankModule.cpp
    src/Module/ModalBank.cpp
    src/Module/Room.cpp
    src/Module/RoomModule.cpp
    src/Module/StrikeModule.cpp
    src/Module/ModuleFactory.cpp
    src/Voice/ModuleMap.cpp
    src/Voice/pm_module_specs.cpp
    src/Voice/VoiceStateManager.cpp
    src/Voice/Voice.cpp
    src/Voice/VoiceManager.cpp)
set_target_properties(pm_framework PROPERTIES POSITION_INDEPENDENT_CODE ON)
# All framework folders on the include path so the vendored `#include "X.hpp"` resolve
# across Module/ Voice/ Common/ (mirrors PolygraphModular's layout).
target_include_directories(pm_framework PUBLIC src/Common src/Module src/Voice)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(pm_framework PRIVATE -Wall -Wno-unknown-pragmas -Wno-unused-parameter)
    # The room-mode loop sums thousands of resonators per sample; -ffast-math lets it vectorise
    # those two sums (reassociation). Room.cpp alone: nothing else needs it.
    set_source_files_properties(src/Module/Room.cpp PROPERTIES COMPILE_OPTIONS "-ffast-math")
endif()

# --- presets: Presets/*.json compiled in as a C++ array, not loaded at runtime -------------
# Factory presets need no file on disk and don't depend on CLAP's own preset-discovery extension —
# the UI presents its own dropdown. (The UI's Save / Load reads and writes the same JSON for the
# user's own patches: src/UI/PresetFile.hpp.) Adding/removing a preset file requires
# reconfiguring (CONFIGURE_DEPENDS re-globs on build, but a brand new file still needs `cmake -B
# build -S .` rerun once to pick up the new match).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB STRUCKROOM_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Presets/*.json")
set(STRUCKROOM_GENERATED_PRESETS_H "${CMAKE_BINARY_DIR}/generated/GeneratedPresets.h")
add_custom_command(
    OUTPUT "${STRUCKROOM_GENERATED_PRESETS_H}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gen_presets.py"
            ${STRUCKROOM_PRESET_FILES} -o "${STRUCKROOM_GENERATED_PRESETS_H}"
    DEPENDS ${STRUCKROOM_PRESET_FILES} "${CMAKE_SOURCE_DIR}/tools/gen_presets.py"
    COMMENT "Generating embedded presets from Presets/*.json")
add_custom_target(generate_presets DEPENDS "${STRUCKROOM_GENERATED_PRESETS_H}")

# --- embedded resources: Resources/* compiled in as raw bytes ----------------------------
# Uses DPF's own res2c.py (ships in the dpf/ submodule) to embed the logo PNG's raw file bytes;
# NanoVG's createImageFromMemory() decodes it at UI-construction time. No filesystem dependency
# on the resource file once built — same reasoning as the preset codegen above.
set(STRUCKROOM_RESOURCES_HPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.hpp")
set(STRUCKROOM_RESOURCES_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.cpp")
# res2c.py embeds a whole folder and names each symbol by the file's stem, so help.md (the source of
# help.html) would collide with help.html. Copy everything but *.md into a staging folder first.
file(GLOB STRUCKROOM_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Resources/*")
list(FILTER STRUCKROOM_RESOURCE_FILES EXCLUDE REGEX "\\.md$")
set(STRUCKROOM_RESOURCES_STAGING "${CMAKE_BINARY_DIR}/generated/Resources")
add_custom_command(
    OUTPUT "${STRUCKROOM_RESOURCES_HPP}" "${STRUCKROOM_RESOURCES_CPP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKROOM_RESOURCES_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKROOM_RESOURCES_STAGING}"
    COMMAND ${CMAKE_COMMAND} -E copy ${STRUCKROOM_RESOURCE_FILES} "${STRUCKROOM_RESOURCES_STAGING}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
            EmbeddedResources "${STRUCKROOM_RESOURCES_STAGING}" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS ${STRUCKROOM_RESOURCE_FILES} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
    COMMENT "Embedding Resources/* as compiled-in data")
add_custom_target(generate_resources DEPENDS "${STRUCKROOM_RESOURCES_HPP}" "${STRUCKROOM_RESOURCES_CPP}")

# --- DPF is used ONLY for the GUI ------------------------------------------
# UIExporter + NanoVG + pugl. The CLAP entry point is ours (src/plugin.cpp), because DPF's
# generated CLAP wrapper cannot receive MIDI 2.0 note events — see that file's header.
add_subdirectory(dpf)
# dgl-opengl is normally created by dpf_add_plugin()'s UI_TYPE branch; call the same helper
# directly. SHARED_RESOURCES=TRUE embeds DGL's DejaVu font (fontFace(NANOVG_DEJAVU_SANS_TTF));
# USE_FILE_BROWSER=TRUE builds the file dialog Save / Load open (DISTRHO_UI_FILE_BROWSER).
dpf__add_dgl_opengl(TRUE TRUE FALSE)
# StruckRoom's own namespace (UmpireDAW's docs/decisions.md, "DPF fork"): on macOS pugl names its
# Objective-C classes after it, so two DPF binaries with the same one in a host collide.
target_compile_definitions(dgl-opengl PUBLIC DGL_NAMESPACE=StruckRoomDGL)
# DGL is vendored, so its warnings are not ours to fix: gcc's "<cstdbool> is deprecated" #warning
# from pugl.hpp under C++17+, and a stringop-overflow false positive in nanovg's stb_image.h.
target_compile_options(dgl-opengl PRIVATE -w)

# --- struckroom_core: the whole plugin (DSP + host glue + GUI shell) as one static archive ----
# Shared by the .clap output below. Hidden visibility for our own objects, so clap_entry — marked
# CLAP_EXPORT in plugin.cpp — is the only symbol this target exports (the vendored engine and DGL
# libraries linked in keep their own default visibility).
add_library(struckroom_core STATIC
    src/plugin.cpp
    src/UI/StruckRoomUI.cpp
    "${STRUCKROOM_RESOURCES_CPP}"              # generated: logo, font and help page bytes (see above)
    "${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp"
    "${DPF_ROOT_DIR}/distrho/src/DistrhoUtils.cpp")
set_target_properties(struckroom_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
target_include_directories(struckroom_core PUBLIC
    src                               # plugin_gui.h (the DSP <-> GUI seam)
    plugin                            # DistrhoPluginInfo.h + StruckRoomParams.h
    external/clap/include             # <clap/clap.h> (the raw entry point)
    "${CMAKE_BINARY_DIR}/generated"   # GeneratedPresets.h + EmbeddedResources.hpp
    "${DPF_ROOT_DIR}/distrho")        # DistrhoUI.hpp / DistrhoUIInternal.hpp
# clap-wrapper's plugin-side extension header only (clapwrapper/vst3.h: which VST3 note expressions to
# offer). Header-only and plain C, so the CLAP build needs it even with STRUCKROOM_BUILD_VST3 off.
target_include_directories(struckroom_core PRIVATE "${CMAKE_SOURCE_DIR}/external/clap-wrapper/include")
target_compile_definitions(struckroom_core PUBLIC
    DISTRHO_PLUGIN_TARGET_CLAP
    DISTRHO_IS_STANDALONE=0
    STRUCKROOM_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(struckroom_core PUBLIC pm_framework dgl-opengl)
add_dependencies(struckroom_core generate_presets generate_resources)
# These two reach pugl.hpp through DPF's private UI headers; mute only its <cstdbool> #warning.
set_source_files_properties(src/UI/StruckRoomUI.cpp "${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp"
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>")
dpf__add_plugin_specific_ui_sources(struckroom_core FALSE)   # macOS: DistrhoUI_macOS.mm
if(APPLE)
    # The live Retina scale of the host window (see the file's header comment).
    target_sources(struckroom_core PRIVATE src/UI/BackingScale_mac.mm)
endif()
# Dev-only JSON patch dumper (keystroke → file); compiled out of release builds.
if(STRUCKROOM_PRESET_DUMP)
    target_compile_definitions(struckroom_core PRIVATE STRUCKROOM_PRESET_DUMP=1)
endif()

# --- the .clap output ------------------------------------------------------
# A MODULE with no sources of its own would be rejected by CMake, and simply linking the archive is
# not enough: nothing references clap_entry, so the linker would drop the object that defines it.
# Hence the empty placeholder plus a whole-archive force-load.
function(struckroom_force_load_whole_archive TARGET_NAME LIB_NAME)
    if(APPLE)
        target_link_options(${TARGET_NAME} PRIVATE "-Wl,-force_load,$<TARGET_FILE:${LIB_NAME}>")
    elseif(UNIX)
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,--whole-archive" "$<TARGET_FILE:${LIB_NAME}>" "-Wl,--no-whole-archive")
    endif()
endfunction()

set(STRUCKROOM_EMPTY_SOURCE "${CMAKE_BINARY_DIR}/generated/struckroom_empty.c")
# A genuinely empty file trips -Wempty-translation-unit under -Wpedantic -Werror, and ISO C requires
# at least one declaration anyway (clap-wrapper applies -Wpedantic -Werror to the AUv2 target).
set(_struckroom_empty_content "typedef int struckroom_empty_translation_unit_marker;\n")
set(_struckroom_empty_existing "")
if(EXISTS "${STRUCKROOM_EMPTY_SOURCE}")
    file(READ "${STRUCKROOM_EMPTY_SOURCE}" _struckroom_empty_existing)
endif()
if(NOT _struckroom_empty_existing STREQUAL _struckroom_empty_content)
    file(WRITE "${STRUCKROOM_EMPTY_SOURCE}" "${_struckroom_empty_content}")
endif()

add_library(StruckRoom MODULE "${STRUCKROOM_EMPTY_SOURCE}")
target_link_libraries(StruckRoom PRIVATE struckroom_core)
struckroom_force_load_whole_archive(StruckRoom struckroom_core)

if(APPLE)
    set_target_properties(StruckRoom PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unlikelyware.struckroom"
        MACOSX_BUNDLE_BUNDLE_NAME "StruckRoom"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
else()
    set_target_properties(StruckRoom PROPERTIES PREFIX "" SUFFIX ".clap")
endif()

# --- install: symlink the built .clap into the user CLAP dir ---------------
# A separate target (mirroring PlainOldSynth), not a POST_BUILD step, so an ordinary `ninja` build
# never writes outside the build tree. rm -rf first: create_symlink cannot replace a real bundle
# directory, and older DPF-based builds left a symlink to build/bin/StruckRoom.clap behind.
if(APPLE)
    set(STRUCKROOM_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    set(STRUCKROOM_CLAP_ARTIFACT "$<TARGET_BUNDLE_DIR:StruckRoom>")
else()
    set(STRUCKROOM_CLAP_DIR "$ENV{HOME}/.clap")
    set(STRUCKROOM_CLAP_ARTIFACT "$<TARGET_FILE:StruckRoom>")
endif()
add_custom_target(install-clap
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKROOM_CLAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKROOM_CLAP_DIR}/StruckRoom.clap"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${STRUCKROOM_CLAP_ARTIFACT}"
        "${STRUCKROOM_CLAP_DIR}/StruckRoom.clap"
    DEPENDS StruckRoom
    VERBATIM
    COMMENT "Symlinking StruckRoom.clap into ${STRUCKROOM_CLAP_DIR}")

# --- VST3 / AUv2 via the vendored clap-wrapper, wrapping the very same CLAP ------------------
# Purely additive: neither block touches the .clap target above. As PlainOldSynth.
if(STRUCKROOM_BUILD_VST3 OR STRUCKROOM_BUILD_AUV2)
    if(NOT CLAP_SDK_ROOT)
        set(CLAP_SDK_ROOT "${CMAKE_SOURCE_DIR}/external/clap" CACHE PATH "" FORCE)
    endif()
    add_subdirectory(external/clap-wrapper)
endif()

if(STRUCKROOM_BUILD_VST3)
    if(NOT VST3_SDK_ROOT)
        set(VST3_SDK_ROOT "${CMAKE_SOURCE_DIR}/external/vst3sdk" CACHE PATH "" FORCE)
    endif()

    add_library(StruckRoom_vst3 MODULE "${STRUCKROOM_EMPTY_SOURCE}")
    if(UNIX AND NOT APPLE)
        # clap-wrapper's Linux branch tests a misspelled TCLP_RESOURCE_DIRECTORY, which is never
        # set, so it always warns that RESOURCE_DIRECTORY is unsupported. Defining it empty
        # silences that; we pass no RESOURCE_DIRECTORY.
        set(TCLP_RESOURCE_DIRECTORY "")
    endif()
    target_link_libraries(StruckRoom_vst3 PRIVATE struckroom_core)
    struckroom_force_load_whole_archive(StruckRoom_vst3 struckroom_core)
    target_add_vst3_wrapper(
        TARGET StruckRoom_vst3
        OUTPUT_NAME "StruckRoom"
        BUNDLE_IDENTIFIER "com.unlikelyware.struckroom.vst3"
        BUNDLE_VERSION "${PROJECT_VERSION}")
    if(TARGET base-sdk-vst3 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # The VST3 SDK still uses std::wstring_convert, deprecated since C++17.
        target_compile_options(base-sdk-vst3 PRIVATE -Wno-deprecated-declarations)
    endif()

    # Hosts want the bundle folder. On Linux $<TARGET_FILE> is the .so inside it; clap-wrapper
    # builds the bundle in build/Release (or build/Debug). As Retune 9a2b6eb.
    if(APPLE)
        set(STRUCKROOM_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
        set(STRUCKROOM_VST3_ARTIFACT "$<TARGET_BUNDLE_DIR:StruckRoom_vst3>")
    else()
        set(STRUCKROOM_VST3_DIR "$ENV{HOME}/.vst3")
        set(STRUCKROOM_VST3_ARTIFACT
            "${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/StruckRoom.vst3")
    endif()
    add_custom_target(install-vst3
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKROOM_VST3_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKROOM_VST3_DIR}/StruckRoom.vst3"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${STRUCKROOM_VST3_ARTIFACT}"
            "${STRUCKROOM_VST3_DIR}/StruckRoom.vst3"
        DEPENDS StruckRoom_vst3
        VERBATIM
        COMMENT "Symlinking StruckRoom.vst3 into ${STRUCKROOM_VST3_DIR}")
endif()

if(STRUCKROOM_BUILD_AUV2)
    # AUv2 is an Apple-only format, and clap-wrapper's target_add_auv2_wrapper does not exist off
    # Apple platforms. Say so plainly instead of failing with an unknown-command error.
    if(NOT APPLE)
        message(FATAL_ERROR
            "STRUCKROOM_BUILD_AUV2 is macOS-only (AUv2 is an Apple plugin format). "
            "Reconfigure with -DSTRUCKROOM_BUILD_AUV2=OFF.")
    endif()
    # clap-wrapper fetches Apple's AudioUnitSDK itself (CPM) unless pointed at a local copy with
    # -DAUDIOUNIT_SDK_ROOT=...
    if(NOT AUDIOUNIT_SDK_ROOT)
        set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON)
    endif()

    add_library(StruckRoom_auv2 MODULE "${STRUCKROOM_EMPTY_SOURCE}")
    target_link_libraries(StruckRoom_auv2 PRIVATE struckroom_core)
    struckroom_force_load_whole_archive(StruckRoom_auv2 struckroom_core)
    # "StRm" (StruckRoom) as the subtype, under Unlikelyware's own "nLKL" manufacturer code.
    target_add_auv2_wrapper(
        TARGET StruckRoom_auv2
        OUTPUT_NAME "StruckRoom"
        BUNDLE_IDENTIFIER "com.unlikelyware.struckroom.auv2"
        BUNDLE_VERSION "${PROJECT_VERSION}"
        MANUFACTURER_NAME "Unlikelyware"
        MANUFACTURER_CODE "nLKL"
        SUBTYPE_CODE "StRm"
        INSTRUMENT_TYPE "aumu")

    # A copy, not a symlink: AU hosts don't reliably find symlinked components.
    set(STRUCKROOM_AUV2_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    add_custom_target(install-auv2
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKROOM_AUV2_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKROOM_AUV2_DIR}/StruckRoom.component"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:StruckRoom_auv2>"
            "${STRUCKROOM_AUV2_DIR}/StruckRoom.component"
        DEPENDS StruckRoom_auv2
        VERBATIM
        COMMENT "Copying StruckRoom.component into ${STRUCKROOM_AUV2_DIR}")
endif()

# --- self-tests (the framework; headless, no DAW) --------------------------
add_executable(pm-selftest test/pm_selftest.cpp)
target_link_libraries(pm-selftest PRIVATE pm_framework)
target_include_directories(pm-selftest PRIVATE external/clap/include) # clap_ump.h decoders

add_executable(pm-module-test test/pm_module_test.cpp)
target_link_libraries(pm-module-test PRIVATE pm_framework)

add_executable(pm-sinosc-test test/pm_sinosc_test.cpp)
target_link_libraries(pm-sinosc-test PRIVATE pm_framework)

add_executable(pm-ifscore-test test/pm_ifscore_test.cpp)
target_link_libraries(pm-ifscore-test PRIVATE pm_framework)

add_executable(pm-adsr-test test/pm_adsr_test.cpp)
target_link_libraries(pm-adsr-test PRIVATE pm_framework)

add_executable(pm-modules-test test/pm_modules_test.cpp)
target_link_libraries(pm-modules-test PRIVATE pm_framework)

add_executable(pm-factory-test test/pm_factory_test.cpp)
target_link_libraries(pm-factory-test PRIVATE pm_framework)

add_executable(pm-voice-test test/pm_voice_test.cpp)
target_link_libraries(pm-voice-test PRIVATE pm_framework)

add_executable(pm-modroute-test test/pm_modroute_test.cpp)
target_link_libraries(pm-modroute-test PRIVATE pm_framework)

add_executable(pm-voicemanager-test test/pm_voicemanager_test.cpp)
target_link_libraries(pm-voicemanager-test PRIVATE pm_framework)

add_executable(ss-room-test test/ss_room_test.cpp)
target_link_libraries(ss-room-test PRIVATE pm_framework)

add_executable(ss-bend-test test/ss_bend_test.cpp)
target_link_libraries(ss-bend-test PRIVATE pm_framework)
target_include_directories(ss-bend-test PRIVATE external/clap/include)   # clap_ump.h

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    # pm-selftest only pulls in light headers → fully strict (optionally -Werror in CI).
    target_compile_options(pm-selftest PRIVATE -Wall -Wextra)
    if(STRUCKROOM_WERROR)
        target_compile_options(pm-selftest PRIVATE -Werror)
    endif()
    # tests that pull in vendored headers → relaxed (unused params in inline methods).
    foreach(t pm-module-test pm-sinosc-test pm-ifscore-test pm-adsr-test pm-modules-test pm-factory-test
              pm-voice-test pm-voicemanager-test pm-modroute-test ss-bend-test)
        target_compile_options(${t} PRIVATE -Wall -Wno-unknown-pragmas -Wno-unused-parameter)
    endforeach()
endif()

enable_testing()
add_test(NAME pm COMMAND pm-selftest)
set_tests_properties(pm PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-module COMMAND pm-module-test)
set_tests_properties(pm-module PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-sinosc COMMAND pm-sinosc-test)
set_tests_properties(pm-sinosc PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-ifscore COMMAND pm-ifscore-test)
set_tests_properties(pm-ifscore PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-adsr COMMAND pm-adsr-test)
set_tests_properties(pm-adsr PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modules COMMAND pm-modules-test)
set_tests_properties(pm-modules PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-factory COMMAND pm-factory-test)
set_tests_properties(pm-factory PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-voice COMMAND pm-voice-test)
set_tests_properties(pm-voice PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modroute COMMAND pm-modroute-test)
set_tests_properties(pm-modroute PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-voicemanager COMMAND pm-voicemanager-test)
set_tests_properties(pm-voicemanager PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME ss-room COMMAND ss-room-test)
set_tests_properties(ss-room PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")
add_test(NAME ss-bend COMMAND ss-bend-test)
set_tests_properties(ss-bend PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

# The UI's Save / Load file format, without the UI.
add_executable(ss-presetfile-test test/ss_presetfile_test.cpp)
target_include_directories(ss-presetfile-test PRIVATE plugin src/UI)
set_target_properties(ss-presetfile-test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_test(NAME ss-presetfile COMMAND ss-presetfile-test "${CMAKE_SOURCE_DIR}/Presets"
                                    "${CMAKE_BINARY_DIR}/presetfile-test")
set_tests_properties(ss-presetfile PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

# --- end-to-end CLAP host test: note-port dialect negotiation + the MIDI 2.0 7.9-pitch path ------
# Drives the real built .clap over the CLAP C API (no DAW, no GUI): dlopen + clap_entry + factory,
# then a MIDI 2.0 channel-voice-2 NoteOn carrying a 7.9-pitch attribute. This is the only check that
# proves the raw entry point does what dpf_add_plugin could not.
add_executable(ss-clap-host-test test/ss_clap_host_test.cpp)
target_include_directories(ss-clap-host-test PRIVATE
    external/clap/include             # <clap/clap.h>
    plugin)                           # StruckRoomParams.h (parameter ids/enums)
# dlopen/dlsym/dlclose: on glibc before 2.34 these live in libdl, so ask CMake for the right library
# rather than assuming it is part of libc (it is not, on older Linux distributions).
target_link_libraries(ss-clap-host-test PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(ss-clap-host-test StruckRoom)

if(APPLE)
    set(STRUCKROOM_CLAP_UNDER_TEST "$<TARGET_BUNDLE_DIR:StruckRoom>")
else()
    set(STRUCKROOM_CLAP_UNDER_TEST "$<TARGET_FILE:StruckRoom>")
endif()
add_test(NAME ss-clap COMMAND ss-clap-host-test "${STRUCKROOM_CLAP_UNDER_TEST}")
set_tests_properties(ss-clap PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")
