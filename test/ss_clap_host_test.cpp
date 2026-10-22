// test/ss_clap_host_test.cpp — drives the ACTUAL built StruckRoom.clap through the CLAP C API, with
// no DAW and no GUI. This is the end-to-end proof of the thing that matters most here: that a MIDI
// 2.0 NoteOn carrying a 7.9-pitch attribute survives the plugin's note port, reaches the resonator's
// pitch converter, and comes back out at the right (microtonal) frequency.
//
// It is also the only check that exercises note-port dialect negotiation — the reason this plugin
// implements CLAP directly instead of using DPF's wrapper, which drops the event outright and
// advertises only the MIDI/MPE dialects.
//
// The pitch checks install a controlled patch through the params extension (a harmonic String
// material, no damping, no inharmonic stretch, mono, spread 0) instead of trusting the default
// patch, so they measure the pitch path and not the timbre. Each measurement gets a fresh plugin
// instance, because a modal voice with a multi-second decay is not a clean slate.
//
// The pitch assertion is a RATIO between two renderings of the same note that differ only in the
// 7.9 fraction (0 vs 256 = half a semitone), which is the quantity the MIDI 2.0 attribute controls.
// Absolute frequency is reported and sanity-checked loosely, since the resonator's fundamental is
// the engine's business, not this test's.
//
// Usage: ss-clap-host-test /path/to/StruckRoom.clap
// Prints "ALL PASS" on success.

#include <clap/clap.h>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "StruckRoomParams.h"   // parameter ids/enums; header-only, nothing is linked from it

using namespace struckroom;

namespace {

int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

constexpr double kSampleRate = 48000.0;

// --- minimal host -------------------------------------------------------------------------------
void hostNoop(const clap_host_t*) {}
const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }

// --- minimal event list -------------------------------------------------------------------------
struct EventList {
    const clap_event_header_t** events = nullptr;
    uint32_t count = 0;

    static uint32_t size(const clap_input_events_t* list) {
        return static_cast<const EventList*>(list->ctx)->count;
    }
    static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t index) {
        return static_cast<const EventList*>(list->ctx)->events[index];
    }
};

// Collects the NOTE_END events a plugin pushes, with the absolute frame each arrived at.
struct NoteEndSink {
    struct End { clap_event_note_t event; uint64_t frame; };
    std::vector<End> ends;
    uint64_t blockStart = 0;
    static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* header) {
        auto* sink = static_cast<NoteEndSink*>(list->ctx);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_NOTE_END)
            sink->ends.push_back({ *reinterpret_cast<const clap_event_note_t*>(header),
                                   sink->blockStart + header->time });
        return true;
    }
};

// --- pitch estimate -----------------------------------------------------------------------------
// Normalized autocorrelation over the sustained part of the decay. A modal bank is periodic once its
// harmonic modes are ringing, and autocorrelation is far more forgiving of the excitation transient
// and of the mode amplitudes than zero crossings are. The first lag whose correlation is within 5%
// of the best is the fundamental period (its multiples are also peaks, at larger lags). The peak is
// then refined by parabolic interpolation: a whole-sample lag alone resolves C4 only to ~0.5%
// (48000/182 = 263.7 Hz), too coarse to check an interval.
double estimatePitch(const std::vector<float>& samples, double minHz, double maxHz) {
    const size_t start = samples.size() * 2 / 5;      // skip the strike transient
    const size_t count = samples.size() - start;
    if (count < 256) return 0.0;
    const size_t minLag = std::max<size_t>(2, size_t(kSampleRate / maxHz));
    const size_t maxLag = std::min(count - 1, size_t(kSampleRate / minHz));
    if (maxLag <= minLag) return 0.0;

    double best = 0.0;
    std::vector<double> correlation(maxLag + 1, 0.0);
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        double dot = 0.0, energyA = 0.0, energyB = 0.0;
        for (size_t i = 0; i + lag < count; ++i) {
            const double a = samples[start + i];
            const double b = samples[start + i + lag];
            dot += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        correlation[lag] = dot / (std::sqrt(energyA * energyB) + 1e-30);
        best = std::max(best, correlation[lag]);
    }
    if (best <= 0.0) return 0.0;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        if (correlation[lag] < 0.95 * best) continue;
        while (lag < maxLag && correlation[lag + 1] > correlation[lag]) ++lag;   // to the local peak
        double period = double(lag);
        if (lag > minLag && lag < maxLag) {
            const double l = correlation[lag - 1], c = correlation[lag], r = correlation[lag + 1];
            const double denominator = l - 2.0 * c + r;
            if (denominator < 0.0) period += 0.5 * (l - r) / denominator;
        }
        return kSampleRate / period;
    }
    return 0.0;
}

