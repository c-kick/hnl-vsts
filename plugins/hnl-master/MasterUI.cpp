// HNL Master: user interface (NanoVG).
//
// The window is a row of modules. A stage that is switched off collapses to
// a title bar, and the window shrinks to fit. Coordinates are in design
// pixels (kHeight high, width from layout()), scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Widgets.hpp"
#include "Help.hpp"
#include "Params.hpp"
#include "Presets.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

const uint32_t kBg = 0x14161a, kPanel = 0x1d2027, kPanelEdge = 0x2a2e38, kTrack = 0x30353f, kWell = 0x0e1013;
const uint32_t kText = 0xe6e9ef, kDim = 0x8a93a3;
const uint32_t kAmber = 0xf2a33a, kTeal = 0x37c8b4, kRed = 0xef5b5b, kBlue = 0x5b9cf2;
const uint32_t kViolet = 0xa98bf5, kSky = 0x5bc0f2, kRose = 0xf27ba8;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d;

// In signal order. Input and output are always open.
enum Module { kModInput, kModComp, kModEq, kModExc, kModWide, kModRev, kModLim, kModOutput, kModCount };

struct ModuleInfo {
    const char* title;
    const char* shortTitle; // used when collapsed
    uint32_t accent;
    uint32_t toggle; // on/off parameter, or kParamCount for none
    float height;    // when open, for the modules stacked in a column
};

const float kHeight = UI_BASE_HEIGHT, kTop = 52, kBottom = 548, kMargin = 12, kGap = 8;
const float kInputW = 84, kCompW = 486, kColumnW = 212, kOutputW = 182, kBarH = 30;

const ModuleInfo kModules[kModCount] = {
    {"INPUT", "INPUT", kBlue, kParamCount, 0},
    {"MULTIBAND COMPRESSOR", "COMPRESSOR", kAmber, kCompOn, 0},
    {"EQUALISER", "EQUALISER", kViolet, kEqOn, 196},
    {"EXCITER", "EXCITER", kTeal, kExcOn, 148},
    {"STEREO WIDTH", "STEREO WIDTH", kSky, kWideOn, 114},
    {"REVERB", "REVERB", kRose, kRevOn, 114},
    {"LIMITER", "LIMITER", kRed, kLimOn, 140},
    {"OUTPUT", "OUTPUT", kBlue, kParamCount, 0},
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

struct Control {
    uint32_t id;
    Module module;
    float dx, dy, r; // knob centre and radius, relative to the module
    const char* label;
    bool toggle;
};

const float kBandWidth = 162.0f;
const float kPresetRight = 268, kHeaderY = 14; // the preset bar starts this far from the right edge, which moves as modules open and close
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x262a33, 0x3a404c, kText, kDim, kAmber};
const Rect kModeBox = {12, 36, 188, 22}; // exciter character, relative to its module
const Rect kResetBox = {20, 448, 142, 28};

} // namespace

class MasterUI : public UI {
public:
    MasterUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();

        knob(kInputGain, kModInput, 42, 448, 19, "GAIN");

        knob(kXoverLow, kModComp, kBandWidth, 64, 15, "LOW X-OVER");
        knob(kXoverHigh, kModComp, 2 * kBandWidth, 64, 15, "HIGH X-OVER");
        for (int b = 0; b < 3; ++b) {
            const float k1 = b * kBandWidth + 40, k2 = b * kBandWidth + 100;
            const uint32_t base = kLowThreshold + 5 * b;
            knob(base + 0, kModComp, k1, 182, 20, "THRESHOLD");
            knob(base + 1, kModComp, k2, 182, 20, "RATIO");
            knob(base + 2, kModComp, k1, 294, 20, "ATTACK");
            knob(base + 3, kModComp, k2, 294, 20, "RELEASE");
            knob(base + 4, kModComp, k1, 406, 20, "GAIN");
        }

        const float four[4] = {31, 81, 131, 181}, three[3] = {46, 106, 166};
        knob(kEqLowFreq, kModEq, four[0], 72, 16, "LOW FREQ");
        knob(kEqLowGain, kModEq, four[1], 72, 16, "LOW GAIN");
        knob(kEqHighFreq, kModEq, four[2], 72, 16, "HIGH FREQ");
        knob(kEqHighGain, kModEq, four[3], 72, 16, "HIGH GAIN");
        knob(kEqMidFreq, kModEq, three[0], 154, 16, "PEAK FREQ");
        knob(kEqMidGain, kModEq, three[1], 154, 16, "PEAK GAIN");
        knob(kEqMidQ, kModEq, three[2], 154, 16, "PEAK Q");

