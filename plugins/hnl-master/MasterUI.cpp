// HNL Master: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <vector>

#include "DistrhoUI.hpp"

#include "Params.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

const uint32_t kBg = 0x14161a, kPanel = 0x1d2027, kPanelEdge = 0x2a2e38, kTrack = 0x30353f;
const uint32_t kText = 0xe6e9ef, kDim = 0x8a93a3;
const uint32_t kAmber = 0xf2a33a, kTeal = 0x37c8b4, kRed = 0xef5b5b, kBlue = 0x5b9cf2;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d;

enum Section { kSecInput, kSecComp, kSecExc, kSecLim };

struct Control {
    uint32_t id;
    float x, y, r; // knob centre and radius, or toggle centre (r unused)
    Section section;
    const char* label;
    bool toggle;
};

struct Panel {
    float x, y, w, h;
    const char* title;
    uint32_t accent;
};

const Panel kPanelInput = {12, 52, 84, 496, "INPUT", kBlue};
const Panel kPanelComp = {104, 52, 486, 496, "MULTIBAND COMPRESSOR", kAmber};
const Panel kPanelExc = {598, 52, 180, 210, "EXCITER", kTeal};
const Panel kPanelLim = {598, 270, 180, 278, "LIMITER", kRed};
const Panel kPanelOut = {786, 52, 182, 496, "OUTPUT", kBlue};

const float kBandWidth = 162.0f;
const float kResetX = 806, kResetY = 500, kResetW = 142, kResetH = 28;

} // namespace

