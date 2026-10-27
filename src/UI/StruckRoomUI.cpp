// StruckRoomUI.cpp — the front end, in three tabs chosen in the header:
//   Object: the struck object, as StruckSynth had it. Six grouped panels (Global, Strike, Object,
//           Decay, Timbre, Expression) of reusable Knob/Dropdown controls, plus a live ModalSpectrum
//           that drives the real framework-free ModalBank core to draw the mode layout and decay.
//   Room:   a RoomMap (top-down, drag the object and the listener) beside the Room, Placement and
//           Mix panels.
//   Walls:  the two wall pairs side by side, over a RoomDecay view of every room mode's T60.
// Every tab's widgets exist all the time; switching tabs only shows and hides them.

// Before DPF's headers: CoreFoundation's MacTypes.h declares a global Point, ambiguous with DGL's
// once DPF's using-declarations are in scope.
#ifndef _WIN32
#include <pwd.h>                             // presetDir: the account's home folder
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>   // the idle timer when the host has none (startIdleTimer)
#endif

#include "DistrhoUI.hpp"

#include "src/DistrhoUIInternal.hpp" // DPF's UIExporter (DPF_ROOT_DIR/distrho is on the include path)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "DistrhoPluginInfo.h"
#include "Dropdown.hpp"
#include "EmbeddedResources.hpp" // build-time codegen: logo PNG and help page bytes
#include "GeneratedPresets.h"     // build-time codegen from Presets/*.json (see tools/gen_presets.py)
#include "GroupBox.hpp"
#include "HeaderButton.hpp"
#include "HelpButton.hpp"
#include "Knob.hpp"
#include "Logo.hpp"
#include "PopupMenu.hpp"
#include "RoomMap.hpp"
#include "TabBar.hpp"
#include "PresetKeys.hpp"         // param -> preset-JSON key map, shared with the dumper
#include "PresetDump.hpp"         // dev-only JSON patch dumper (no-op unless STRUCKROOM_PRESET_DUMP=1)
#include "PresetFile.hpp"         // Save / Load: the patch as preset JSON

#include "ModalBank.hpp"        // framework-free core, for the spectrum visual
#include "ModalGenerators.hpp"  // procedural mode series
#include "ModalMaterials.hpp"   // static material tables + kModalStretchValues
#include "CPPTypes.hpp"         // DEFAULT_SAMPLE_RATE (the UIExporter's nominal rate below)
#include "pm_params.h"          // ModalBankMaterial / MODAL_BANK_FIRST_GENERATOR
#include "StruckRoomParams.h"
#include "plugin_gui.h"         // the C seam to src/plugin.cpp (the raw CLAP plugin)

START_NAMESPACE_DISTRHO

using namespace struckroom;  // params (kParams, ParamIndex, kMaterialNames)

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Dropdown;
using DGL_NAMESPACE::GroupBox;
using DGL_NAMESPACE::HeaderButton;
using DGL_NAMESPACE::HelpButton;
using DGL_NAMESPACE::Knob;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::PopupMenu;
using DGL_NAMESPACE::RoomMap;
using DGL_NAMESPACE::TabBar;

// Category accent palette (struck-instrument scheme): global=steel, strike=amber, object=green,
// decay=violet, output=copper. Reused for panels, knobs, and the spectrum.
static inline Color colGlobal() { return Color(150, 166, 182); }
static inline Color colStrike() { return Color(214, 168, 92);  }
static inline Color colObject() { return Color(120, 190, 140); }
static inline Color colDecay()  { return Color(168, 140, 214); }
static inline Color colOutput() { return Color(214, 132, 96);  }
static inline Color colExpression() { return Color(96, 186, 196); }
// The room tabs: room=blue, placement=sand, mix=rose, and one colour per wall pair.
static inline Color colRoom()      { return Color(110, 170, 220); }
static inline Color colPlacement() { return Color(222, 200, 128); }
static inline Color colMix()       { return Color(204, 146, 176); }
static inline Color colSideWalls() { return Color(226, 120, 110); }
static inline Color colFBWalls()   { return Color(120, 200, 170); }

// --------------------------------------------------------------------------------------------
// ModalSpectrum — draws the current bank as vertical bars: x = mode frequency (log), height =
// relative decay time (T60), so damp-tilt and the damp-bell visibly reshape the profile. It owns
// a ModalBank core and reconfigures it from the modal params on every relevant change.
class ModalSpectrum : public NanoSubWidget
{
public:
    explicit ModalSpectrum(Widget* parent) : NanoSubWidget(parent)
    {
        fBank.setSampleRate(48000.0);
        recompute();
    }

    // Push the modal params the visual depends on, then recompute the mode layout.
    void update(int material, int modes, int stretchIdx, float pitchOct,
                float decayT60, float dampTilt, float brightness,
                float bellAmt, float bellCtr, float bellWid)
    {
        fMaterial = material; fModes = modes; fStretchIdx = stretchIdx; fPitch = pitchOct;
        fDecayT60 = decayT60; fDampTilt = dampTilt; fBrightness = brightness;
        fBellAmt = bellAmt; fBellCtr = bellCtr; fBellWid = bellWid;
        recompute();
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();

        // Panel background + border.
        beginPath(); roundedRect(0, 0, w, h, 6); fillColor(Color(46, 40, 22)); fill();
        beginPath(); roundedRect(0.5f, 0.5f, w - 1, h - 1, 6);
        strokeColor(Color(80, 70, 42)); strokeWidth(1.0f); stroke();

        const float padX = 12, padTop = 10, padBot = 16;
        const float x0 = padX, x1 = w - padX, y1 = h - padBot;
        const float plotH = h - padTop - padBot;
        if (fCount <= 0 || plotH <= 4) return;

        // Log-frequency axis spanning the mode range (with a little headroom).
        const float fLo = std::log2(std::max(20.0f, fMinFreq * 0.9f));
        const float fHi = std::log2(std::max(fLo + 1.0f, fMaxFreq * 1.1f));

        // Baseline.
        beginPath(); moveTo(x0, y1 + 3); lineTo(x1, y1 + 3);
        strokeColor(Color(70, 74, 82)); strokeWidth(1.0f); stroke();

        for (int i = 0; i < fCount; ++i)
        {
            const float fx  = (std::log2(fFreq[i]) - fLo) / (fHi - fLo);
            const float px  = x0 + fx * (x1 - x0);
            const float mag = std::clamp(fT60[i] / fT60Max, 0.02f, 1.0f);
            const float bh  = mag * plotH;

            // Longer-ringing modes draw more opaque; the cap dot keeps short modes visible.
            Color bar = fAccent; bar.alpha = 0.35f + 0.55f * mag;
            beginPath(); rect(px - 1.5f, y1 - bh, 3.0f, bh);
            fillColor(bar); fill();
            beginPath(); circle(px, y1 - bh, 2.2f); fillColor(Color(240, 244, 248)); fill();
        }
    }

private:
    void recompute()
    {
        // Fundamental for the display only: a nominal C3 transposed by the Pitch offset.
        const float f0 = 130.81f * std::exp2(fPitch);
        fBank.setFundamental(f0);
        fBank.setModeCount(fModes);
        fBank.setStretch(kModalStretchValues[std::clamp(fStretchIdx, 0, kModalStretchCount - 1)]);
        fBank.setDecayT60(fDecayT60);
        fBank.setDampTilt(fDampTilt);
        fBank.setBrightness(fBrightness);
        fBank.setDampBellAmount(fBellAmt);
        fBank.setDampBellCentre(fBellCtr);
        fBank.setDampBellWidth(fBellWid);
        selectMaterial(fMaterial);
        fBank.prepare();

        fCount = std::min(fBank.activeModes(), (int)ModalBank::kMaxModes);
        fMinFreq = 1e9f; fMaxFreq = 1.0f; fT60Max = 1e-4f;
        for (int i = 0; i < fCount; ++i)
        {
            fFreq[i] = fBank.modeFrequency(i);
            fT60[i]  = fBank.modeT60(i);
            fMinFreq = std::min(fMinFreq, fFreq[i]);
            fMaxFreq = std::max(fMaxFreq, fFreq[i]);
            fT60Max  = std::max(fT60Max, fT60[i]);
        }
        if (fCount <= 0) { fMinFreq = 100.0f; fMaxFreq = 1000.0f; }
    }

