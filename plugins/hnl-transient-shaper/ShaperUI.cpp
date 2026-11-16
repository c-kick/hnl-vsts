// HNL Transient Shaper: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
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

const uint32_t kBg = 0x181615, kPanel = 0x22201e, kPanelEdge = 0x35302d, kTrack = 0x403a36, kWell = 0x0f0d0c;
const uint32_t kText = 0xece8e4, kDim = 0x988f88, kAccent = 0xf0855a;
const uint32_t kFace = 0xe6e2d6, kFaceEdge = 0xb3ad9c, kInk = 0x22252a, kFaceRed = 0xb83a2b;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;
const float kMeterSpan = 12.0f; // dB of gain change at either end of the scale

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kMeterPanel = {12, 52, 300, 170}, kLevelPanel = {320, 52, 488, 170};
const Rect kTimePanel = {12, 230, 796, 70}, kKnobPanel = {12, 308, 796, 120};
const Rect kFace_ = {24, 68, 276, 138};

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kAttack, 92, 372, 26, "ATTACK"}, {kSustain, 251, 372, 26, "SUSTAIN"}, {kLength, 410, 372, 26, "LENGTH"},
    {kOutput, 569, 372, 26, "OUTPUT"}, {kMix, 728, 372, 26, "MIX"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; };
const Selector kSelectors[] = {
    {kFocus, {24, 262, 360, 26}, "DETECTOR"},
};