class MasterUI : public UI {
public:
    MasterUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, true) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();

        add(kInputGain, 54, 500, 19, kSecInput, "GAIN");

        addToggle(kCompOn, kPanelComp, kSecComp);
        add(kXoverLow, kPanelComp.x + kBandWidth, 116, 15, kSecComp, "LOW X-OVER");
        add(kXoverHigh, kPanelComp.x + 2 * kBandWidth, 116, 15, kSecComp, "HIGH X-OVER");
        for (int b = 0; b < 3; ++b) {
            const float x = kPanelComp.x + b * kBandWidth, k1 = x + 40, k2 = x + 100;
            const uint32_t base = kLowThreshold + 5 * b;
            add(base + 0, k1, 234, 20, kSecComp, "THRESHOLD");
            add(base + 1, k2, 234, 20, kSecComp, "RATIO");
            add(base + 2, k1, 346, 20, kSecComp, "ATTACK");
            add(base + 3, k2, 346, 20, kSecComp, "RELEASE");
            add(base + 4, k1, 458, 20, kSecComp, "GAIN");
        }

        addToggle(kExcOn, kPanelExc, kSecExc);
        add(kExcFreq, kPanelExc.x + 48, 124, 18, kSecExc, "FREQUENCY");
        add(kExcDrive, kPanelExc.x + 132, 124, 18, kSecExc, "DRIVE");
        add(kExcColour, kPanelExc.x + 48, 208, 18, kSecExc, "COLOUR");
        add(kExcMix, kPanelExc.x + 132, 208, 18, kSecExc, "MIX");

        addToggle(kLimOn, kPanelLim, kSecLim);
        add(kLimGain, kPanelLim.x + 42, 362, 20, kSecLim, "GAIN");
        add(kLimCeiling, kPanelLim.x + 108, 362, 20, kSecLim, "CEILING");
        add(kLimRelease, kPanelLim.x + 42, 476, 20, kSecLim, "RELEASE");
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        repaint();
    }

    void stateChanged(const char*, const char*) override {}

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = static_cast<float>(getScaleFactor());
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        beginPath();
        rect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
        fillColor(rgb(kBg));
        fill();

        label("HNL MASTER", 16, 27, 18, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("multiband compressor  /  exciter  /  true-peak limiter", 150, 28, 11, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);

        drawPanel(kPanelInput, true);
        drawPanel(kPanelComp, on(kCompOn));
        drawPanel(kPanelExc, on(kExcOn));
        drawPanel(kPanelLim, on(kLimOn));
        drawPanel(kPanelOut, true);

        // Input: level meters.
        levelMeter(34, 92, 12, 360, values_[kMeterInL]);
        levelMeter(50, 92, 12, 360, values_[kMeterInR]);
        levelScale(66, 92, 360);

        // Compressor: band titles, separators and gain-reduction meters.
        static const char* const kBandNames[3] = {"LOW", "MID", "HIGH"};
        const float compAlpha = on(kCompOn) ? 1.0f : 0.35f;
        for (int b = 0; b < 3; ++b) {
            const float x = kPanelComp.x + b * kBandWidth;
            label(kBandNames[b], x + kBandWidth * 0.5f, 172, 12, rgb(kAmber, compAlpha), ALIGN_CENTER | ALIGN_MIDDLE);
            if (b > 0) {
                beginPath();
                moveTo(x, 160);
                lineTo(x, 536);
                strokeColor(rgb(kPanelEdge));
                strokeWidth(1);
                stroke();
            }
            const float gr = values_[kMeterGrLow + b];
            reductionMeter(x + 136, 204, 10, 320, gr, 20.0f, rgb(kAmber, compAlpha));
            label("GR", x + 100, 446, 9, rgb(kDim, compAlpha), ALIGN_CENTER | ALIGN_MIDDLE);
            char text[24];
            std::snprintf(text, sizeof(text), "%.1f dB", gr);
            label(text, x + 100, 464, 12, rgb(kText, compAlpha), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Limiter: gain-reduction meter and readout.
        {
            const float alpha = on(kLimOn) ? 1.0f : 0.35f, gr = values_[kMeterGrLim];
            reductionMeter(kPanelLim.x + 152, 312, 10, 222, gr, 12.0f, rgb(kRed, alpha));
            label("GR", kPanelLim.x + 108, 464, 9, rgb(kDim, alpha), ALIGN_CENTER | ALIGN_MIDDLE);
            char text[24];
            std::snprintf(text, sizeof(text), "%.1f dB", gr);
            label(text, kPanelLim.x + 108, 482, 12, rgb(kText, alpha), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Output: level meters, loudness and true peak.
        levelMeter(806, 92, 14, 390, values_[kMeterOutL]);
        levelMeter(824, 92, 14, 390, values_[kMeterOutR]);
        levelScale(842, 92, 390);
        readout("MOMENTARY", values_[kMeterLufsM], "LUFS", 918, 104, 15);
        readout("SHORT-TERM", values_[kMeterLufsS], "LUFS", 918, 174, 15);
        readout("INTEGRATED", values_[kMeterLufsI], "LUFS", 918, 244, 20);
        readout("TRUE PEAK MAX", values_[kMeterTruePeak], "dBTP", 918, 330, 15,
                values_[kMeterTruePeak] > values_[kLimCeiling] + 0.05f && on(kLimOn) ? kRed : kText);

        beginPath();
        roundedRect(kResetX, kResetY, kResetW, kResetH, 4);
        fillColor(rgb(kTrack));
        fill();
        label("RESET METERS", kResetX + kResetW * 0.5f, kResetY + kResetH * 0.5f, 10, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);

        for (const Control& c : controls_) {
            if (c.toggle) drawToggle(c);
            else drawKnob(c);
        }
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

        if (x >= kResetX && x <= kResetX + kResetW && y >= kResetY && y <= kResetY + kResetH) {
            setState("reset", "");
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
        if (drag_ < 0) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY()) / s;
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        dragNorm_ += (dragY_ - y) / range;
        dragNorm_ = dragNorm_ < 0.0f ? 0.0f : (dragNorm_ > 1.0f ? 1.0f : dragNorm_);
        dragY_ = y;
        change(controls_[drag_].id, normToParam(controls_[drag_].id, dragNorm_), false);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        const float s = static_cast<float>(getScaleFactor());
        const int hit = controlAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0 || controls_[hit].toggle || drag_ >= 0) return false;
        const uint32_t id = controls_[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        const float norm = paramToNorm(id, values_[id]) + step * static_cast<float>(ev.delta.getY());
        change(id, normToParam(id, norm), true);
        return true;
    }

private:
    void add(uint32_t id, float x, float y, float r, Section section, const char* text) {
        controls_.push_back({id, x, y, r, section, text, false});
    }
    void addToggle(uint32_t id, const Panel& p, Section section) {
        controls_.push_back({id, p.x + p.w - 28, p.y + 14, 0, section, "", true});
    }

    bool on(uint32_t id) const { return values_[id] > 0.5f; }
    bool sectionOn(Section s) const {
        return s == kSecComp ? on(kCompOn) : s == kSecExc ? on(kExcOn) : s == kSecLim ? on(kLimOn) : true;
    }
    static uint32_t accentOf(Section s) {
        return s == kSecComp ? kAmber : s == kSecExc ? kTeal : s == kSecLim ? kRed : kBlue;
    }

    int controlAt(float x, float y) const {
        for (size_t i = 0; i < controls_.size(); ++i) {
            const Control& c = controls_[i];
            if (c.toggle) {
                if (std::fabs(x - c.x) <= 20 && std::fabs(y - c.y) <= 12) return static_cast<int>(i);
            } else if (std::hypot(x - c.x, y - c.y) <= c.r + 8) {
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
        repaint();
    }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }

    void drawPanel(const Panel& p, bool active) {
        beginPath();
        roundedRect(p.x, p.y, p.w, p.h, 6);
        fillColor(rgb(kPanel));
        fill();
        strokeColor(rgb(kPanelEdge));
        strokeWidth(1);
        stroke();
        beginPath();
        roundedRect(p.x + 10, p.y + 11, 3, 8, 1.5f);
        fillColor(rgb(p.accent, active ? 1.0f : 0.35f));
        fill();
        label(p.title, p.x + 20, p.y + 15, 10, rgb(active ? kText : kDim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawToggle(const Control& c) {
        const bool active = on(c.id);
        beginPath();
        roundedRect(c.x - 14, c.y - 7, 28, 14, 7);
        fillColor(active ? rgb(accentOf(c.section)) : rgb(kTrack));
        fill();
        beginPath();
        circle(active ? c.x + 7 : c.x - 7, c.y, 5);
        fillColor(rgb(active ? kBg : kDim));
        fill();
    }

    void drawKnob(const Control& c) {
        const ParamInfo& p = kParams[c.id];
        const float alpha = sectionOn(c.section) ? 1.0f : 0.35f;
        const float a0 = 0.75f * 3.14159265f, a1 = 2.25f * 3.14159265f;
        const float norm = paramToNorm(c.id, values_[c.id]);
        const float angle = a0 + norm * (a1 - a0);
        // Parameters centred on zero grow from the middle.
        const bool bipolar = p.kind == kLinear && p.min < 0.0f && p.max > 0.0f;
        const float from = bipolar ? a0 + paramToNorm(c.id, 0.0f) * (a1 - a0) : a0;

        beginPath();
        arc(c.x, c.y, c.r, a0, a1, CW);
        strokeColor(rgb(kTrack));
        strokeWidth(3.5f);
        lineCap(ROUND);
        stroke();

        if (std::fabs(angle - from) > 0.01f) {
            beginPath();
            arc(c.x, c.y, c.r, std::min(from, angle), std::max(from, angle), CW);
            strokeColor(rgb(accentOf(c.section), alpha));
            strokeWidth(3.5f);
            stroke();
        }

        beginPath();
        circle(c.x, c.y, c.r - 6);
        fillColor(rgb(0x262a33));
        fill();
        beginPath();
        moveTo(c.x + std::cos(angle) * (c.r - 13), c.y + std::sin(angle) * (c.r - 13));
        lineTo(c.x + std::cos(angle) * (c.r - 7), c.y + std::sin(angle) * (c.r - 7));
        strokeColor(rgb(kText, alpha));
        strokeWidth(2);
        stroke();

        label(c.label, c.x, c.y - c.r - 12, 9, rgb(kDim, alpha), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        formatValue(c.id, values_[c.id], text, sizeof(text));
        label(text, c.x, c.y + c.r + 13, 11, rgb(kText, alpha), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    static void formatValue(uint32_t id, float v, char* out, size_t size) {
        const char* unit = kParams[id].unit;
        if (unit[0] == 'H') {
            if (v >= 1000.0f) std::snprintf(out, size, "%.2f kHz", v / 1000.0f);
            else std::snprintf(out, size, "%.0f Hz", v);
        } else if (unit[0] == 'd') {
            std::snprintf(out, size, "%+.1f dB", v);
        } else if (unit[0] == 'm') {
            std::snprintf(out, size, v < 10.0f ? "%.1f ms" : "%.0f ms", v);
        } else if (unit[0] == ':') {
            std::snprintf(out, size, "%.1f:1", v);
        } else {
            std::snprintf(out, size, "%.0f %%", v);
        }
    }

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void levelMeter(float x, float y, float w, float h, float db) {
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(0x0e1013));
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

    /** Bar that grows downward with gain reduction (db <= 0). */
    void reductionMeter(float x, float y, float w, float h, float db, float range, const Color& colour) {
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(0x0e1013));
        fill();
        const float t = std::min(1.0f, -db / range);
        if (t <= 0.0f) return;
        beginPath();
        rect(x, y, w, h * t);
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
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterUI)
};

UI* createUI() { return new MasterUI(); }

END_NAMESPACE_DISTRHO