    // Mirror of ModalBankModule's selectMaterial: static tables vs procedural generators.
    void selectMaterial(int idx)
    {
        idx = std::clamp(idx, 0, (int)ModalBankMaterialCount - 1);
        if (idx < (int)MODAL_BANK_FIRST_GENERATOR) { fBank.setMaterial(kModalMaterials[idx]); return; }
        const ModalSeriesID series = (ModalSeriesID)(idx - MODAL_BANK_FIRST_GENERATOR);
        const float param = (series == ModalSeriesPiano) ? 0.0003f
                          : (series == ModalSeriesPower) ? 1.5f : 0.0f;
        const int n = generateModalSeries(series, param, ModalBank::kMaxModes,
                                          fGenBuffer, ModalBank::kMaxModes);
        fGenMaterial.modes = fGenBuffer;
        fGenMaterial.count = n;
        fGenMaterial.name  = "Gen";
        fBank.setMaterial(fGenMaterial);
        fBank.invalidate();
    }

    ModalBank     fBank;
    ModalMode     fGenBuffer[ModalBank::kMaxModes];
    ModalMaterial fGenMaterial { nullptr, 0, "Gen" };
    Color         fAccent = colObject();

    int   fMaterial = 1, fModes = 64, fStretchIdx = 9;
    float fPitch = 0.0f, fDecayT60 = 2.0f, fDampTilt = 0.9f, fBrightness = 0.0f;
    float fBellAmt = 0.0f, fBellCtr = 2.0f, fBellWid = 0.4f;

    int   fCount = 0;
    float fFreq[ModalBank::kMaxModes] = {};
    float fT60 [ModalBank::kMaxModes] = {};
    float fMinFreq = 100.0f, fMaxFreq = 1000.0f, fT60Max = 1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModalSpectrum)
};

// --------------------------------------------------------------------------------------------
// RoomDecay — every room mode's decay against its frequency, at C4: x = frequency (log, from a
// quarter of the note to the Detail limit), y = T60. The same formula as Room::rebuildGeometry /
// retunePitch, so it shows what the engine will do: absorption and the HF corner pull the cloud
// down, Air caps it. Each dot is coloured by the wall pair its plane waves mostly hit, so the two
// pairs' shares are visible. Marks along the bottom show where a ringing wall's fundamental sits.
class RoomDecay : public NanoSubWidget
{
public:
    struct WallView { float absorb, bright, ring, ratio; };

    explicit RoomDecay(Widget* parent) : NanoSubWidget(parent) { fHasFont = loadSharedResources(); }

    void setUiScale(float s) { fUiScale = s; }

    void update(float width, float depth, float air, float detail, WallView side, WallView frontBack)
    {
        fWall[0] = side; fWall[1] = frontBack;
        fCount = 0; fMaxT60 = 0.1f;
        fDetail = std::max(2.0f, detail);
        const double W = std::max(0.05f, width), D = std::max(0.05f, depth);
        // Thin a dense room to at most ~kMaxDots dots: step through (nx, ny) on a coarser grid.
        const double total = M_PI * W * D * fDetail * fDetail;
        const int step = std::max(1, (int)std::ceil(std::sqrt(total / kMaxDots)));
        const double airRate = 13.815510557964274 / std::max(0.1f, air);
        auto loss = [](const WallView& w, double q) {
            const double a = std::clamp((double)w.absorb, 0.0, 0.999);
            const double fc = std::exp2((double)w.bright);
            return -std::log(1.0 - a) + std::log1p((q / fc) * (q / fc));
        };
        for (int nx = 0; nx <= (int)(2.0 * W * fDetail) && fCount < kMaxDots; nx += step)
            for (int ny = 0; ny <= (int)(2.0 * D * fDetail) && fCount < kMaxDots; ny += step)
            {
                if (nx == 0 && ny == 0) continue;
                const double kx = nx / W, ky = ny / D, kk = std::hypot(kx, ky), q = 0.5 * kk;
                if (q > fDetail) continue;
                const double cx = kx / kk, cy = ky / kk;
                const double rate = kF0 * ((cx / W) * loss(fWall[0], q) + (cy / D) * loss(fWall[1], q)) + airRate;
                fQ[fCount]    = (float)q;
                fT60[fCount]  = (float)(13.815510557964274 / rate);
                fSide[fCount] = (float)(cx * cx);   // share of the side walls, 0..1
                fMaxT60 = std::max(fMaxT60, fT60[fCount]);
                ++fCount;
            }
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        beginPath(); roundedRect(0, 0, w, h, 6); fillColor(Color(46, 40, 22)); fill();
        beginPath(); roundedRect(0.5f, 0.5f, w - 1, h - 1, 6);
        strokeColor(Color(80, 70, 42)); strokeWidth(1.0f); stroke();

        const float x0 = 44 * s, x1 = w - 12 * s, yTop = 24 * s, y1 = h - 20 * s;
        if (x1 <= x0 || y1 <= yTop) return;
        const float qLo = std::log2(0.25f), qHi = std::log2(fDetail);
        auto px = [&](float q) { return x0 + (std::log2(std::max(q, 0.25f)) - qLo) / (qHi - qLo) * (x1 - x0); };
        auto py = [&](float t) { return y1 - std::min(t / fMaxT60, 1.0f) * (y1 - yTop); };

        // Harmonics of the note, for orientation.
        beginPath();
        for (int n = 1; n <= (int)fDetail; n *= 2) { moveTo(px((float)n), yTop); lineTo(px((float)n), y1); }
        strokeColor(Color(72, 64, 40)); strokeWidth(1.0f); stroke();

        const Color side = colSideWalls(), fb = colFBWalls();
        for (int i = 0; i < fCount; ++i)
        {
            const float a = fSide[i];
            Color c((int)(side.red * 255 * a + fb.red * 255 * (1 - a)),
                    (int)(side.green * 255 * a + fb.green * 255 * (1 - a)),
                    (int)(side.blue * 255 * a + fb.blue * 255 * (1 - a)));
            c.alpha = 0.75f;
            beginPath(); rect(px(fQ[i]) - 1.0f, py(fT60[i]) - 1.0f, 2.0f, 2.0f); fillColor(c); fill();
        }

        // Ringing walls: a mark at each wall pair's fundamental.
        for (int p = 0; p < 2; ++p)
        {
            if (fWall[p].ring <= 0.0f) continue;
            const float x = px(fWall[p].ratio);
            beginPath(); moveTo(x - 5 * s, y1 + 9 * s); lineTo(x + 5 * s, y1 + 9 * s); lineTo(x, y1 + 2 * s); closePath();
            fillColor(p == 0 ? side : fb); fill();
        }

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f * s);
            fillColor(Color(150, 154, 162));
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(10 * s, 6 * s, "Room decay vs frequency, at C4", nullptr);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f s", fMaxT60);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(x0 - 6 * s, yTop, buf, nullptr);
            text(x0 - 6 * s, y1, "0", nullptr);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            for (int n = 1; n <= (int)fDetail; n *= 2)
            {
                std::snprintf(buf, sizeof(buf), n == 1 ? "f0" : "%dx", n);
                text(px((float)n), y1 + 3 * s, buf, nullptr);
            }
        }
    }