// --- state save/load plumbing -------------------------------------------------------------------
struct StateSink {
    std::string bytes;
    static int64_t write(const clap_ostream_t* stream, const void* buffer, uint64_t size) {
        static_cast<StateSink*>(stream->ctx)->bytes.append(static_cast<const char*>(buffer), size_t(size));
        return int64_t(size);
    }
};

struct StateSource {
    std::string bytes;
    size_t pos = 0;
    static int64_t read(const clap_istream_t* stream, void* buffer, uint64_t size) {
        auto* s = static_cast<StateSource*>(stream->ctx);
        const size_t remaining = s->bytes.size() - s->pos;
        const size_t n = std::min<size_t>(size_t(size), remaining);
        std::memcpy(buffer, s->bytes.data() + s->pos, n);
        s->pos += n;
        return int64_t(n);
    }
};

// Send one parameter value and render a tiny block so the plugin stores/applies it.
// (Defined after render(), below.)

// --- the library + plugin under test ------------------------------------------------------------
struct Library {
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_descriptor_t* descriptor = nullptr;
    clap_host_t host{};
};

bool openLibrary(const std::string& path, Library& lib) {
    std::string binary = path;
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".clap") == 0) {
        const size_t slash = path.find_last_of('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        binary = path + "/Contents/MacOS/" + name.substr(0, name.size() - 5);
    }
    lib.handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib.handle == nullptr) lib.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib.handle == nullptr) return false;

    lib.entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib.handle, "clap_entry"));
    if (lib.entry == nullptr || !lib.entry->init(path.c_str())) return false;

    lib.factory = static_cast<const clap_plugin_factory_t*>(
        lib.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (lib.factory == nullptr) return false;
    lib.descriptor = lib.factory->get_plugin_descriptor(lib.factory, 0);
    if (lib.descriptor == nullptr) return false;

    lib.host = clap_host_t{};
    lib.host.clap_version = CLAP_VERSION_INIT;
    lib.host.name = "ss-clap-host-test";
    lib.host.vendor = "test";
    lib.host.url = "";
    lib.host.version = "1";
    lib.host.get_extension = hostGetExtension;
    lib.host.request_restart = hostNoop;
    lib.host.request_process = hostNoop;
    lib.host.request_callback = hostNoop;
    return true;
}

struct Instance {
    const clap_plugin_t* plugin = nullptr;

    bool create(Library& lib) {
        plugin = lib.factory->create_plugin(lib.factory, &lib.host, lib.descriptor->id);
        return plugin != nullptr && plugin->init(plugin)
            && plugin->activate(plugin, kSampleRate, 1, 4096);
    }
    void destroy() {
        if (plugin != nullptr) { plugin->deactivate(plugin); plugin->destroy(plugin); }
        plugin = nullptr;
    }
};

// Both output channels; StruckRoom's stereo width is a mid/side pass over the summed voice, so at
// spread = 0 both channels carry the same mono signal.
struct StereoAudio {
    std::vector<float> left, right;
};

StereoAudio renderStereo(const clap_plugin_t* plugin, uint32_t frames,
                         const clap_event_header_t** events, uint32_t eventCount,
                         NoteEndSink* noteEnds = nullptr) {
    StereoAudio audio;
    audio.left.assign(frames, 0.0f);
    audio.right.assign(frames, 0.0f);

    EventList holder;
    holder.events = events;
    holder.count = eventCount;
    clap_input_events_t in{};
    in.ctx = &holder;
    in.size = EventList::size;
    in.get = EventList::get;

    float* channels[2] = { audio.left.data(), audio.right.data() };
    clap_audio_buffer_t out{};
    out.data32 = channels;
    out.data64 = nullptr;
    out.channel_count = 2;
    out.latency = 0;
    out.constant_mask = 0;

    clap_process_t process{};
    process.steady_time = -1;
    process.frames_count = frames;
    process.transport = nullptr;
    process.audio_inputs = nullptr;
    process.audio_inputs_count = 0;
    process.audio_outputs = &out;
    process.audio_outputs_count = 1;
    process.in_events = &in;
    clap_output_events_t outEvents{};
    outEvents.ctx = noteEnds;
    outEvents.try_push = NoteEndSink::tryPush;
    process.out_events = noteEnds != nullptr ? &outEvents : nullptr;

    plugin->process(plugin, &process);
    return audio;
}

std::vector<float> render(const clap_plugin_t* plugin, uint32_t frames,
                         const clap_event_header_t** events, uint32_t eventCount) {
    return renderStereo(plugin, frames, events, eventCount).left;
}

// Send one parameter value and render a tiny block so the plugin stores/applies it.
void sendParam(const clap_plugin_t* plugin, uint32_t index, double value) {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = index;
    event.value = value;
    const clap_event_header_t* headers[] = { &event.header };
    render(plugin, 64, headers, 1);
}

