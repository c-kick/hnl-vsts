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
const Rect kPrev = {150, 14, 22, 24}, kName = {176, 14, 170, 24}, kNext = {350, 14, 22, 24};
const float kMenuRow = 22;
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
        preset_ = -1;
        for (int i = 0; i < presets::kCount; ++i)
            if (presetName_ == presets::kAll[i].name) preset_ = i;
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

        label("HNL MASTER", 16, 27, 18, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        drawPresetBar();

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

        if (menuOpen_) drawPresetMenu();
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

        if (menuOpen_) {
            const int row = menuRowAt(x, y);
            menuOpen_ = false;
            if (row >= 0) applyPreset(row);
            repaint();
            return true;
        }
        if (kName.contains(x, y)) {
            menuOpen_ = true;
            hover_ = preset_;
            repaint();
            return true;
        }
        if (kPrev.contains(x, y) || kNext.contains(x, y)) {
            const int step = kNext.contains(x, y) ? 1 : -1;
            applyPreset(((preset_ < 0 ? 0 : preset_ + step) + presets::kCount) % presets::kCount);
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
        if (menuOpen_) {
            const int row = menuRowAt(x, y);
            if (row != hover_) { hover_ = row; repaint(); }
            return true;
        }
        if (drag_ < 0) return false;
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        dragNorm_ += (dragY_ - y) / range;
        dragNorm_ = dragNorm_ < 0.0f ? 0.0f : (dragNorm_ > 1.0f ? 1.0f : dragNorm_);
        dragY_ = y;
        change(controls_[drag_].id, normToParam(controls_[drag_].id, dragNorm_), false);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (menuOpen_ || drag_ >= 0) return false;
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

    void applyPreset(int index) {
        preset_ = index;
        presetName_ = presets::kAll[index].name;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float v = presets::value(index, id);
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
            if (std::fabs(values_[id] - presets::value(preset_, id)) > tolerance) return true;
        }
        return false;
    }

    Rect menuRect() const { return {kName.x, kName.y + kName.h + 2, kName.w, presets::kCount * kMenuRow + 8}; }

    int menuRowAt(float x, float y) const {
        const Rect m = menuRect();
        if (!m.contains(x, y)) return -1;
        const int row = static_cast<int>((y - m.y - 4) / kMenuRow);
        return row < 0 || row >= presets::kCount ? -1 : row;
    }

    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& colour) {
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        lineTo(x2, y2);
        closePath();
        fillColor(colour);
        fill();
    }

    void drawPresetBar() {
        for (const Rect* r : {&kPrev, &kName, &kNext}) {
            beginPath();
            roundedRect(r->x, r->y, r->w, r->h, 4);
            fillColor(rgb(r == &kName ? kPanel : kTrack));
            fill();
        }
        beginPath();
        roundedRect(kName.x, kName.y, kName.w, kName.h, 4);
        strokeColor(rgb(kPanelEdge));
        strokeWidth(1);
        stroke();

        const float my = kPrev.y + kPrev.h * 0.5f;
        triangle(kPrev.x + 14, my - 5, kPrev.x + 14, my + 5, kPrev.x + 8, my, rgb(kText));
        triangle(kNext.x + 8, my - 5, kNext.x + 8, my + 5, kNext.x + 14, my, rgb(kText));
        triangle(kName.x + kName.w - 18, my - 2, kName.x + kName.w - 10, my - 2, kName.x + kName.w - 14, my + 3, rgb(kDim));

        std::string text = presetName_;
        if (presetModified()) text += " *";
        label(text.c_str(), kName.x + 10, my, 11, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawPresetMenu() {
        const Rect m = menuRect();
        beginPath();
        roundedRect(m.x, m.y, m.w, m.h, 4);
        fillColor(rgb(0x262a33));
        fill();
        strokeColor(rgb(0x3a404c));
        strokeWidth(1);
        stroke();
        for (int i = 0; i < presets::kCount; ++i) {
            const float y = m.y + 4 + i * kMenuRow;
            if (i == hover_) {
                beginPath();
                roundedRect(m.x + 4, y, m.w - 8, kMenuRow, 3);
                fillColor(rgb(kTrack));
                fill();
            }
            label(presets::kAll[i].name, m.x + 12, y + kMenuRow * 0.5f, 11, rgb(i == preset_ ? kAmber : kText), ALIGN_LEFT | ALIGN_MIDDLE);
        }
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

    int preset_ = 0, hover_ = -1;
    std::string presetName_ = "Default";
    bool menuOpen_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterUI)
};

UI* createUI() { return new MasterUI(); }

END_NAMESPACE_DISTRHO