private:
    static constexpr int    kMaxDots = 4000;
    static constexpr double kF0 = 261.6255653005986;   // C4: where the "at note 60" values apply

    WallView fWall[2] {};
    float fQ[kMaxDots] = {}, fT60[kMaxDots] = {}, fSide[kMaxDots] = {};
    int   fCount = 0;
    float fMaxT60 = 1.0f, fDetail = 12.0f, fUiScale = 1.0f;
    bool  fHasFont = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoomDecay)
};

// --------------------------------------------------------------------------------------------
class StruckRoomUI : public UI,
                      public Knob::Callback,
                      public Dropdown::Callback,
                      public PopupMenu::Callback,
                      public RoomMap::Callback,
                      public TabBar::Callback
{
public:
    StruckRoomUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        setGeometryConstraints(864, 512, true, false); // lock the 1080:640 design ratio (0.8x min)
        fHasFont = loadSharedResources();               // DPF's bundled DejaVu Sans for the title

        // DPF is only the GUI layer here: UIExporter set uiData->dspPtr (our clap_plugin_t) before
        // calling createUI(), so the seam in plugin_gui.h is already usable. Reading the values
        // through it means an editor opened on a host-restored patch starts in sync instead of
        // showing the parameter defaults.
        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = fPlugin != nullptr ? static_cast<float>(ss_param_value(fPlugin, i))
                                            : kParams[i].def;

        fSpectrum  = std::make_unique<ModalSpectrum>(this);
        fRoomMap   = std::make_unique<RoomMap>(this, this);
        fRoomMap->setColors(colObject(), Color(230, 230, 236), colSideWalls(), colFBWalls());
        fRoomDecay = std::make_unique<RoomDecay>(this);
        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);

        fGlobalGroup = makeGroup("Global", colGlobal());
        fStrikeGroup = makeGroup("Strike", colStrike());
        fObjectGroup = makeGroup("Object", colObject());
        fDecayGroup  = makeGroup("Decay",  colDecay());
        fOutputGroup = makeGroup("Timbre", colOutput());
        fExpressionGroup = makeGroup("Expression", colExpression());
        fRoomGroup       = makeGroup("Room",              colRoom());
        fPlaceGroup      = makeGroup("Placement",         colPlacement());
        fMixGroup        = makeGroup("Mix",               colMix());
        fSideGroup       = makeGroup("Side walls",        colSideWalls());
        fFBGroup         = makeGroup("Front / back walls", colFBWalls());
        fSideGroup->setColumns(4);   // twelve controls each: three rows of four, the effects last
        fFBGroup->setColumns(4);

        // Build each param control into its panel, in panel order.
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            GroupBox* g = groupFor(i);
            if (isDropdownParam(i))
            {
                auto d = std::make_unique<Dropdown>(g, this);
                d->setId(i);
                d->setLabel(kParams[i].name);
                d->setChoices(choicesForWidget(i));
                d->setIndex(static_cast<int>(fValues[i] + 0.5f));   // fValues came from the seam
                g->addControl(d.get());
                fDropdowns[i] = std::move(d);
            }
            else
            {
                const ParamSpec& s = kParams[i];
                auto k = std::make_unique<Knob>(g, this);
                k->setId(i);
                k->setLabel(s.name);
                k->setAccentColor(accentFor(i));
                k->configure(s.min, s.max, s.logarithmic, s.def);
                k->setRealValue(fValues[i], false);                 // fValues came from the seam
                k->setFormatter(formatterFor(i));
                g->addControl(k.get());
                fKnobs[i] = std::move(k);
            }
        }

        fTabBar = std::make_unique<TabBar>(this, this);
        fTabBar->setTabs({ "Object", "Room", "Walls" });
        fTabBar->setAccentColor(Color(224, 190, 150));

        // Preset selector: applies to the whole patch, so it's parented to the UI (like fMenu)
        // and sits in the header strip, not in a GroupBox. Shares the generic Dropdown/PopupMenu
        // plumbing every other dropdown uses; menuSelected() tells it apart via kPresetDropdownId.
        fPresetDropdown = std::make_unique<Dropdown>(this, this);
        fPresetDropdown->setId(kPresetDropdownId);
        fPresetDropdown->setLabel("Preset");
        {
            std::vector<std::string> names;
            for (int i = 0; i < kPresetCount; ++i) names.push_back(kPresets[i].name);
            fPresetDropdown->setChoices(names);
        }
        fPresetDropdown->setPlaceholder("--");
        fPresetDropdown->setIndex(-1); // no preset selected until the user picks one

        // help.html shows the logo as "Unlikelyware.png", so it is written out under that name.
        fHelpButton = std::make_unique<HelpButton>(this, std::vector<HelpButton::File>{
            { "StruckRoom-help.html", EmbeddedResources::helpData, EmbeddedResources::helpDataSize },
            { "Unlikelyware.png", EmbeddedResources::Unlikelyware_Final_ColorData,
                                  EmbeddedResources::Unlikelyware_Final_ColorDataSize },
        });
        fHelpButton->setLabel("Help");

        // Save / Load the patch as a preset file (PresetFile.hpp), through the system file dialog.
        fSaveButton = std::make_unique<HeaderButton>(this, "Save", [this] { browseForPreset(true); });
        fLoadButton = std::make_unique<HeaderButton>(this, "Load", [this] { browseForPreset(false); });

        fMenu = std::make_unique<PopupMenu>(this, this); // created last → draws on top

        pushSpectrum();
        pushRoomViews();
        showTab(0);
        layout();
    }