        knob(kExcFreq, kModExc, four[0], 106, 16, "FREQ");
        knob(kExcDrive, kModExc, four[1], 106, 16, "DRIVE");
        knob(kExcColour, kModExc, four[2], 106, 16, "COLOUR");
        knob(kExcMix, kModExc, four[3], 106, 16, "MIX");

        knob(kWideWidth, kModWide, 66, 72, 16, "WIDTH");
        knob(kWideFreq, kModWide, 146, 72, 16, "ABOVE");

        knob(kRevDecay, kModRev, three[0], 72, 16, "DECAY");
        knob(kRevDamping, kModRev, three[1], 72, 16, "DAMPING");
        knob(kRevMix, kModRev, three[2], 72, 16, "AMOUNT");

        knob(kLimGain, kModLim, three[0], 72, 16, "GAIN");
        knob(kLimCeiling, kModLim, three[1], 72, 16, "CEILING");
        knob(kLimRelease, kModLim, three[2], 72, 16, "RELEASE");

        for (int m = 0; m < kModCount; ++m)
            if (kModules[m].toggle != kParamCount)
                controls_.push_back({kModules[m].toggle, static_cast<Module>(m), 0, 15, 0, "", true});

        layout();
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        if (kParams[index].kind == kToggle) layout();
        repaint();
    }

    void stateChanged(const char* key, const char* value) override {
        if (std::strcmp(key, "preset") != 0) return;
        presetName_ = value;
        list_.rescan();
        preset_ = list_.find(presetName_);
        repaint();
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = static_cast<float>(getScaleFactor());
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        beginPath();
        rect(0, 0, std::max(width_, getWidth() / s), std::max(kHeight, getHeight() / s));
        fillColor(rgb(kBg));
        fill();

        hnl::ui::title(*this, "MASTER", rgb(kText));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (int m = 0; m < kModCount; ++m) drawModule(static_cast<Module>(m));

        drawInput(rects_[kModInput]);
        if (open(kModComp)) drawCompressor(rects_[kModComp]);
        if (open(kModExc)) drawExciterMode(rects_[kModExc]);
        if (open(kModLim)) drawLimiter(rects_[kModLim]);
        drawOutput(rects_[kModOutput]);

        for (const Control& c : controls_) {
            if (c.toggle) drawToggle(c);
            else if (open(c.module)) drawKnob(c);
        }

        presetBar_.drawMenu(*this, kTheme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        if (!presetBar_.isOpen() && drag_ < 0) help_.drawTip(*this, kTheme, width_, kHeight);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            editParameter(controls_[drag_].id, false);
            drag_ = -1;
            return true;
        }

        presetNote_.clear();
        if (!presetBar_.isBusy() && presetBar_.nameBox().contains(x, y)) { // another window may have saved or deleted one
            list_.rescan();
            preset_ = list_.find(presetName_);
        }
        const int picked = presetBar_.mouseDown(x, y, preset_, list_.count(), list_.factoryCount(), canSavePreset());
        if (picked != hnl::ui::PresetBar::kNotMine) {
            help_.hide();
            presetAction(picked);
            repaint();
            return true;
        }
        if (help_.buttonHit(x, y)) {
            help_.on = !help_.on;
            help_.motion(x, y);
            repaint();
            return true;
        }

        if (inModule(kModOutput, kResetBox, x, y)) {
            setState("reset", "");
            return true;
        }
        if (open(kModExc) && inModule(kModExc, kModeBox, x, y)) {
            const int mode = static_cast<int>((x - rects_[kModExc].x - kModeBox.x) / (kModeBox.w / 3.0f));
            change(kExcMode, static_cast<float>(mode < 0 ? 0 : (mode > 2 ? 2 : mode)), true);
            return true;
        }

        const int hit = controlAt(x, y);
        if (hit < 0) return false;
        const Control& c = controls_[hit];

        if (c.toggle) {
            change(c.id, values_[c.id] > 0.5f ? 0.0f : 1.0f, true);
            return true;
        }
        const bool doubleClick = hit == lastClick_ && ev.time - lastClickTime_ < 350;
        lastClick_ = hit;
        lastClickTime_ = ev.time;
        if (doubleClick || (ev.mod & kModifierControl)) {
            change(c.id, kParams[c.id].def, true);
            return true;
        }
        drag_ = hit;
        dragY_ = y;
        dragNorm_ = paramToNorm(c.id, values_[c.id]);
        editParameter(c.id, true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override {
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (presetBar_.isOpen()) {
            if (presetBar_.motion(x, y, list_.count())) repaint();
            return true;
        }
        if (drag_ < 0) {
            if (help_.motion(x, y)) repaint();
            return false;
        }
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        dragNorm_ += (dragY_ - y) / range;
        dragNorm_ = dragNorm_ < 0.0f ? 0.0f : (dragNorm_ > 1.0f ? 1.0f : dragNorm_);
        dragY_ = y;
        change(controls_[drag_].id, normToParam(controls_[drag_].id, dragNorm_), false);
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override {
        if (!ev.press) return presetBar_.isEditing();
        const int result = presetBar_.key(ev.key);
        if (result == hnl::ui::PresetBar::kNotMine) return false;
        presetAction(result);
        repaint();
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override {
        if (!presetBar_.character(ev.string)) return false;
        repaint();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (presetBar_.isBusy() || drag_ >= 0) return false;
        const float s = static_cast<float>(getScaleFactor());
        const int hit = controlAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0 || controls_[hit].toggle) return false;
        const uint32_t id = controls_[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        const float norm = paramToNorm(id, values_[id]) + step * static_cast<float>(ev.delta.getY());
        change(id, normToParam(id, norm), true);
        return true;
    }

private:
    void knob(uint32_t id, Module module, float dx, float dy, float r, const char* text) {
        controls_.push_back({id, module, dx, dy, r, text, false});
    }

    bool on(uint32_t id) const { return values_[id] > 0.5f; }
    /** A module is open when it has no on/off switch or is switched on. */
    bool open(Module m) const { return kModules[m].toggle == kParamCount || on(kModules[m].toggle); }

    // ------------------------------------------------------------- layout

    /** Places the modules and resizes the window to fit them. */
    void layout() {
        float x = kMargin;
        rects_[kModInput] = {x, kTop, kInputW, kBottom - kTop};
        x += kInputW + kGap;
        if (open(kModComp)) {
            rects_[kModComp] = {x, kTop, kCompW, kBottom - kTop};
            x += kCompW + kGap;
        }
        // Everything else stacks in columns; a collapsed compressor joins them.
        float y = kTop;
        for (int i = kModComp; i <= kModLim; ++i) {
            const Module m = static_cast<Module>(i);
            if (m == kModComp && open(m)) continue;
            const float h = open(m) ? kModules[m].height : kBarH;
            if (y + h > kBottom && y > kTop) {
                x += kColumnW + kGap;
                y = kTop;
            }
            rects_[m] = {x, y, kColumnW, h};
            y += h + kGap;
        }
        x += kColumnW + kGap;
        rects_[kModOutput] = {x, kTop, kOutputW, kBottom - kTop};
        width_ = x + kOutputW + kMargin;

        const double s = getScaleFactor();
        const uint w = static_cast<uint>(std::lround(width_ * s)), h = static_cast<uint>(std::lround(kHeight * s));
        if (getWidth() != w || getHeight() != h) setSize(w, h);
        presetBar_.moveTo(width_ - kPresetRight, width_);
        help_.setButton(width_ - 24, kHeaderY + 12);
        placeHelp();
    }

    /** Tells the help tips where everything is. Wide areas first, single controls on top. */
    void placeHelp() {
        using hnl::ui::Box;
        help_.clear();
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        const Rect& in = rects_[kModInput];
        help_.add({in.x + 16, in.y + 34, 56, 372}, "INPUT LEVEL", help::kInputMeter);
        const Rect& out = rects_[kModOutput];
        help_.add({out.x + 14, out.y + 34, 60, 402}, "OUTPUT LEVEL", help::kOutputMeter);
        help_.add({out.x + 84, out.y + 40, 96, 210}, "LOUDNESS", help::kLoudness);
        help_.add({out.x + 84, out.y + 264, 96, 64}, "TRUE PEAK", help::kTruePeak);
        help_.add({out.x + kResetBox.x, out.y + kResetBox.y, kResetBox.w, kResetBox.h}, "RESET METERS", help::kReset);
        if (open(kModComp)) {
            const Rect& p = rects_[kModComp];
            for (int b = 0; b < 3; ++b) help_.add({p.x + b * kBandWidth + 70, p.y + 380, 86, 104}, "GAIN REDUCTION", help::kGainReduction);
        }
        if (open(kModLim)) help_.add({rects_[kModLim].x + 8, rects_[kModLim].y + 110, kColumnW - 16, 24}, "GAIN REDUCTION", help::kGainReduction);
        if (open(kModExc)) {
            const Rect& p = rects_[kModExc];
            help_.add({p.x + kModeBox.x, p.y + kModeBox.y, kModeBox.w, kModeBox.h}, "EXCITER MODE", help::forParam(kExcMode));
        }
        for (const Control& c : controls_) {
            if (c.toggle) help_.add({cx(c) - 16, cy(c) - 9, 32, 18}, kParams[c.id].name, help::forParam(c.id));
            else if (open(c.module)) help_.add({cx(c) - c.r - 12, cy(c) - c.r - 18, 2 * c.r + 24, 2 * c.r + 38}, kParams[c.id].name, help::forParam(c.id));
        }
    }

    bool inModule(Module m, const Rect& box, float x, float y) const {
        return box.contains(x - rects_[m].x, y - rects_[m].y);
    }

    float cx(const Control& c) const { return rects_[c.module].x + (c.toggle ? rects_[c.module].w - 28 : c.dx); }
    float cy(const Control& c) const { return rects_[c.module].y + c.dy; }

    int controlAt(float x, float y) const {
        for (size_t i = 0; i < controls_.size(); ++i) {
            const Control& c = controls_[i];
            if (c.toggle) {
                if (std::fabs(x - cx(c)) <= 20 && std::fabs(y - cy(c)) <= 12) return static_cast<int>(i);
            } else if (open(c.module) && std::hypot(x - cx(c), y - cy(c)) <= c.r + 8) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /** Applies a value locally and sends it to the host. */
    void change(uint32_t id, float value, bool gesture) {
        if (gesture) editParameter(id, true);
        values_[id] = value;
        setParameterValue(id, value);
        if (gesture) editParameter(id, false);
        if (kParams[id].kind == kToggle) layout();
        repaint();
    }

    // ------------------------------------------------------------ presets

    bool canSavePreset() const { return list_.canSave() && !list_.full(); }

    /** What the preset bar asks for: load a preset, start typing a name, save under it, or delete one. */
    void presetAction(int picked) {
        typedef hnl::ui::PresetBar Bar;
        if (picked >= 0) {
            applyPreset(picked);
        } else if (picked == Bar::kEdit) {
            presetBar_.startEdit(list_.suggestName(preset_));
            getWindow().focus(); // so that the typing comes to this window
        } else if (picked == Bar::kSave) {
            const int saved = list_.save(presetBar_.text(), values_);
            if (saved < 0) {
                presetNote_ = "Could not save";
                return;
            }
            preset_ = saved;
            presetName_ = list_.name(saved);
            setState("preset", presetName_.c_str());
        } else if (picked == Bar::kDelete) {
            if (!list_.remove(presetBar_.target())) presetNote_ = "Could not delete";
            preset_ = list_.find(presetName_);
        }
    }

    void applyPreset(int index) {
        preset_ = index;
        presetName_ = list_.name(index);
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float v = list_.value(index, id);
            if (values_[id] == v) continue;
            editParameter(id, true);
            values_[id] = v;
            setParameterValue(id, v);
            editParameter(id, false);
        }
        setState("preset", presetName_.c_str());
        layout();
        repaint();
    }

    /** True when a control was moved since the preset was chosen. */
    bool presetModified() const {
        if (preset_ < 0) return false;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float tolerance = 1e-4f * (kParams[id].max - kParams[id].min);
            if (std::fabs(values_[id] - list_.value(preset_, id)) > tolerance) return true;
        }
        return false;
    }

    // ------------------------------------------------------------ modules

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }

    void drawModule(Module m) {
        const Rect& p = rects_[m];
        const ModuleInfo& info = kModules[m];
        const bool active = open(m);
        beginPath();
        roundedRect(p.x, p.y, p.w, p.h, 6);
        fillColor(rgb(kPanel));
        fill();
        strokeColor(rgb(kPanelEdge));
        strokeWidth(1);
        stroke();
        beginPath();
        roundedRect(p.x + 10, p.y + 11, 3, 8, 1.5f);
        fillColor(rgb(info.accent, active ? 1.0f : 0.4f));
        fill();
        label(active ? info.title : info.shortTitle, p.x + 20, p.y + 15, 10, rgb(active ? kText : kDim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawInput(const Rect& p) {
        levelMeter(p.x + 22, p.y + 40, 12, 360, values_[kMeterInL]);
        levelMeter(p.x + 38, p.y + 40, 12, 360, values_[kMeterInR]);
        levelScale(p.x + 54, p.y + 40, 360);
    }

    void drawCompressor(const Rect& p) {
        static const char* const kBandNames[3] = {"LOW", "MID", "HIGH"};
        for (int b = 0; b < 3; ++b) {
            const float x = p.x + b * kBandWidth;
            label(kBandNames[b], x + kBandWidth * 0.5f, p.y + 120, 12, rgb(kAmber), ALIGN_CENTER | ALIGN_MIDDLE);
            if (b > 0) {
                beginPath();
                moveTo(x, p.y + 108);
                lineTo(x, p.y + 484);
                strokeColor(rgb(kPanelEdge));
                strokeWidth(1);
                stroke();
            }
            const float gr = values_[kMeterGrLow + b];
            reductionMeter(x + 136, p.y + 152, 10, 320, gr, 20.0f, rgb(kAmber), false);
            label("GR", x + 100, p.y + 394, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
            char text[24];
            std::snprintf(text, sizeof(text), "%.1f dB", gr);
            label(text, x + 100, p.y + 412, 12, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawExciterMode(const Rect& p) {
        const int mode = static_cast<int>(std::lround(values_[kExcMode]));
        const float w = kModeBox.w / 3.0f, x = p.x + kModeBox.x, y = p.y + kModeBox.y;
        beginPath();
        roundedRect(x, y, kModeBox.w, kModeBox.h, 4);
        fillColor(rgb(kTrack));
        fill();
        for (int i = 0; i < 3; ++i) {
            if (i == mode) {
                beginPath();
                roundedRect(x + i * w + 2, y + 2, w - 4, kModeBox.h - 4, 3);
                fillColor(rgb(kTeal));
                fill();
            }
            char text[8];
            std::snprintf(text, sizeof(text), "%s", kExciterModes[i]);
            for (char* c = text; *c; ++c) *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
            label(text, x + (i + 0.5f) * w, y + kModeBox.h * 0.5f, 9, rgb(i == mode ? kBg : kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawLimiter(const Rect& p) {
        const float gr = values_[kMeterGrLim];
        label("GR", p.x + 12, p.y + 122, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        reductionMeter(p.x + 32, p.y + 118, 108, 8, gr, 12.0f, rgb(kRed), true);
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", gr);
        label(text, p.x + p.w - 12, p.y + 122, 11, rgb(kText), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawOutput(const Rect& p) {
        levelMeter(p.x + 20, p.y + 40, 14, 390, values_[kMeterOutL]);
        levelMeter(p.x + 38, p.y + 40, 14, 390, values_[kMeterOutR]);
        levelScale(p.x + 56, p.y + 40, 390);
        const float x = p.x + 132;
        readout("MOMENTARY", values_[kMeterLufsM], "LUFS", x, p.y + 52, 15);
        readout("SHORT-TERM", values_[kMeterLufsS], "LUFS", x, p.y + 122, 15);
        readout("INTEGRATED", values_[kMeterLufsI], "LUFS", x, p.y + 192, 20);
        readout("TRUE PEAK MAX", values_[kMeterTruePeak], "dBTP", x, p.y + 278, 15,
                values_[kMeterTruePeak] > values_[kLimCeiling] + 0.05f && on(kLimOn) ? kRed : kText);

        beginPath();
        roundedRect(p.x + kResetBox.x, p.y + kResetBox.y, kResetBox.w, kResetBox.h, 4);
        fillColor(rgb(kTrack));
        fill();
        label("RESET METERS", p.x + kResetBox.x + kResetBox.w * 0.5f, p.y + kResetBox.y + kResetBox.h * 0.5f, 10, rgb(kText),
              ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    void drawToggle(const Control& c) {
        const bool active = on(c.id);
        const float x = cx(c), y = cy(c);
        beginPath();
        roundedRect(x - 14, y - 7, 28, 14, 7);
        fillColor(active ? rgb(kModules[c.module].accent) : rgb(kTrack));
        fill();
        beginPath();
        circle(active ? x + 7 : x - 7, y, 5);
        fillColor(rgb(active ? kBg : kDim));
        fill();
    }

    void drawKnob(const Control& c) {
        const ParamInfo& p = kParams[c.id];
        const float x = cx(c), y = cy(c);
        const float a0 = 0.75f * 3.14159265f, a1 = 2.25f * 3.14159265f;
        const float norm = paramToNorm(c.id, values_[c.id]);
        const float angle = a0 + norm * (a1 - a0);
        // Parameters with a neutral point in the middle grow from there.
        float from = a0;
        if (c.id == kWideWidth) from = a0 + paramToNorm(c.id, 100.0f) * (a1 - a0);
        else if (p.kind == kLinear && p.min < 0.0f && p.max > 0.0f) from = a0 + paramToNorm(c.id, 0.0f) * (a1 - a0);

        beginPath();
        arc(x, y, c.r, a0, a1, CW);
        strokeColor(rgb(kTrack));
        strokeWidth(3.5f);
        lineCap(ROUND);
        stroke();

        if (std::fabs(angle - from) > 0.01f) {
            beginPath();
            arc(x, y, c.r, std::min(from, angle), std::max(from, angle), CW);
            strokeColor(rgb(kModules[c.module].accent));
            strokeWidth(3.5f);
            stroke();
        }

        beginPath();
        circle(x, y, c.r - 6);
        fillColor(rgb(0x262a33));
        fill();
        beginPath();
        moveTo(x + std::cos(angle) * (c.r - 13), y + std::sin(angle) * (c.r - 13));
        lineTo(x + std::cos(angle) * (c.r - 7), y + std::sin(angle) * (c.r - 7));
        strokeColor(rgb(kText));
        strokeWidth(2);
        stroke();

        const bool small = c.r < 18;
        label(c.label, x, y - c.r - 12, small ? 8 : 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        formatValue(c.id, values_[c.id], text, sizeof(text));
        label(text, x, y + c.r + 13, small ? 10 : 11, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    static void formatValue(uint32_t id, float v, char* out, size_t size) {
        switch (kParams[id].unit[0]) {
        case 'H':
            if (v >= 9995.0f) std::snprintf(out, size, "%.1f kHz", v / 1000.0f);
            else if (v >= 1000.0f) std::snprintf(out, size, "%.2f kHz", v / 1000.0f);
            else std::snprintf(out, size, "%.0f Hz", v);
            break;
        case 'd': std::snprintf(out, size, "%+.1f dB", v); break;
        case 'm': std::snprintf(out, size, v < 10.0f ? "%.1f ms" : "%.0f ms", v); break;
        case ':': std::snprintf(out, size, "%.1f:1", v); break;
        case 's': std::snprintf(out, size, "%.2f s", v); break;
        case 'Q': std::snprintf(out, size, "%.2f", v); break;
        default: std::snprintf(out, size, "%.0f %%", v); break;
        }
    }

    // ------------------------------------------------------------- meters

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void levelMeter(float x, float y, float w, float h, float db) {
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(kWell));
        fill();
        const float top = levelPos(db);
        struct Zone { float from, to; uint32_t colour; };
        const Zone zones[3] = {{-48, -12, kGreen}, {-12, -3, kYellow}, {-3, 6, kRed}};
        for (const Zone& z : zones) {
            const float a = levelPos(z.from), b = std::min(levelPos(z.to), top);
            if (b <= a) continue;
            beginPath();
            rect(x, y + h * (1.0f - b), w, h * (b - a));
            fillColor(rgb(z.colour));
            fill();
        }
    }

    void levelScale(float x, float y, float h) {
        static const int kMarks[] = {6, 0, -6, -12, -24, -36, -48};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x, y + h * (1.0f - levelPos(static_cast<float>(db))), 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    /** Bar that grows with gain reduction (db <= 0): downward, or to the right. */
    void reductionMeter(float x, float y, float w, float h, float db, float range, const Color& colour, bool horizontal) {
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(kWell));
        fill();
        const float t = std::min(1.0f, -db / range);
        if (t <= 0.0f) return;
        beginPath();
        if (horizontal) rect(x, y, w * t, h);
        else rect(x, y, w, h * t);
        fillColor(colour);
        fill();
    }

    void readout(const char* title, float value, const char* unit, float x, float y, float size, uint32_t colour = kText) {
        label(title, x, y, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[24];
        if (value <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", value);
        label(text, x, y + 20, size, rgb(colour), ALIGN_CENTER | ALIGN_MIDDLE);
        label(unit, x, y + 38, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    std::vector<Control> controls_;
    Rect rects_[kModCount];
    float width_ = UI_BASE_WIDTH;
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{UI_BASE_WIDTH - kPresetRight, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterUI)
};

UI* createUI() { return new MasterUI(); }

END_NAMESPACE_DISTRHO