const float kPresetX = 266, kHeaderY = 14;
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x2c2826, 0x4a433e, kText, kDim, kAccent};

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class ShaperUI : public UI {
public:
    ShaperUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(UI_BASE_WIDTH - 24, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kFace_.x, kFace_.y, kFace_.w, kFace_.h}, "GAIN CHANGE", help::kGain);
        help_.add({kLevelPanel.x, kLevelPanel.y, kLevelPanel.w, kLevelPanel.h}, "LEVELS", help::kLevels);
        for (const Selector& sel : kSelectors)
            help_.add({sel.box.x, sel.box.y - 20, sel.box.w, sel.box.h + 20}, kParams[sel.id].name, help::forParam(sel.id));
        for (const Knob& k : kKnobs)
            help_.add({k.x - k.r - 12, k.y - k.r - 20, 2 * k.r + 24, 2 * k.r + 42}, kParams[k.id].name, help::forParam(k.id));
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
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
        rect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
        fillColor(rgb(kBg));
        fill();

        label("HNL TRANSIENT SHAPER", 16, 27, 18, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (const Rect* p : {&kMeterPanel, &kLevelPanel, &kTimePanel, &kKnobPanel}) panel(*p);

        drawGain(kFace_, values_[kMeterBoost] >= values_[kMeterCut] ? values_[kMeterBoost] : -values_[kMeterCut]);
        drawLevels();

        for (const Selector& sel : kSelectors) drawSelector(sel);
        drawDetector();

        for (float x : {489.5f}) {
            beginPath();
            moveTo(x, kKnobPanel.y + 16);
            lineTo(x, kKnobPanel.y + kKnobPanel.h - 16);
            strokeColor(rgb(kPanelEdge));
            strokeWidth(1);
            stroke();
        }
        for (const Knob& k : kKnobs) drawKnob(k);

        presetBar_.drawMenu(*this, kTheme, presets::kCount, preset_, [](int i) { return presets::kAll[i].name; });
        if (!presetBar_.isOpen() && drag_ < 0) help_.drawTip(*this, kTheme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            editParameter(kKnobs[drag_].id, false);
            drag_ = -1;
            return true;
        }

        const int picked = presetBar_.mouseDown(x, y, preset_, presets::kCount);
        if (picked != hnl::ui::PresetBar::kNotMine) {
            help_.hide();
            if (picked >= 0) applyPreset(picked);
            repaint();
            return true;
        }
        if (help_.buttonHit(x, y)) {
            help_.on = !help_.on;
            help_.motion(x, y);
            repaint();
            return true;
        }

        for (const Selector& sel : kSelectors) {
            if (!sel.box.contains(x, y)) continue;
            const int count = static_cast<int>(kParams[sel.id].max) + 1;
            const int choice = static_cast<int>((x - sel.box.x) / (sel.box.w / count));
            change(sel.id, static_cast<float>(choice < 0 ? 0 : (choice >= count ? count - 1 : choice)), true);
            return true;
        }

        const int hit = knobAt(x, y);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const bool doubleClick = hit == lastClick_ && ev.time - lastClickTime_ < 350;
        lastClick_ = hit;
        lastClickTime_ = ev.time;
        if (doubleClick || (ev.mod & kModifierControl)) {
            change(id, kParams[id].def, true);
            return true;
        }
        drag_ = hit;
        dragY_ = y;
        dragNorm_ = paramToNorm(id, values_[id]);
        editParameter(id, true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override {
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (presetBar_.isOpen()) {
            if (presetBar_.motion(x, y, presets::kCount)) repaint();
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
        change(kKnobs[drag_].id, normToParam(kKnobs[drag_].id, dragNorm_), false);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (presetBar_.isOpen() || drag_ >= 0) return false;
        const float s = static_cast<float>(getScaleFactor());
        const int hit = knobAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        change(id, normToParam(id, paramToNorm(id, values_[id]) + step * static_cast<float>(ev.delta.getY())), true);
        return true;
    }

private:
    int knobAt(float x, float y) const {
        for (int i = 0; i < kKnobCount; ++i)
            if (std::hypot(x - kKnobs[i].x, y - kKnobs[i].y) <= kKnobs[i].r + 8) return i;
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

    // ------------------------------------------------------------ presets

    void applyPreset(int index) {
        preset_ = index;
        presetName_ = presets::kAll[index].name;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float v = presets::value(index, id);
            if (!presets::inPreset(id) || values_[id] == v) continue;
            editParameter(id, true);
            values_[id] = v;
            setParameterValue(id, v);
            editParameter(id, false);
        }
        setState("preset", presetName_.c_str());
        repaint();
    }

    /** True when a control was moved since the preset was chosen. */
    bool presetModified() const {
        if (preset_ < 0) return false;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float tolerance = 1e-4f * (kParams[id].max - kParams[id].min);
            if (presets::inPreset(id) && std::fabs(values_[id] - presets::value(preset_, id)) > tolerance) return true;
        }
        return false;
    }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }

    void panel(const Rect& p) {
        beginPath();
        roundedRect(p.x, p.y, p.w, p.h, 6);
        fillColor(rgb(kPanel));
        fill();
        strokeColor(rgb(kPanelEdge));
        strokeWidth(1);
        stroke();
    }

    // -------------------------------------------------- gain change meter

    /** A needle that rests in the middle: right when the signal is being turned up, left when down. */
    void drawGain(const Rect& f, float db) {
        beginPath();
        roundedRect(f.x, f.y, f.w, f.h, 5);
        fillColor(rgb(kFace));
        fill();
        strokeColor(rgb(kFaceEdge));
        strokeWidth(1.5f);
        stroke();

        const float cx = f.x + f.w * 0.5f, py = f.y + f.h + 40, radius = 134, span = 0.78f; // radians each side of vertical
        const auto angle = [&](float pos) { return -kPi / 2 - span + pos * 2 * span; };
        const auto point = [&](float pos, float r, float& x, float& y) {
            x = cx + std::cos(angle(pos)) * r;
            y = py + std::sin(angle(pos)) * r;
        };
        const auto posOf = [&](float gain) { return 0.5f + gain / (2.0f * kMeterSpan); };

        beginPath();
        arc(cx, py, radius, angle(0.0f), angle(1.0f), CW);
        strokeColor(rgb(kInk));
        strokeWidth(1.5f);
        lineCap(BUTT);
        stroke();
        // The side on which the signal is turned up.
        beginPath();
        arc(cx, py, radius - 2, angle(0.5f), angle(1.0f), CW);
        strokeColor(rgb(kFaceRed));
        strokeWidth(5.5f);
        stroke();

        for (int mark = -static_cast<int>(kMeterSpan); mark <= static_cast<int>(kMeterSpan); ++mark) {
            const float pos = posOf(static_cast<float>(mark));
            const bool major = mark % 6 == 0;
            if (!major && mark % 3 != 0 && mark % 2 != 0) continue;
            float x0, y0, x1, y1;
            point(pos, radius, x0, y0);
            point(pos, radius + (major ? 9 : (mark % 3 == 0 ? 6 : 4)), x1, y1);
            beginPath();
            moveTo(x0, y0);
            lineTo(x1, y1);
            strokeColor(rgb(kInk));
            strokeWidth(major ? 1.5f : 1.0f);
            stroke();
            if (!major) continue;
            char text[8];
            std::snprintf(text, sizeof(text), mark > 0 ? "+%d" : "%d", mark);
            point(pos, radius + 19, x1, y1);
            label(text, x1, y1, 9, rgb(kInk), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        label("GAIN CHANGE", f.x + 12, f.y + 14, 9, rgb(kInk, 0.7f), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dB", f.x + f.w - 12, f.y + 14, 10, rgb(kInk, 0.7f), ALIGN_RIGHT | ALIGN_MIDDLE);

        float x0, y0, x1, y1;
        const float t = posOf(db), pos = t < -0.03f ? -0.03f : (t > 1.03f ? 1.03f : t);
        point(pos, (py - (f.y + f.h - 8)) / -std::sin(angle(pos)), x0, y0); // from just above the lower edge of the face
        point(pos, radius + 6, x1, y1);
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strokeColor(rgb(kInk));
        strokeWidth(1.6f);
        stroke();
    }

    // ------------------------------------------------------------- levels

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawLevels() {
        const Rect& p = kLevelPanel;
        const float x = p.x + 62, w = p.w - 78;
        label("LEVELS", p.x + 16, p.y + 16, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dBFS", p.x + p.w - 16, p.y + 16, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        static const char* const kRows[4] = {"IN L", "IN R", "OUT L", "OUT R"};
        static const uint32_t kMeters[4] = {kMeterInL, kMeterInR, kMeterOutL, kMeterOutR};
        for (int row = 0; row < 4; ++row) {
            const float y = p.y + 32 + row * 15 + (row >= 2 ? 8 : 0), top = levelPos(values_[kMeters[row]]);
            label(kRows[row], p.x + 16, y + 5, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath();
            rect(x, y, w, 10);
            fillColor(rgb(kWell));
            fill();
            struct Zone { float from, to; uint32_t colour; };
            const Zone zones[3] = {{-48, -12, kGreen}, {-12, -3, kYellow}, {-3, 6, kRed}};
            for (const Zone& z : zones) {
                const float a = levelPos(z.from), b = std::min(levelPos(z.to), top);
                if (b <= a) continue;
                beginPath();
                rect(x + w * a, y, w * (b - a), 10);
                fillColor(rgb(z.colour));
                fill();
            }
        }
        static const int kMarks[] = {-48, -36, -24, -12, -6, 0, 6};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), p.y + 108, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Peaks in and out, and the largest boost and cut as numbers.
        const float in = std::max(values_[kMeterInL], values_[kMeterInR]), out = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        readout("IN PEAK", in, in <= -99.0f, in > -0.05f, p.x + 16);
        readout("OUT PEAK", out, out <= -99.0f, out > -0.05f, p.x + 136);
        readout("BOOST", values_[kMeterBoost], false, false, p.x + 256);
        readout("CUT", values_[kMeterCut] < 0.05f ? 0.0f : -values_[kMeterCut], false, false, p.x + 376);
    }

    void readout(const char* name, float value, bool none, bool hot, float x) {
        const float y = kLevelPanel.y + 144;
        char text[24];
        if (none) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", value);
        label(name, x, y - 12, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, x, y + 4, 16, rgb(hot ? kRed : kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dB", x + 56, y + 6, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const Rect& b = sel.box;
        const int count = static_cast<int>(kParams[sel.id].max) + 1, active = choice(sel.id);
        const float w = b.w / count;
        label(sel.label, b.x, b.y - 13, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        fillColor(rgb(kTrack));
        fill();
        for (int i = 0; i < count; ++i) {
            if (i == active) {
                beginPath();
                roundedRect(b.x + i * w + 2, b.y + 2, w - 4, b.h - 4, 3);
                fillColor(rgb(kAccent));
                fill();
            }
            label(upper(kParams[sel.id].choices[i]).c_str(), b.x + (i + 0.5f) * w, b.y + b.h * 0.5f, 9, rgb(i == active ? kBg : kText),
                  ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    /** In words, what the detector listens to. */
    void drawDetector() {
        static const char* const kHears[4] = {
            "LISTENS TO THE WHOLE SIGNAL", "LISTENS BELOW 150 HZ: KICK AND BASS",
            "LISTENS TO 150 HZ - 2.5 KHZ: SNARE AND TOMS", "LISTENS ABOVE 2.5 KHZ: HATS AND CLICKS",
        };
        const float x = 410;
        label(kHears[choice(kFocus)], x, kTimePanel.y + 28, 10, rgb(kAccent), ALIGN_LEFT | ALIGN_MIDDLE);
        label("THE WHOLE SIGNAL IS SHAPED, WHATEVER THE DETECTOR LISTENS TO", x, kTimePanel.y + 46, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawKnob(const Knob& k) {
        const ParamInfo& p = kParams[k.id];
        const float a0 = 0.75f * kPi, a1 = 2.25f * kPi;
        const float angle = a0 + paramToNorm(k.id, values_[k.id]) * (a1 - a0);
        // Parameters centred on zero grow from the middle.
        const float from = p.min < 0.0f ? a0 + paramToNorm(k.id, 0.0f) * (a1 - a0) : a0;

        beginPath();
        arc(k.x, k.y, k.r, a0, a1, CW);
        strokeColor(rgb(kTrack));
        strokeWidth(3.5f);
        lineCap(ROUND);
        stroke();

        if (std::fabs(angle - from) > 0.01f) {
            beginPath();
            arc(k.x, k.y, k.r, std::min(from, angle), std::max(from, angle), CW);
            strokeColor(rgb(kAccent));
            strokeWidth(3.5f);
            stroke();
        }

        beginPath();
        circle(k.x, k.y, k.r - 6);
        fillColor(rgb(0x2e2a28));
        fill();
        beginPath();
        moveTo(k.x + std::cos(angle) * (k.r - 15), k.y + std::sin(angle) * (k.r - 15));
        lineTo(k.x + std::cos(angle) * (k.r - 7), k.y + std::sin(angle) * (k.r - 7));
        strokeColor(rgb(kText));
        strokeWidth(2);
        stroke();

        label(k.label, k.x, k.y - k.r - 14, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        if (k.id == kAttack || k.id == kSustain) std::snprintf(text, sizeof(text), std::fabs(v) < 0.5f ? "0 %%" : "%+.0f %%", v);
        else if (k.id == kLength) std::snprintf(text, sizeof(text), "%.0f ms", v);
        else if (k.id == kOutput) std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, k.x, k.y + k.r + 15, 11, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    hnl::ui::HelpTips help_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShaperUI)
};

UI* createUI() { return new ShaperUI(); }

END_NAMESPACE_DISTRHO