protected:
    /* -- host -> UI ------------------------------------------------------- */
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        fValues[index] = value;
        if (fKnobs[index])     { fKnobs[index]->setRealValue(value, false); fKnobs[index]->repaint(); }
        if (fDropdowns[index])   fDropdowns[index]->setIndex(static_cast<int>(value + 0.5f));
        if (affectsSpectrum(index)) pushSpectrum();
        if (affectsRoomViews(index)) pushRoomViews();
    }

    /* -- knob -> host ----------------------------------------------------- */
    void knobDragStarted(Knob* k) override  { editParameter(k->getId(), true); }
    void knobDragFinished(Knob* k) override { editParameter(k->getId(), false); }
    void knobValueChanged(Knob* k, float v) override
    {
        const uint32_t id = k->getId();
        fValues[id] = v;
        setParameterValue(id, v);
        if (affectsSpectrum(id)) pushSpectrum();
        if (affectsRoomViews(id)) pushRoomViews();
    }

    /* -- room map -> host: the markers are Src X/Y and Mic X/Y ------------- */
    static uint32_t markerX(int marker) { return marker == RoomMap::kObject ? kParamSrcX : kParamMicX; }
    static uint32_t markerY(int marker) { return marker == RoomMap::kObject ? kParamSrcY : kParamMicY; }
    void roomMapDragStarted(int marker) override
    {
        editParameter(markerX(marker), true);
        editParameter(markerY(marker), true);
    }
    void roomMapMoved(int marker, float x, float y) override
    {
        for (const auto& [id, v] : { std::pair{markerX(marker), x}, std::pair{markerY(marker), y} })
        {
            fValues[id] = v;
            setParameterValue(id, v);
            if (fKnobs[id]) { fKnobs[id]->setRealValue(v, false); fKnobs[id]->repaint(); }
        }
    }
    void roomMapDragFinished(int marker) override
    {
        editParameter(markerX(marker), false);
        editParameter(markerY(marker), false);
    }

    /* -- tabs -------------------------------------------------------------- */
    void tabSelected(TabBar*, int index) override { showTab(index); }

    void showTab(int index)
    {
        fTab = index;
        if (fMenu) fMenu->dismiss();
        const bool object = index == 0, room = index == 1, walls = index == 2;
        for (SubWidget* w : std::initializer_list<SubWidget*>{
                 fSpectrum.get(), fGlobalGroup.get(), fStrikeGroup.get(), fObjectGroup.get(),
                 fDecayGroup.get(), fOutputGroup.get(), fExpressionGroup.get() })
            w->setVisible(object);
        for (SubWidget* w : std::initializer_list<SubWidget*>{
                 fRoomMap.get(), fRoomGroup.get(), fPlaceGroup.get(), fMixGroup.get() })
            w->setVisible(room);
        for (SubWidget* w : std::initializer_list<SubWidget*>{
                 fSideGroup.get(), fFBGroup.get(), fRoomDecay.get() })
            w->setVisible(walls);
        if (fTabBar) fTabBar->setActive(index);
        repaint();
    }

    /* -- dropdown + menu -------------------------------------------------- */
    void dropdownClicked(Dropdown* d) override
    {
        int x, y, w, h;
        d->boxRect(x, y, w, h);
        fMenu->openFor(d->getId(), d->choices(), d->index(), x, y + h, y, w,
                       static_cast<int>(getHeight()));
    }
    // A click outside the plugin window never reaches us, so treat losing focus to the host (or
    // another app) as "clicked elsewhere" and close any open menu. Clicks inside the window are
    // handled by the menu itself.
    void uiFocus(bool focus, DGL_NAMESPACE::CrossingMode mode) override
    {
        UI::uiFocus(focus, mode);
        if (!focus && fMenu != nullptr)
            fMenu->dismiss();
    }

    void menuSelected(PopupMenu* menu, int index) override
    {
        const uint32_t id = menu->tag();
        if (id == kPresetDropdownId) { applyPreset(index); return; }
        if (id >= kParamCount) return;
        editParameter(id, true);
        setParameterValue(id, static_cast<float>(index));
        editParameter(id, false);
        fValues[id] = static_cast<float>(index);
        if (fDropdowns[id]) fDropdowns[id]->setIndex(index);
        if (affectsSpectrum(id)) pushSpectrum();
        if (affectsRoomViews(id)) pushRoomViews();
    }

    // Apply every param the preset has a value for (looked up by "module.item" key via kPresetKeys —
    // the same mapping PresetFile.hpp uses to write these files). Params the preset doesn't mention
    // keep their current value; values are clamped to the param's range, since a loaded file may
    // come from anywhere. parameterChanged() already syncs the widget and the spectrum for each
    // param, so it's reused here instead of duplicating that dispatch.
    template <typename Find>
    void applyPresetValues(Find find)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            const std::string key = std::string(kPresetKeys[i].module) + "." + kPresetKeys[i].item;
            float v;
            if (!find(key, v)) continue;
            v = std::clamp(v, kParams[i].min, kParams[i].max);
            if (kParams[i].integer) v = std::round(v);
            editParameter(i, true);
            setParameterValue(i, v);
            editParameter(i, false);
            parameterChanged(i, v); // updates fValues[i], the widget, and the spectrum
        }
    }

    void applyPreset(int index)
    {
        if (index < 0 || index >= kPresetCount) return;
        const PresetEntry& preset = kPresets[index];
        applyPresetValues([&](const std::string& key, float& v) {
            for (int j = 0; j < preset.count; ++j)
                if (key == preset.values[j].key) { v = preset.values[j].value; return true; }
            return false;
        });
        if (fPresetDropdown) fPresetDropdown->setIndex(index);
        fPatchName = preset.name; // Save offers it as the file name
    }

    /* -- Save / Load ------------------------------------------------------ */
    // The dialog is asynchronous: openFileBrowser() returns at once and the chosen path arrives in
    // uiFileBrowserSelected(), so fBrowsing remembers which of the two asked. A second click opens
    // a new dialog in place of one still open (DPF closes the old one), so a result that never
    // arrives can't leave the buttons dead.
    enum class Browse { None, Save, Load };

    void browseForPreset(bool saving)
    {
        const std::string dir = presetDir();
        const std::string defaultName = (fPatchName.empty() ? std::string("Untitled") : fPatchName) + ".json";
        FileBrowserOptions opts;
        opts.saving      = saving;
        opts.title       = saving ? "Save StruckRoom Patch" : "Load StruckRoom Patch";
        opts.startDir    = dir.empty() ? nullptr : dir.c_str();
        opts.defaultName = saving ? defaultName.c_str() : nullptr;
        opts.extension   = saving ? nullptr : "json";
        fBrowsing = saving ? Browse::Save : Browse::Load;
        if (!openFileBrowser(opts))
            fBrowsing = Browse::None;
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        const Browse mode = fBrowsing;
        fBrowsing = Browse::None;
        if (filename == nullptr || filename[0] == '\0') return; // cancelled

        namespace fs = std::filesystem;
        fs::path path(filename);
        if (mode == Browse::Save)
        {
            if (!path.has_extension()) path += ".json";
            const std::string name = path.stem().string();
            if (!writePresetFile(path.string(), fValues, name))
            {
                std::fprintf(stderr, "[StruckRoom] could not save %s\n", path.string().c_str());
                showStatus("Couldn't save");
                return;
            }
            showPatchName(name);
        }
        else if (mode == Browse::Load)
        {
            PresetFileData data;
            if (!readPresetFile(path.string(), data))
            {
                std::fprintf(stderr, "[StruckRoom] %s is not a StruckRoom preset\n",
                             path.string().c_str());
                showStatus("Couldn't load");
                return;
            }
            applyPresetValues([&](const std::string& key, float& v) {
                for (const auto& kv : data.values)
                    if (key == kv.first) { v = kv.second; return true; }
                return false;
            });
            // Named as the factory presets are (tools/gen_presets.py): meta.name, else the file stem.
            const bool hasName = !data.name.empty() && data.name != "Untitled";
            showPatchName(hasName ? data.name : path.stem().string());
        }
        else return;
        fLastDir = path.parent_path().string();
    }

    // A saved or loaded patch is not a factory preset: the menu shows its name, dimmed, with none
    // of the factory entries ticked.
    void showPatchName(const std::string& name)
    {
        fPatchName = name;
        if (fPresetDropdown) { fPresetDropdown->setPlaceholder(name); fPresetDropdown->setIndex(-1); }
    }

    // A failed Save / Load says so in the menu, where the patch name would go: stderr is lost in most
    // hosts. Unlike showPatchName(), it leaves the name Save offers alone.
    void showStatus(const std::string& text)
    {
        if (fPresetDropdown) { fPresetDropdown->setPlaceholder(text); fPresetDropdown->setIndex(-1); }
    }

    // The last folder used, else Documents/StruckRoom (made on first use), else the home folder.
    // The home folder is the account's, not $HOME: in a sandboxed host process (Logic runs AUs in
    // one) $HOME is the sandbox's container. There, making the folder fails and the dialog opens on
    // the home folder; the dialog itself grants access to whatever is picked.
    std::string presetDir() const
    {
        if (!fLastDir.empty()) return fLastDir;
        namespace fs = std::filesystem;
        const char* home = nullptr;
#ifndef _WIN32
        if (const passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
#endif
        if (home == nullptr) home = std::getenv("HOME");
        if (home == nullptr) home = std::getenv("USERPROFILE");
        if (home == nullptr) return "";
        std::error_code ec;
        const fs::path dir = fs::path(home) / "Documents" / "StruckRoom";
        fs::create_directories(dir, ec);
        return ec ? std::string(home) : dir.string();
    }

    /* -- drawing / layout ------------------------------------------------- */
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); rect(0, 0, w, h); fillColor(Color(32, 28, 14)); fill();

        // Title, just to the right of the brand logo (fTitleX set in layout()).
        if (fHasFont)
        {
            const float s = w / 900.0f;
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(22 * s);
            fillColor(Color(224, 190, 150));
            text(fTitleX, 20 * s, "StruckRoom", nullptr);
            fillColor(Color(150, 154, 162)); fontSize(12 * s);
            text(fTitleX + 132 * s, 22 * s, "struck, in a room", nullptr);
        }
    }
    void onResize(const ResizeEvent& ev) override { UI::onResize(ev); layout(); }

    // Some Linux hosts (notably REAPER via pugl) don't apply the UI's initial size until the
    // event loop starts pumping, so the window loads mis-sized (top gap, bottom row clipped)
    // until a manual resize. Re-assert the default size once on the first idle tick to force
    // the host to apply correct geometry, which drives onResize() -> layout().
    void uiIdle() override
    {
        if (fInitialSizeNudged) return;
        fInitialSizeNudged = true;
        setSize(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT);
    }