double readParam(const clap_plugin_t* plugin, uint32_t index) {
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (params == nullptr || params->get_value == nullptr) return -1.0;
    double value = 0.0;
    return params->get_value(plugin, index, &value) ? value : -1.0;
}

float peakOf(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::fabs(s));
    return peak;
}

// Install a pitch-friendly patch: a harmonic String material, no inharmonic stretch, no damping,
// long decay, mono spread, full level. Everything that could colour or confuse a pitch measurement
// is pinned down.
void installTestPatch(const clap_plugin_t* plugin) {
    struct Holder { clap_event_param_value_t event{}; };
    std::vector<Holder> storage;

    auto add = [&storage](uint32_t index, double value) {
        Holder holder{};
        holder.event.header.size = sizeof(clap_event_param_value_t);
        holder.event.header.time = 0;
        holder.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        holder.event.header.type = CLAP_EVENT_PARAM_VALUE;
        holder.event.param_id = index;   // the plugin's clap_id is the 0-based parameter index
        holder.event.value = value;
        storage.push_back(holder);
    };

    add(kParamVoiceMode, 1.0);          // poly: pitch slew is forced instant, no glide scoop
    add(kParamGlide, 0.0001);
    add(kParamLevel, 1.0);
    add(kParamSpread, 0.0);             // mono: one unambiguous fundamental
    add(kParamStrikeType, 0.0);         // mallet
    add(kParamHardness, 0.0);           // bright impulse: excites the fundamental strongly
    add(kParamVelToLevel, 1.0);
    add(kParamVelToHardness, 0.0);
    add(kParamContactMode, 0.0);
    add(kParamPitch, 0.0);
    add(kParamMaterial, 0.0);           // "String": the harmonic series
    add(kParamModeCount, 64.0);
    add(kParamStretch, 9.0);            // kModalStretchValues[9] == 0.0 (perfectly harmonic)
    add(kParamDecayT60, 8.0);           // ring long enough to measure
    add(kParamDampTilt, 0.0);
    add(kParamDampBellAmount, 0.0);
    add(kParamDampBellCentre, 2.0);
    add(kParamDampBellWidth, 0.4);
    add(kParamStrikePosition, 0.05);    // near the end: every mode, fundamental included, is excited
    add(kParamBrightness, 0.0);

    std::vector<const clap_event_header_t*> headers;
    headers.reserve(storage.size());
    for (Holder& holder : storage) headers.push_back(&holder.event.header);
    render(plugin, 256, headers.data(), uint32_t(headers.size()));
}

// A MIDI 2.0 channel-voice-2 NoteOn UMP with a 7.9-pitch attribute (attributeType 3).
clap_event_midi2_t makeUmpNoteOn(uint8_t note, uint16_t velocity, uint8_t attributeType,
                                 uint16_t attribute) {
    const uint32_t words[2] = {
        (0x4u << 28) | (0x9u << 20) | (uint32_t(note) << 8) | uint32_t(attributeType),
        (uint32_t(velocity) << 16) | uint32_t(attribute),
    };
    clap_event_midi2_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI2;
    event.port_index = 0;
    std::memcpy(event.data, words, sizeof(words));
    return event;
}

clap_event_note_t makeClapNoteOn(int16_t key, double velocity) {
    clap_event_note_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_NOTE_ON;
    event.port_index = 0;
    event.key = key;
    event.channel = 0;
    event.velocity = velocity;
    return event;
}

clap_event_note_expression_t makeNoteExpression(clap_note_expression id, int32_t noteId, int16_t key,
                                                 double value) {
    clap_event_note_expression_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    event.expression_id = id;
    event.note_id = noteId;
    event.port_index = 0;
    event.channel = 0;
    event.key = key;
    event.value = value;
    return event;
}

clap_event_midi_t makeMidi1(uint8_t status, uint8_t data1, uint8_t data2) {
    clap_event_midi_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.port_index = 0;
    event.data[0] = status; event.data[1] = data1; event.data[2] = data2;
    return event;
}

// Render events (a note, plus anything that shapes it) through a fresh instance with the test patch,
// and report the measured pitch.
double pitchOfEvents(Library& lib, const clap_event_header_t** events, uint32_t eventCount,
                     double expectedHz, const char* what) {
    Instance inst;
    if (!inst.create(lib)) {
        check(false, std::string(what) + ": plugin instance created");
        return 0.0;
    }
    installTestPatch(inst.plugin);

    const std::vector<float> audio = render(inst.plugin, uint32_t(kSampleRate), events, eventCount);

    bool finite = true;
    for (float s : audio) if (!std::isfinite(s)) finite = false;
    const float peak = peakOf(audio);
    const double pitch = estimatePitch(audio, expectedHz * 0.5, expectedHz * 2.0);
    std::printf("        %-28s expected ~%8.3f Hz | measured %8.3f Hz | peak %.4f\n",
                what, expectedHz, pitch, double(peak));

    check(finite, std::string(what) + ": output is finite");
    check(peak > 0.01f, std::string(what) + ": produces audio");
    inst.destroy();
    return pitch;
}