#if STRUCKROOM_PRESET_DUMP
    // Dev-only: press 'P' to write the current patch as JSON to $HOME (see PresetDump.hpp). fValues
    // already mirrors every param, so it's the patch snapshot.
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (ev.press && ev.key == 'p')
        {
            const std::string path = dumpPresetToFile(fValues);
            std::fprintf(stderr, "[StruckRoom] patch %s %s\n",
                         path.empty() ? "dump FAILED" : "dumped to", path.c_str());
            return true;
        }
        return UI::onKeyboard(ev);
    }
#endif

private:
    void layout()
    {
        const int W = getWidth(), H = getHeight();
        const float s = W / 1080.0f;
        const int M   = static_cast<int>(16 * s);
        const int hdr = static_cast<int>(52 * s); // header strip: brand + title left, preset right
        const int gap = static_cast<int>(10 * s);

        // Brand logo top-left; the title starts just to its right (used by onNanoDisplay).
        const int logoH = static_cast<int>(24 * s);
        fLogo->setHeightPreservingAspect(static_cast<float>(logoH));
        fLogo->setAbsolutePos(M, static_cast<int>(8 * s));
        fTitleX = M + static_cast<int>(fLogo->getWidth()) + static_cast<int>(12 * s);
        fMenu->setUiScale(s);

        // Preset selector, right-aligned in the header. Height fits the Dropdown's label + box
        // (16 + 26 units, scaled); it seats within the 52-unit header.
        const int presetW = static_cast<int>(200 * s);
        const int presetH = static_cast<int>(44 * s);
        fPresetDropdown->setUiScale(s);
        fPresetDropdown->setSize(static_cast<uint>(presetW), static_cast<uint>(presetH));
        fPresetDropdown->setAbsolutePos(W - M - presetW, static_cast<int>(4 * s));

        // Help, just left of the Preset menu, captioned and boxed the same way.
        const int helpW = static_cast<int>(44 * s);
        fHelpButton->setUiScale(s);
        fHelpButton->setSize(static_cast<uint>(helpW), static_cast<uint>(presetH));
        fHelpButton->setAbsolutePos(W - M - presetW - helpW, static_cast<int>(4 * s));

        // Save and Load, left of Help, their boxes level with the others'.
        const int fileW = static_cast<int>(52 * s);
        const int loadX = W - M - presetW - helpW - fileW;
        fLoadButton->setUiScale(s);
        fLoadButton->setSize(static_cast<uint>(fileW), static_cast<uint>(presetH));
        fLoadButton->setAbsolutePos(loadX, static_cast<int>(4 * s));
        fSaveButton->setUiScale(s);
        fSaveButton->setSize(static_cast<uint>(fileW), static_cast<uint>(presetH));
        fSaveButton->setAbsolutePos(loadX - fileW, static_cast<int>(4 * s));

        // 3 rows under the header. Within a row the width is split by the cells each panel needs (its
        // control count), so controls get similar widths everywhere instead of a 2-control panel taking
        // half the window. Top: spectrum | Strike. Middle: Object | Decay. Bottom: Global | Timbre |
        // Expression.
        const int gridY = hdr;
        const int gridH = H - gridY - M;
        const int rowH  = (gridH - 2 * gap) / 3;
        const int row0  = gridY, row1 = gridY + rowH + gap, row2 = gridY + 2 * (rowH + gap);

        auto cells = [](const GroupBox* g) { return std::max(1, g->controlCount()); };
        struct Slot { SubWidget* widget; int cells; };
        // Lay out one row left to right, each widget's width in proportion to its cells.
        auto placeRow = [&](std::initializer_list<Slot> slots, int y) {
            const int n = static_cast<int>(slots.size());
            const int rowW = W - 2 * M - (n - 1) * gap;   // the panels, without the gaps between them
            int total = 0;
            for (const Slot& slot : slots) total += slot.cells;
            int x = M, used = 0, done = 0;
            for (const Slot& slot : slots) {
                used += slot.cells;
                const int right = rowW * used / total;   // cumulative, so rounding never drifts
                slot.widget->setAbsolutePos(x, y);
                slot.widget->setSize(static_cast<uint>(right - done), static_cast<uint>(rowH));
                x += (right - done) + gap;
                done = right;
            }
        };
        GroupBox* panels[11] = { fGlobalGroup.get(), fStrikeGroup.get(), fObjectGroup.get(),
                                 fDecayGroup.get(), fOutputGroup.get(), fExpressionGroup.get(),
                                 fRoomGroup.get(), fPlaceGroup.get(), fMixGroup.get(),
                                 fSideGroup.get(), fFBGroup.get() };
        for (GroupBox* g : panels) g->setUiScale(s);

        // Tabs, centred in the header between the title and Help.
        const int tabW = static_cast<int>(300 * s), tabH = static_cast<int>(28 * s);
        fTabBar->setSize(static_cast<uint>(tabW), static_cast<uint>(tabH));
        fTabBar->setAbsolutePos((W - tabW) / 2, static_cast<int>(12 * s));

        // The spectrum holds no controls; it reads fine at about three control cells wide.
        constexpr int kSpectrumCells = 3;
        placeRow({ { fSpectrum.get(), kSpectrumCells }, { fStrikeGroup.get(), cells(fStrikeGroup.get()) } }, row0);
        placeRow({ { fObjectGroup.get(), cells(fObjectGroup.get()) },
                   { fDecayGroup.get(),  cells(fDecayGroup.get()) } }, row1);
        placeRow({ { fGlobalGroup.get(), cells(fGlobalGroup.get()) },
                   { fOutputGroup.get(), cells(fOutputGroup.get()) },
                   { fExpressionGroup.get(), cells(fExpressionGroup.get()) } }, row2);

        // Room tab: the map takes the left part at full height; Room, Placement and Mix stack on the
        // right, one per row, so their knobs match the Object tab's.
        const int mapW = (W - 2 * M - gap) * 44 / 100;
        fRoomMap->setUiScale(s);
        fRoomMap->setAbsolutePos(M, row0);
        fRoomMap->setSize(static_cast<uint>(mapW), static_cast<uint>(gridH));
        const int colX = M + mapW + gap, colW = W - M - colX;
        int y = row0;
        for (GroupBox* g : { fRoomGroup.get(), fPlaceGroup.get(), fMixGroup.get() })
        {
            g->setAbsolutePos(colX, y);
            g->setSize(static_cast<uint>(colW), static_cast<uint>(rowH));
            y += rowH + gap;
        }

        // Walls tab: the two pairs side by side over two rows' height (three rows of four controls
        // each, so a little smaller than elsewhere), the decay view across the bottom row.
        const int halfW = (W - 2 * M - gap) / 2;
        fSideGroup->setAbsolutePos(M, row0);
        fSideGroup->setSize(static_cast<uint>(halfW), static_cast<uint>(2 * rowH + gap));
        fFBGroup->setAbsolutePos(M + halfW + gap, row0);
        fFBGroup->setSize(static_cast<uint>(W - 2 * M - gap - halfW), static_cast<uint>(2 * rowH + gap));
        fRoomDecay->setUiScale(s);
        fRoomDecay->setAbsolutePos(M, row2);
        fRoomDecay->setSize(static_cast<uint>(W - 2 * M), static_cast<uint>(rowH));

        // Knob diameter bounded by the row height (room for label + value) and, via relayout, the
        // cell width. Dropdowns are sized to their cell by relayout(); we only set their scale.
        const float knobD = std::min(74.0f * s, rowH * 0.44f);
        // A wall cell is a third of the panel's content height; this leaves its knob room for the
        // label and value (about 0.85 of the other tabs' knobs at the default size).
        const float wallS = 0.85f * s;
        const float wallCellH = (2 * rowH + gap - 38 * s) / 3.0f;
        const float wallKnobD = std::min(knobD * 0.85f, wallCellH * 0.56f);
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            const bool wall = isSideWall(i) || isFBWall(i);
            if (fKnobs[i])     fKnobs[i]->setDiameter(wall ? wallKnobD : knobD, wall ? wallS : s);
            if (fDropdowns[i]) fDropdowns[i]->setUiScale(wall ? wallS : s);
        }
        for (GroupBox* g : panels) g->relayout();
    }

    std::unique_ptr<GroupBox> makeGroup(const char* title, Color accent)
    {
        auto g = std::make_unique<GroupBox>(this);
        g->setTitle(title);
        g->setAccentColor(accent);
        return g;
    }

    static bool isSideWall(uint32_t i)
    {
        return (i >= kParamSideAbsorb && i <= kParamSideLink) || (i >= kParamSideShift && i <= kParamSideGap);
    }
    static bool isFBWall(uint32_t i)
    {
        return (i >= kParamFBAbsorb && i <= kParamFBLink) || (i >= kParamFBShift && i <= kParamFBGap);
    }

    GroupBox* groupFor(uint32_t i) const
    {
        switch (i)
        {
        case kParamVoiceMode: case kParamGlide: case kParamLevel: case kParamSpread: case kParamMpe:
            return fGlobalGroup.get();
        case kParamStrikeType: case kParamHardness: case kParamVelToLevel:
        case kParamVelToHardness: case kParamContactMode:
            return fStrikeGroup.get();
        case kParamPitch: case kParamMaterial: case kParamModeCount: case kParamStretch:
            return fObjectGroup.get();
        case kParamDecayT60: case kParamDampTilt: case kParamDampBellAmount:
        case kParamDampBellCentre: case kParamDampBellWidth:
            return fDecayGroup.get();
        case kParamPressToDecay: case kParamTimbreToTilt: case kParamTimbreToPos:
            return fExpressionGroup.get();
        case kParamRoomWidth: case kParamRoomDepth: case kParamRoomTrack: case kParamDecayTrack:
        case kParamRoomAir: case kParamRoomDetail:
            return fRoomGroup.get();
        case kParamSrcX: case kParamSrcY: case kParamMicX: case kParamMicY:
            return fPlaceGroup.get();
        case kParamDirect: case kParamEarly: case kParamLate:
            return fMixGroup.get();
        default:
            if (isSideWall(i)) return fSideGroup.get();
            if (isFBWall(i))   return fFBGroup.get();
            return fOutputGroup.get(); // strikePos, brightness
        }
    }

    static Color accentFor(uint32_t i)
    {
        switch (i)
        {
        case kParamVoiceMode: case kParamGlide: case kParamLevel: case kParamSpread: case kParamMpe:
            return colGlobal();
        case kParamStrikeType: case kParamHardness: case kParamVelToLevel:
        case kParamVelToHardness: case kParamContactMode:
            return colStrike();
        case kParamPitch: case kParamMaterial: case kParamModeCount: case kParamStretch:
            return colObject();
        case kParamDecayT60: case kParamDampTilt: case kParamDampBellAmount:
        case kParamDampBellCentre: case kParamDampBellWidth:
            return colDecay();
        case kParamPressToDecay: case kParamTimbreToTilt: case kParamTimbreToPos:
            return colExpression();
        case kParamRoomWidth: case kParamRoomDepth: case kParamRoomTrack: case kParamDecayTrack:
        case kParamRoomAir: case kParamRoomDetail:
            return colRoom();
        case kParamSrcX: case kParamSrcY: case kParamMicX: case kParamMicY:
            return colPlacement();
        case kParamDirect: case kParamEarly: case kParamLate:
            return colMix();
        default:
            if (isSideWall(i)) return colSideWalls();
            if (isFBWall(i))   return colFBWalls();
            return colOutput();
        }
    }

    // The label lists live in StruckRoomParams.h so the CLAP params extension reports exactly the
    // same enums; this just adapts them to the widget's std::vector<std::string> shape.
    static std::vector<std::string> choicesForWidget(uint32_t i)
    {
        std::vector<std::string> v;
        for (const char* const* c = struckroom::choicesFor(i); c != nullptr && *c != nullptr; ++c)
            v.emplace_back(*c);
        return v;
    }

    static bool affectsSpectrum(uint32_t i)
    {
        return i == kParamMaterial || i == kParamModeCount || i == kParamStretch
            || i == kParamPitch || i == kParamDecayT60 || i == kParamDampTilt
            || i == kParamBrightness || i == kParamDampBellAmount
            || i == kParamDampBellCentre || i == kParamDampBellWidth;
    }

    static bool affectsRoomViews(uint32_t i) { return i >= kParamRoomWidth; }

    void pushRoomViews()
    {
        const float* v = fValues;
        fRoomMap->setRoom(v[kParamRoomWidth], v[kParamRoomDepth]);
        fRoomMap->setObject(v[kParamSrcX], v[kParamSrcY]);
        fRoomMap->setMic(v[kParamMicX], v[kParamMicY]);
        fRoomMap->setWall(0, v[kParamSideAbsorb], v[kParamSideRing]);
        fRoomMap->setWall(1, v[kParamFBAbsorb], v[kParamFBRing]);
        fRoomDecay->update(v[kParamRoomWidth], v[kParamRoomDepth], v[kParamRoomAir], v[kParamRoomDetail],
                           { v[kParamSideAbsorb], v[kParamSideBright], v[kParamSideRing], v[kParamSideRingRatio] },
                           { v[kParamFBAbsorb], v[kParamFBBright], v[kParamFBRing], v[kParamFBRingRatio] });
    }

    void pushSpectrum()
    {
        fSpectrum->update(static_cast<int>(fValues[kParamMaterial] + 0.5f),
                          static_cast<int>(fValues[kParamModeCount] + 0.5f),
                          static_cast<int>(fValues[kParamStretch] + 0.5f),
                          fValues[kParamPitch], fValues[kParamDecayT60],
                          fValues[kParamDampTilt], fValues[kParamBrightness],
                          fValues[kParamDampBellAmount], fValues[kParamDampBellCentre],
                          fValues[kParamDampBellWidth]);
    }

    static std::function<std::string(float)> formatterFor(uint32_t i)
    {
        const std::string u = kParams[i].unit;
        const bool integer = kParams[i].integer;
        const bool isTime  = (u == "s");
        return [u, integer, isTime](float v) {
            char b[32];
            if (integer)      std::snprintf(b, sizeof(b), "%d", static_cast<int>(v + 0.5f));
            else if (isTime)  v < 1.0f ? std::snprintf(b, sizeof(b), "%.0f ms", v * 1000.0f)
                                       : std::snprintf(b, sizeof(b), "%.2f s", v);
            else if (!u.empty()) std::snprintf(b, sizeof(b), "%.2f %s", v, u.c_str());
            else              std::snprintf(b, sizeof(b), "%.2f", v);
            return std::string(b);
        };
    }

    std::unique_ptr<ModalSpectrum> fSpectrum;
    std::unique_ptr<GroupBox>      fGlobalGroup, fStrikeGroup, fObjectGroup, fDecayGroup, fOutputGroup,
                                   fExpressionGroup;
    std::unique_ptr<RoomMap>       fRoomMap;
    std::unique_ptr<RoomDecay>     fRoomDecay;
    std::unique_ptr<GroupBox>      fRoomGroup, fPlaceGroup, fMixGroup, fSideGroup, fFBGroup;
    std::unique_ptr<TabBar>        fTabBar;
    int                            fTab = 0;
    std::unique_ptr<Knob>          fKnobs[kParamCount];
    std::unique_ptr<Dropdown>      fDropdowns[kParamCount];
    std::unique_ptr<PopupMenu>     fMenu;
    std::unique_ptr<Logo>          fLogo;
    std::unique_ptr<Dropdown>      fPresetDropdown;
    std::unique_ptr<HelpButton>    fHelpButton;
    std::unique_ptr<HeaderButton>  fSaveButton, fLoadButton;
    Browse                         fBrowsing = Browse::None;
    std::string                    fPatchName;          // the saved / loaded patch, "" for none yet
    std::string                    fLastDir;            // where the last Save / Load went
    const clap_plugin_t*           fPlugin = nullptr;   // the raw CLAP plugin (see plugin_gui.h)
    float                          fValues[kParamCount];
    bool                           fHasFont = false;
    bool                           fInitialSizeNudged = false;
    int                            fTitleX = 16;

    // Sentinel id for the preset dropdown, past the real param range so menuSelected() can tell it
    // apart from a per-param dropdown.
    static constexpr uint32_t kPresetDropdownId = kParamCount;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StruckRoomUI)
};

UI* createUI()
{
    return new StruckRoomUI();
}

// ================================================================================================
// CLAP `gui` extension
// ================================================================================================
// Modelled on DPF's own ClapUI (DistrhoPluginCLAP.cpp) and PlainOldSynth's UI: gui_create() only
// allocates a holder, because UIExporter cannot be constructed until the host hands us a parent
// window in set_parent(). DPF is used purely as the GUI layer — the plugin itself is the raw CLAP
// plugin in src/plugin.cpp, so parameter traffic crosses the C seam in plugin_gui.h rather than
// DPF's plugin side.

namespace {

struct GuiState {
    const clap_plugin_t* plugin = nullptr;
    std::unique_ptr<UIExporter> exporter;
    uintptr_t parentWindow = 0;
    // The editor size in the HOST's units — logical points on macOS (CLAP cocoa), pixels on X11.
    // This, not the UIExporter's pixel size, is the source of truth: the pixel size is derived from
    // it with the live scale (applySize), so it can be re-derived when the window changes screens.
    uint32_t width  = DISTRHO_UI_DEFAULT_WIDTH;
    uint32_t height = DISTRHO_UI_DEFAULT_HEIGHT;
    double appliedScale = 0.0;   // the scale the UIExporter's pixel size was last derived with
    bool timerRegistered = false;
    clap_id timerId = 0;
#ifdef __APPLE__
    CFRunLoopTimerRef ownTimer = nullptr;   // when the host has no timer-support (startIdleTimer)
#endif
};

#ifdef __APPLE__
extern "C" double ss_gui_backing_scale(uintptr_t nsView);   // BackingScale_mac.mm
#endif

// Host units → UIExporter pixels. On macOS this is the backing scale of the window the editor is in
// RIGHT NOW, deliberately not UIExporter::getScaleFactor(): DPF caches that once, possibly from the
// wrong screen, and never updates it, while pugl maps mouse positions with the live value — see
// BackingScale_mac.mm. On X11, CLAP sizes are already physical pixels, so there is nothing to convert
// (matching DPF's own CLAP wrapper, which scales on macOS only).
double liveScale(const GuiState* gui)
{
#ifdef __APPLE__
    return ss_gui_backing_scale(gui->parentWindow);
#else
    (void)gui;
    return 1.0;
#endif
}

// Size the UIExporter to gui->width x gui->height host units at the live scale. pugl re-reads the
// screen's scale while doing this and re-sends the pixel size, so it also re-syncs DPF after a move
// between screens even when the size in points did not change.
void applySize(GuiState* gui)
{
    if (!gui->exporter) return;
    const double scale = liveScale(gui);
    gui->appliedScale = scale;
    gui->exporter->setWindowSizeFromHost(uint(std::lround(gui->width  * scale)),
                                         uint(std::lround(gui->height * scale)));
}

#ifdef __APPLE__
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

// The UI's idle (ss_gui_on_timer) runs on a host timer. Without idle, DPF never collects the file
// dialog's result (Load and Save do nothing) and the UI's own uiIdle() never runs. clap-wrapper's
// AUv2 host has no timers (its register_timer returns false), so on macOS the UI then runs its own
// on the main run loop, in the common modes so it keeps firing while a menu or sheet is up.
void startIdleTimer(GuiState* gui)
{
    if (gui->timerRegistered) return;
    const clap_host_t* host = ss_plugin_host(gui->plugin);
    const auto* timer = static_cast<const clap_host_timer_support_t*>(
        host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    if (timer != nullptr && timer->register_timer(host, 16 /* ~60fps */, &gui->timerId)) {
        gui->timerRegistered = true;
        return;
    }
#ifdef __APPLE__
    if (gui->ownTimer != nullptr) return;
    CFRunLoopTimerContext context = { 0, gui, nullptr, nullptr, nullptr };
    gui->ownTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.016, 0.016,
                                         0, 0,
                                         [](CFRunLoopTimerRef, void* info) {
                                             ss_gui_on_timer(static_cast<GuiState*>(info)->plugin, 0);
                                         },
                                         &context);
    if (gui->ownTimer != nullptr)
        CFRunLoopAddTimer(CFRunLoopGetMain(), gui->ownTimer, kCFRunLoopCommonModes);
#endif
}

void stopIdleTimer(GuiState* gui)
{
    if (gui->timerRegistered) {
        const clap_host_t* host = ss_plugin_host(gui->plugin);
        const auto* timer = static_cast<const clap_host_timer_support_t*>(
            host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
        if (timer != nullptr) timer->unregister_timer(host, gui->timerId);
        gui->timerRegistered = false;
    }
#ifdef __APPLE__
    if (gui->ownTimer != nullptr) {
        CFRunLoopTimerInvalidate(gui->ownTimer);
        CFRelease(gui->ownTimer);
        gui->ownTimer = nullptr;
    }
#endif
}

bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* isFloating)
{
    *api = kWindowApi;
    *isFloating = false;
    return true;
}

bool gui_create(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!gui_is_api_supported(plugin, api, isFloating)) return false;
    auto* gui = new GuiState();
    gui->plugin = plugin;
    ss_plugin_set_gui(plugin, gui);   // plugin.cpp owns the pointer and hands it back to us
    return true;
}