double pitchOfNote(Library& lib, const clap_event_header_t* noteEvent, double expectedHz,
                   const char* what) {
    const clap_event_header_t* events[] = { noteEvent };
    return pitchOfEvents(lib, events, 1, expectedHz, what);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s /path/to/StruckRoom.clap\n", argv[0]);
        return 2;
    }

    Library lib;
    check(openLibrary(argv[1], lib), "dlopen + clap_entry + factory + descriptor");
    if (lib.handle == nullptr) { std::printf("FAILURES: %d\n", gFailures); return 1; }

    std::printf("descriptor: id=%s name=%s vendor=%s\n",
                lib.descriptor->id, lib.descriptor->name, lib.descriptor->vendor);
    check(std::strcmp(lib.descriptor->id, "com.unlikelyware.struckroom") == 0, "descriptor id");
    check(std::strcmp(lib.descriptor->name, "StruckRoom") == 0, "descriptor name");

    // --- audio ports -----------------------------------------------------------------------------
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created");
        const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_AUDIO_PORTS));
        check(ports != nullptr, "audio-ports extension present");
        if (ports != nullptr) {
            check(ports->count(inst.plugin, true) == 0, "no audio inputs");
            check(ports->count(inst.plugin, false) == 1, "one audio output");
            clap_audio_port_info_t info{};
            if (ports->get(inst.plugin, 0, false, &info))
                check(info.channel_count == 2, "output is stereo");
            else
                check(false, "audio port info readable");
        }
        inst.destroy();
    }

    // --- the note port must offer (and prefer) the MIDI 2.0 dialect ------------------------------
    // This is the negotiation that makes a MIDI-2.0-capable host send raw UMP notes at all; without
    // it CLAP_EVENT_MIDI2 never arrives and the 7.9-pitch path is dead code.
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (note ports)");
        const auto* notes = static_cast<const clap_plugin_note_ports_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_NOTE_PORTS));
        check(notes != nullptr, "note-ports extension present");
        if (notes != nullptr) {
            check(notes->count(inst.plugin, true) == 1, "one note input port");
            clap_note_port_info_t info{};
            if (notes->get(inst.plugin, 0, true, &info)) {
                check((info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0,
                      "advertises the CLAP note dialect");
                check((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0,
                      "advertises the MIDI 1.0 dialect");
                check((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI2) != 0,
                      "advertises the MIDI 2.0 dialect");
                check(info.preferred_dialect == CLAP_NOTE_DIALECT_MIDI2,
                      "prefers the MIDI 2.0 dialect");
            } else {
                check(false, "note port info readable");
            }
        }
        inst.destroy();
    }

    // --- the gui extension is wired (DPF is now the GUI layer only) ------------------------------
    // Creating a GUI here does not build the UIExporter (that happens in set_parent, which needs a
    // host window), so this is safe headless: it proves the extension exists and its API negotiation
    // works, which is what a host does before it ever hands us a parent window.
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (gui)");
        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_GUI));
        check(gui != nullptr, "gui extension present");
        if (gui != nullptr) {
            const char* api = nullptr;
            bool floating = true;
            check(gui->get_preferred_api(inst.plugin, &api, &floating) && api != nullptr && !floating,
                  "gui prefers an embedded (non-floating) window API");
            check(gui->is_api_supported(inst.plugin, api, false), "preferred gui API is supported");
            // get_size is only valid after create(), which here only allocates the holder — the
            // UIExporter itself waits for set_parent().
            if (gui->create(inst.plugin, api, false)) {
                uint32_t w = 0, h = 0;
                check(gui->get_size(inst.plugin, &w, &h) && w > 0 && h > 0,
                      "gui reports a default size");
                gui->destroy(inst.plugin);
            } else {
                check(false, "gui create() succeeds for the preferred API");
            }
        }
        check(inst.plugin->get_extension(inst.plugin, CLAP_EXT_TIMER_SUPPORT) != nullptr,
              "timer-support extension present (pumps the UI event loop)");
        inst.destroy();
    }

    // --- params extension ------------------------------------------------------------------------
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (params)");
        const auto* params = static_cast<const clap_plugin_params_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_PARAMS));
        check(params != nullptr, "params extension present");
        if (params != nullptr) {
            check(params->count(inst.plugin) == uint32_t(kParamCount), "parameter count matches");
            clap_param_info_t info{};
            if (params->get_info(inst.plugin, kParamMaterial, &info)) {
                check(info.id == kParamMaterial, "param id is the 0-based index (DPF-compatible)");
                check((info.flags & CLAP_PARAM_IS_ENUM) != 0, "Material is flagged as an enum");
                char text[128] = {};
                if (params->value_to_text(inst.plugin, kParamMaterial, 0.0, text, sizeof(text)))
                    check(std::strcmp(text, "String") == 0, "enum value_to_text returns the label");
                else
                    check(false, "value_to_text works");
            } else {
                check(false, "param info readable");
            }
            if (params->get_info(inst.plugin, kParamDecayT60, &info))
                check((info.flags & CLAP_PARAM_IS_ENUM) == 0, "Decay is not an enum");
        }
        inst.destroy();
    }

    // --- state save/load, including the legacy DPF-blob fallback ---------------------------------
    // The raw entry point implements CLAP state itself, so this is real coverage: a round trip must
    // restore values, and the NUL-separated blob DPF's generated wrapper used to write must still
    // load (that is what keeps sessions saved before this switch working).
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (state)");
        const auto* state = static_cast<const clap_plugin_state_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_STATE));
        check(state != nullptr && state->save != nullptr && state->load != nullptr,
              "state extension present");

        if (state != nullptr) {
            sendParam(inst.plugin, kParamDecayT60, 5.0);
            check(std::fabs(readParam(inst.plugin, kParamDecayT60) - 5.0) < 1e-6,
                  "parameter set before saving");

            StateSink sink;
            clap_ostream_t out{};
            out.ctx = &sink;
            out.write = StateSink::write;
            check(state->save(inst.plugin, &out), "state save succeeds");
            check(!sink.bytes.empty(), "state save produced bytes");

            sendParam(inst.plugin, kParamDecayT60, 1.0);   // clobber it
            check(std::fabs(readParam(inst.plugin, kParamDecayT60) - 1.0) < 1e-6,
                  "parameter changed after saving");

            StateSource source;
            source.bytes = sink.bytes;
            clap_istream_t in{};
            in.ctx = &source;
            in.read = StateSource::read;
            check(state->load(inst.plugin, &in), "state load succeeds");
            check(std::fabs(readParam(inst.plugin, kParamDecayT60) - 5.0) < 1e-6,
                  "state round trip restores the value");

            // DPF's old auto-state: NUL-separated "__dpf_parameters_begin__" pairs, then \xfe.
            std::string legacy = "__dpf_parameters_begin__";
            legacy.push_back('\0');
            legacy += "decay";            legacy.push_back('\0');
            legacy += "3";                legacy.push_back('\0');
            legacy += "__dpf_parameters_end__";
            legacy.push_back('\0');
            legacy.push_back('\xfe');
            StateSource legacySource;
            legacySource.bytes = legacy;
            clap_istream_t legacyIn{};
            legacyIn.ctx = &legacySource;
            legacyIn.read = StateSource::read;
            check(state->load(inst.plugin, &legacyIn), "legacy DPF state blob loads");
            check(std::fabs(readParam(inst.plugin, kParamDecayT60) - 3.0) < 1e-6,
                  "legacy DPF state blob restores the value");

            // Data that is neither format must be rejected, not silently defaulted.
            StateSource junk;
            junk.bytes = "not our state at all";
            clap_istream_t junkIn{};
            junkIn.ctx = &junk;
            junkIn.read = StateSource::read;
            check(!state->load(inst.plugin, &junkIn), "foreign state data is rejected");
        }
        inst.destroy();
    }

    // --- MIDI 2.0 7.9-pitch, end to end through the real plugin -----------------------------------
    // Note 60 (C4) with fraction 0 and with fraction 256 = half a semitone. Both go through the same
    // patch; only the UMP attribute differs, so the RATIO of the measured fundamentals is a direct
    // measurement of the MIDI 2.0 path.
    std::printf("MIDI 2.0 7.9-pitch notes through the real plugin:\n");
    const double kC4Hz = 261.6255653;
    const uint16_t kNote = 60;
    const uint16_t attributeZero = uint16_t((kNote << 9) | 0);
    const uint16_t attributeQuarterTone = uint16_t((kNote << 9) | 256);   // 256/512 semitone

    const clap_event_midi2_t noteOnZero = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0x3, attributeZero);
    const clap_event_midi2_t noteOnQuarter = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0x3, attributeQuarterTone);

    const double hzZero = pitchOfNote(lib, &noteOnZero.header, kC4Hz, "7.9 fraction 0 (C4)");
    const double hzQuarter = pitchOfNote(lib, &noteOnQuarter.header,
                                         kC4Hz * std::pow(2.0, 0.5 / 12.0),
                                         "7.9 fraction 256 (+50c)");

    if (hzZero > 0.0 && hzQuarter > 0.0) {
        const double ratio = hzQuarter / hzZero;
        const double expectedRatio = std::pow(2.0, 0.5 / 12.0);   // 1.029302
        const double errorPercent = 100.0 * std::fabs(ratio - expectedRatio) / expectedRatio;
        std::printf("        ratio %.6f (expected %.6f, error %.3f%%)\n",
                    ratio, expectedRatio, errorPercent);
        check(errorPercent < 1.0,
              "the 7.9 fraction moves the measured pitch by half a semitone");
        // Absolute sanity only: the fundamental should be near C4, not an octave off.
        check(std::fabs(hzZero - kC4Hz) / kC4Hz < 0.10, "7.9 fraction 0 sounds near C4 in absolute terms");
    } else {
        check(false, "both 7.9-pitch notes produced a measurable pitch");
    }

    // --- pitch bend through the real plugin -------------------------------------------------------
    // Proves the bend CV reaches ModalBank's second pitch input at full depth, not just in the engine.
    std::printf("Pitch bend through the real plugin:\n");
    {
        // No RC 7: the per-note range is the channel's (±2), so half way up is +1 semitone.
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        clap_event_midi2_t bend = noteOn;
        bend.data[0] = (0x4u << 28) | (0x6u << 20) | (uint32_t(kNote) << 8);                // per-note bend
        bend.data[1] = 0xC0000000u;                                                        // half way up
        const clap_event_header_t* events[] = { &noteOn.header, &bend.header };
        const double expected = kC4Hz * std::pow(2.0, 1.0 / 12.0);
        const double hz = pitchOfEvents(lib, events, 2, expected, "per-note bend, default range");
        // Against the measured unbent C4, as for the 7.9 check: an interval is what a bend controls.
        check(hzZero > 0.0 && std::fabs(hz / hzZero - std::pow(2.0, 1.0 / 12.0)) < 0.002,
              "with no RC 7, a half-way per-note bend moves C4 up 1 semitone (±2)");
    }
    {
        // Announce a ±6 per-note range (registered controller 7, as KeyboardEDO's setup does), then
        // bend half way up: +3 semitones, distinguishable from the ±2 default.
        clap_event_midi2_t range = makeUmpNoteOn(0, 0, 0, 0);
        range.data[0] = (0x4u << 28) | (0x2u << 20) | (0u << 8) | 7u;
        range.data[1] = 6u << 25;
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        clap_event_midi2_t bend = noteOn;
        bend.data[0] = (0x4u << 28) | (0x6u << 20) | (uint32_t(kNote) << 8);
        bend.data[1] = 0xC0000000u;                                                        // half way up
        const clap_event_header_t* events[] = { &range.header, &noteOn.header, &bend.header };
        const double expected = kC4Hz * std::pow(2.0, 3.0 / 12.0);
        const double hz = pitchOfEvents(lib, events, 3, expected, "per-note bend at RC7 = 6");
        check(hzZero > 0.0 && std::fabs(hz / hzZero - std::pow(2.0, 3.0 / 12.0)) < 0.002,
              "registered controller 7 sets the per-note range: half way at ±6 is +3 semitones");
    }
    {
        const clap_event_midi_t bend = makeMidi1(0xE0, 0x00, 0x60);   // half way up: +1 of ±2
        const clap_event_midi_t noteOn = makeMidi1(0x90, uint8_t(kNote), 100);
        const clap_event_header_t* events[] = { &bend.header, &noteOn.header };
        const double expected = kC4Hz * std::pow(2.0, 1.0 / 12.0);
        const double hz = pitchOfEvents(lib, events, 2, expected, "MIDI 1.0 channel bend +1");
        check(hzZero > 0.0 && std::fabs(hz / hzZero - std::pow(2.0, 1.0 / 12.0)) < 0.002,
              "a MIDI 1.0 channel bend of +1 semitone moves C4 to C#4");
    }

    // --- CLAP note expressions + note ids, as Bitwig sends them ---------------------------------------
    // Tuning arrives as Pitch 7.25 (an absolute pitch), so it is exact far beyond the ±2 bend range.
    std::printf("CLAP note expressions (Bitwig):\n");
    {
        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 7;
        const clap_event_note_expression_t tuning =
            makeNoteExpression(CLAP_NOTE_EXPRESSION_TUNING, 7, int16_t(kNote), 24.0);
        const clap_event_header_t* events[] = { &noteOn.header, &tuning.header };
        const double expected = kC4Hz * 4.0;
        const double hz = pitchOfEvents(lib, events, 2, expected, "tuning +24 (key + id)");
        check(hzZero > 0.0 && std::fabs(hz / hzZero - 4.0) < 0.002 * 4.0,
              "a +24 semitone tuning expression sounds two octaves up");
    }
    {
        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 11;
        const clap_event_note_expression_t tuning =
            makeNoteExpression(CLAP_NOTE_EXPRESSION_TUNING, 11, -1, 7.0);   // id only, no key
        const clap_event_header_t* events[] = { &noteOn.header, &tuning.header };
        const double expected = kC4Hz * std::pow(2.0, 7.0 / 12.0);
        const double hz = pitchOfEvents(lib, events, 2, expected, "tuning +7 (id only)");
        check(hzZero > 0.0 && std::fabs(hz / hzZero - std::pow(2.0, 7.0 / 12.0)) < 0.002,
              "an expression naming only the note id reaches that note (+7 semitones)");
    }

    // --- MPE switch through the real plugin ---------------------------------------------------------
    // The same member-channel bend (+3 at ±48) with the switch off and on: the parameter must reach the
    // engine, and it must be a two-state Off/On enum.
    std::printf("MPE switch:\n");
    {
        Instance probe;
        if (probe.create(lib)) {
            const auto* params = static_cast<const clap_plugin_params_t*>(
                probe.plugin->get_extension(probe.plugin, CLAP_EXT_PARAMS));
            clap_param_info_t info{};
            char text[64] = {};
            check(params != nullptr && params->get_info(probe.plugin, kParamMpe, &info)
                      && (info.flags & CLAP_PARAM_IS_ENUM) != 0 && info.default_value == 0.0
                      && params->value_to_text(probe.plugin, kParamMpe, 1.0, text, sizeof(text))
                      && std::string(text) == "On",
                  "MPE is an Off/On enum parameter, off by default");
            probe.destroy();
        }
        auto memberBend = [&](bool mpe) {
            Instance inst;
            if (!inst.create(lib)) return 0.0;
            installTestPatch(inst.plugin);
            sendParam(inst.plugin, kParamMpe, mpe ? 1.0 : 0.0);
            const clap_event_midi_t bend = makeMidi1(0xE1, 0x00, 0x44);        // 8192 + 512, channel 2
            const clap_event_midi_t noteOn = makeMidi1(0x91, uint8_t(kNote), 100);
            const clap_event_header_t* events[] = { &bend.header, &noteOn.header };
            const std::vector<float> audio = render(inst.plugin, uint32_t(kSampleRate), events, 2);
            inst.destroy();
            return 12.0 * std::log2(estimatePitch(audio, 100.0, 2000.0) / hzZero);
        };
        const double off = memberBend(false), on = memberBend(true);
        std::printf("        member bend: %.3f st with MPE off, %.3f st with MPE on\n", off, on);
        check(std::fabs(off - 0.125) < 0.02 && std::fabs(on - 3.0) < 0.02,
              "MPE on reads a member-channel bend at ±48 (+3 st), off at ±2 (+0.125 st)");
    }

    // --- velocity: MIDI 1.0 127 is full scale (the spec's Min-Center-Max upscaling) ----------------
    std::printf("Velocity scaling:\n");
    {
        auto peakFor = [&](const clap_event_header_t* note) {
            Instance inst;
            if (!inst.create(lib)) return -1.0f;
            installTestPatch(inst.plugin);
            sendParam(inst.plugin, kParamLevel, 0.1);   // stay clear of the output clip
            const clap_event_header_t* events[] = { note };
            const float peak = peakOf(render(inst.plugin, 4096, events, 1));
            inst.destroy();
            return peak;
        };
        const clap_event_midi_t midi1 = makeMidi1(0x90, uint8_t(kNote), 127);
        const clap_event_note_t clapNote = makeClapNoteOn(int16_t(kNote), 1.0);
        const float a = peakFor(&midi1.header), b = peakFor(&clapNote.header);
        std::printf("        peak: MIDI 1.0 vel 127 %.6f | CLAP vel 1.0 %.6f\n", double(a), double(b));
        check(a > 0.001f && a == b, "MIDI 1.0 velocity 127 strikes exactly as a CLAP note at velocity 1.0");
    }

    // --- Expression: the three knobs exist, default to 0, and CLAP Pressure reaches Press>Decay ------
    std::printf("Expression:\n");
    {
        Instance probe;
        if (probe.create(lib)) {
            const auto* params = static_cast<const clap_plugin_params_t*>(
                probe.plugin->get_extension(probe.plugin, CLAP_EXT_PARAMS));
            bool allZero = params != nullptr;
            for (const uint32_t index : { uint32_t(kParamPressToDecay), uint32_t(kParamTimbreToTilt), uint32_t(kParamTimbreToPos) }) {
                clap_param_info_t info{};
                allZero = allZero && params->get_info(probe.plugin, index, &info) && info.default_value == 0.0;
            }
            check(allZero, "Press>Decay, Tmb>Tilt and Tmb>Pos exist and default to 0 (no effect)");
            probe.destroy();
        }
        // The tail's level a second after the strike, with full CLAP pressure on the note.
        auto tailLevel = [&](double pressToDecay) {
            Instance inst;
            if (!inst.create(lib)) return 0.0f;
            installTestPatch(inst.plugin);
            sendParam(inst.plugin, kParamDecayT60, 0.3);
            sendParam(inst.plugin, kParamPressToDecay, pressToDecay);
            clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
            noteOn.note_id = 5;
            const clap_event_note_expression_t pressure =
                makeNoteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 5, int16_t(kNote), 1.0);
            const clap_event_header_t* events[] = { &noteOn.header, &pressure.header };
            render(inst.plugin, uint32_t(kSampleRate), events, 2);
            const float level = peakOf(render(inst.plugin, 4800, nullptr, 0));
            inst.destroy();
            return level;
        };
        const float off = tailLevel(0.0), on = tailLevel(3.0);
        std::printf("        tail peak after 1 s: Press>Decay 0 %.6f | +3 oct %.6f\n", double(off), double(on));
        check(on > 100.0f * off, "a CLAP Pressure expression with Press>Decay +3 keeps the note ringing");
    }

    // --- note end + voice-info ---------------------------------------------------------------------
    // A struck note rings on after note-off; CLAP_EVENT_NOTE_END must come when it falls silent, not
    // at the note-off, and only for notes that arrived as CLAP note events.
    std::printf("Note end and voice info:\n");
    {
        Instance inst;
        check(inst.create(lib), "fresh instance");
        installTestPatch(inst.plugin);
        sendParam(inst.plugin, kParamDecayT60, 0.3);

        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 42;
        clap_event_note_t noteOff = noteOn;
        noteOff.header.type = CLAP_EVENT_NOTE_OFF;
        noteOff.header.time = 64;
        const clap_event_midi_t midiOn = makeMidi1(0x90, 72, 100);
        const clap_event_midi_t midiOff = [] { auto e = makeMidi1(0x80, 72, 0); e.header.time = 64; return e; }();
        const clap_event_header_t* first[] = { &noteOn.header, &midiOn.header, &noteOff.header, &midiOff.header };

        NoteEndSink sink;
        const uint32_t block = 512;
        for (uint32_t b = 0; b < uint32_t(4.0 * kSampleRate) / block; ++b) {
            sink.blockStart = uint64_t(b) * block;
            if (b == 0) renderStereo(inst.plugin, block, first, 4, &sink);
            else        renderStereo(inst.plugin, block, nullptr, 0, &sink);
        }
        check(sink.ends.size() == 1, "exactly one NOTE_END, for the CLAP note only (got "
                                         + std::to_string(sink.ends.size()) + ")");
        if (!sink.ends.empty()) {
            const auto& end = sink.ends[0];
            std::printf("        NOTE_END at %.3f s (note-off at %.4f s)\n",
                        double(end.frame) / kSampleRate, 64.0 / kSampleRate);
            check(end.event.note_id == 42 && end.event.key == int16_t(kNote) && end.event.channel == 0
                      && end.event.port_index == 0,
                  "NOTE_END echoes the note-on's id, key, channel and port");
            check(double(end.frame) / kSampleRate > 0.2, "NOTE_END waits for the ring-out, not the note-off");
        }

        const auto* voiceInfo = static_cast<const clap_plugin_voice_info_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_VOICE_INFO));
        check(voiceInfo != nullptr, "voice-info extension is offered");
        if (voiceInfo != nullptr) {
            clap_voice_info_t info{};
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 4 && info.voice_capacity == 32
                      && (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) != 0,
                  "poly: 4 voices by default, overlapping notes supported");
            sendParam(inst.plugin, kParamVoiceMode, 2.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 10 && info.voice_capacity == 32,
                  "Poly 10: 10 voices");
            sendParam(inst.plugin, kParamVoiceMode, 3.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 20 && info.voice_capacity == 32,
                  "Poly 20: 20 voices");
            sendParam(inst.plugin, kParamVoiceMode, 4.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 32 && info.voice_capacity == 32,
                  "Poly 32: 32 voices");
            sendParam(inst.plugin, kParamVoiceMode, 0.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 1 && info.voice_capacity == 32,
                  "mono: 1 voice");
        }
        inst.destroy();
    }

    // --- the MIDI-1.0 / CLAP dialects still work -------------------------------------------------
    std::printf("Other note dialects (must keep working):\n");
    {
        const clap_event_note_t clapNote = makeClapNoteOn(int16_t(kNote), 1.0);
        const double hz = pitchOfNote(lib, &clapNote.header, kC4Hz, "CLAP note-on (C4)");
        check(hz > 0.0, "CLAP note dialect reaches the engine");
    }
    {
        const clap_event_midi_t midi1 = makeMidi1(0x90, uint8_t(kNote), 100);
        const double hz = pitchOfNote(lib, &midi1.header, kC4Hz, "MIDI 1.0 note-on (C4)");
        check(hz > 0.0, "MIDI 1.0 dialect reaches the engine");
    }

    if (lib.handle != nullptr) {
        lib.entry->deinit();
        dlclose(lib.handle);
    }

    if (gFailures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("FAILURES: %d\n", gFailures);
    return 1;
}