void gui_destroy(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    ss_plugin_set_gui(plugin, nullptr);
    if (gui == nullptr) return;

    stopIdleTimer(gui);
    delete gui;   // the UIExporter is destroyed here, which tears the UI down
}

// The UIExporter's edit/set callbacks. `ptr` is whatever was passed as UIExporter's first argument —
// we pass the clap_plugin_t, so these forward straight into the seam.
void onEditParam(void* ptr, uint32_t index, bool started)
{
    const auto* plugin = static_cast<const clap_plugin_t*>(ptr);
    if (started) ss_param_begin_gesture(plugin, index);
    else         ss_param_end_gesture(plugin, index);
}

void onSetParam(void* ptr, uint32_t index, float value)
{
    ss_param_set_from_gui(static_cast<const clap_plugin_t*>(ptr), index, double(value));
}

bool gui_set_scale(const clap_plugin_t*, double) { return false; }   // logical size API (cocoa): no-op

bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    // Host units, straight from the source of truth. Converting the UIExporter's pixel size back
    // with DPF's cached scale is what made the host allocate a container of the wrong size.
    *width  = gui->width;
    *height = gui->height;
    return true;
}

bool gui_can_resize(const clap_plugin_t*) { return true; }

bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    // The UI locks the 1080:640 design ratio via setGeometryConstraints(), so tell the host the same.
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = true;
    hints->aspect_ratio_width      = DISTRHO_UI_DEFAULT_WIDTH;
    hints->aspect_ratio_height     = DISTRHO_UI_DEFAULT_HEIGHT;
    return true;
}

bool gui_adjust_size(const clap_plugin_t*, uint32_t*, uint32_t*) { return true; }   // accept as-is

bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    gui->width  = width;
    gui->height = height;
    applySize(gui);
    return true;
}

bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    if (gui == nullptr || std::strcmp(window->api, kWindowApi) != 0) return false;

    // cocoa is a void*, x11 is already an integer handle — hence the different casts.
#ifdef __APPLE__
    gui->parentWindow = reinterpret_cast<uintptr_t>(window->cocoa);
#else
    gui->parentWindow = static_cast<uintptr_t>(window->x11);
#endif

    if (!gui->exporter) {
        gui->exporter.reset(new UIExporter(
            const_cast<clap_plugin_t*>(plugin),   // callbacksPtr → the edit/set callbacks above
            gui->parentWindow,                    // winId: the host's embed target
            DEFAULT_SAMPLE_RATE,                  // the UI does not need the real rate
            onEditParam, onSetParam,
            nullptr, nullptr, nullptr, nullptr,   // state / note / size / file callbacks: unused
            nullptr,                              // bundlePath
            const_cast<clap_plugin_t*>(plugin),   // dspPtr → UI::getPluginInstancePointer()
            0.0,                                  // scaleFactor: 0 = auto
            // kTypeClassic, not kTypeAuto: matches DPF's own CLAP wrapper for this embedded case.
            DGL_NAMESPACE::Application::kTypeClassic));
        // The UIExporter starts at DISTRHO_UI_DEFAULT_WIDTH x HEIGHT *pixels*; put it at the host-unit
        // size instead, at the scale of the window it is actually in.
        applySize(gui);
    }
    // Here as well as in show(): clap-wrapper's AUv2 view never calls show() unless its local patch
    // (patches/clap-wrapper--auv2-gui-show.patch) is applied, and without idle Load / Save get no file.
    startIdleTimer(gui);
    return true;
}

bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }   // embedded only

void gui_suggest_title(const clap_plugin_t*, const char*) {}

bool gui_show(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;

    startIdleTimer(gui);
    return true;
}

bool gui_hide(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;

    stopIdleTimer(gui);
    return true;
}

const clap_plugin_gui_t kGui = {
    .is_api_supported  = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create            = gui_create,
    .destroy           = gui_destroy,
    .set_scale         = gui_set_scale,
    .get_size          = gui_get_size,
    .can_resize        = gui_can_resize,
    .get_resize_hints  = gui_get_resize_hints,
    .adjust_size       = gui_adjust_size,
    .set_size          = gui_set_size,
    .set_parent        = gui_set_parent,
    .set_transient     = gui_set_transient,
    .suggest_title     = gui_suggest_title,
    .show              = gui_show,
    .hide              = gui_hide,
};

} // namespace

extern "C" const clap_plugin_gui_t* ss_gui_extension(void) { return &kGui; }

// Host automation → an open editor. Called on the main thread by plugin.cpp.
extern "C" void ss_gui_push_param(void* guiPtr, uint32_t index, double value)
{
    auto* gui = static_cast<GuiState*>(guiPtr);
    if (gui != nullptr && gui->exporter)
        gui->exporter->parameterChanged(index, float(value));
}

// CLAP timer-support → pump the DPF/pugl event loop (see this file's header comment).
extern "C" void ss_gui_on_timer(const clap_plugin_t* plugin, clap_id)
{
    auto* gui = static_cast<GuiState*>(ss_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return;
    // The host window moved to a screen with a different backing scale (or the size was set before
    // it was in a window): re-derive the pixel size so drawing and mouse agree again.
    if (liveScale(gui) != gui->appliedScale) applySize(gui);
    gui->exporter->plugin_idle();
}

END_NAMESPACE_DISTRHO
