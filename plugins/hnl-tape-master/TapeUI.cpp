// HNL Tape Master: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "Params.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

const uint32_t kBg = 0x161514, kPanel = 0x201e1c, kPanelEdge = 0x302d29, kTrack = 0x38342f, kWell = 0x0f0e0d;
const uint32_t kText = 0xece6dc, kDim = 0x968e82, kCopper = 0xe0a458;
const uint32_t kFace = 0xe9dfc4, kFaceEdge = 0xb8ad90, kInk = 0x24211d, kFaceRed = 0xb83a2b;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kMeterPanel = {12, 52, 536, 170}, kOutPanel = {556, 52, 252, 170};
const Rect kTapePanel = {12, 230, 796, 70}, kKnobPanel = {12, 308, 796, 120};
const Rect kFaces[2] = {{24, 68, 250, 138}, {286, 68, 250, 138}};

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kRecord, 84, 372, 22, "RECORD"}, {kReproduce, 236, 372, 22, "REPRODUCE"}, {kBias, 400, 372, 22, "BIAS"},
    {kWow, 540, 372, 22, "WOW"}, {kFlutter, 646, 372, 22, "FLUTTER"}, {kNoise, 752, 372, 22, "NOISE"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; };
const Selector kSelectors[] = {
    {kSpeed, {24, 262, 150, 26}, "SPEED (IPS)"}, {kFormula, {186, 262, 296, 26}, "TAPE FORMULA"},
    {kHead, {494, 262, 140, 26}, "HEAD WIDTH"}, {kPath, {646, 262, 150, 26}, "PATH"},
};

const float kLinkX = 160, kLinkY = 372;

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class TapeUI : public UI {
public:
    TapeUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
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

        label("HNL TAPE MASTER", 16, 27, 18, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("two-track mastering tape", 204, 28, 11, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);

        for (const Rect* p : {&kMeterPanel, &kOutPanel, &kTapePanel, &kKnobPanel}) panel(*p);

        drawVu(kFaces[0], values_[kMeterVuL], "L");
        drawVu(kFaces[1], values_[kMeterVuR], "R");
        drawOutput();

        for (const Selector& sel : kSelectors) drawSelector(sel);

        for (float x : {322.0f, 476.0f}) {
            beginPath();
            moveTo(x, kKnobPanel.y + 16);
            lineTo(x, kKnobPanel.y + kKnobPanel.h - 16);
            strokeColor(rgb(kPanelEdge));
            strokeWidth(1);
            stroke();
        }
        for (const Knob& k : kKnobs) drawKnob(k);
        drawLink();
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

        for (const Selector& sel : kSelectors) {
            if (!sel.box.contains(x, y)) continue;
            const int count = static_cast<int>(kParams[sel.id].max) + 1;
            const int choice = static_cast<int>((x - sel.box.x) / (sel.box.w / count));
            change(sel.id, static_cast<float>(choice < 0 ? 0 : (choice >= count ? count - 1 : choice)), true);
            return true;
        }
        if (std::fabs(x - kLinkX) <= 20 && std::fabs(y - kLinkY) <= 12) {
            change(kLink, values_[kLink] > 0.5f ? 0.0f : 1.0f, true);
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
        if (drag_ < 0) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY()) / s;
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        dragNorm_ += (dragY_ - y) / range;
        dragNorm_ = dragNorm_ < 0.0f ? 0.0f : (dragNorm_ > 1.0f ? 1.0f : dragNorm_);
        dragY_ = y;
        change(kKnobs[drag_].id, normToParam(kKnobs[drag_].id, dragNorm_), false);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (drag_ >= 0) return false;
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

    // ----------------------------------------------------------- VU meter

    /** Needle position 0..1 for a level in VU: linear in voltage, like the real thing. */
    static float vuPos(float db) {
        const float t = std::pow(10.0f, db / 20.0f) / 1.4125f; // +3 VU at the end of the scale
        return t < 0.0f ? 0.0f : (t > 1.06f ? 1.06f : t);
    }

    void drawVu(const Rect& f, float db, const char* channel) {
        beginPath();
        roundedRect(f.x, f.y, f.w, f.h, 5);
        fillColor(rgb(kFace));
        fill();
        strokeColor(rgb(kFaceEdge));
        strokeWidth(1.5f);
        stroke();

        const float cx = f.x + f.w * 0.5f, py = f.y + f.h + 40, radius = 134, span = 0.66f; // radians each side of vertical
        const auto angle = [&](float pos) { return -kPi / 2 - span + pos * 2 * span; };
        const auto point = [&](float pos, float r, float& x, float& y) {
            x = cx + std::cos(angle(pos)) * r;
            y = py + std::sin(angle(pos)) * r;
        };

        beginPath();
        arc(cx, py, radius, angle(vuPos(-20)), angle(vuPos(0)), CW);
        strokeColor(rgb(kInk));
        strokeWidth(1.5f);
        lineCap(BUTT);
        stroke();
        beginPath();
        arc(cx, py, radius - 2, angle(vuPos(0)), angle(1.0f), CW);
        strokeColor(rgb(kFaceRed));
        strokeWidth(5.5f);
        stroke();

        static const int kMarks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
        for (int mark : kMarks) {
            const float pos = vuPos(static_cast<float>(mark));
            const bool major = mark == -20 || mark == -10 || mark == -7 || mark == -5 || mark == -3 || mark == 0 || mark == 3;
            float x0, y0, x1, y1;
            point(pos, radius, x0, y0);
            point(pos, radius + (major ? 9 : 5), x1, y1);
            beginPath();
            moveTo(x0, y0);
            lineTo(x1, y1);
            strokeColor(rgb(mark > 0 ? kFaceRed : kInk));
            strokeWidth(major ? 1.5f : 1.0f);
            stroke();
            if (!major) continue;
            char text[8];
            std::snprintf(text, sizeof(text), mark > 0 ? "+%d" : "%d", mark < 0 ? -mark : mark);
            point(pos, radius + 19, x1, y1);
            label(text, x1, y1, 9, rgb(mark > 0 ? kFaceRed : kInk), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        label("VU", f.x + f.w - 12, f.y + 14, 10, rgb(kInk, 0.7f), ALIGN_RIGHT | ALIGN_MIDDLE);
        label(channel, f.x + 12, f.y + 14, 10, rgb(kInk, 0.7f), ALIGN_LEFT | ALIGN_MIDDLE);

        float x0, y0, x1, y1;
        const float pos = vuPos(db);
        point(pos, 52, x0, y0);
        point(pos, radius + 6, x1, y1);
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strokeColor(rgb(kInk));
        strokeWidth(1.6f);
        stroke();
    }

    // ------------------------------------------------------------- output

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawOutput() {
        const float x = kOutPanel.x + 32, w = kOutPanel.w - 48;
        label("OUTPUT", kOutPanel.x + 16, kOutPanel.y + 16, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        for (int c = 0; c < 2; ++c) {
            const float y = kOutPanel.y + 38 + c * 18, top = levelPos(values_[kMeterOutL + c]);
            label(c == 0 ? "L" : "R", kOutPanel.x + 16, y + 5, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
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
            label(text, x + w * levelPos(static_cast<float>(db)), kOutPanel.y + 78, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        const float peak = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        char text[48];
        if (peak <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", peak);
        label("PEAK", kOutPanel.x + 16, kOutPanel.y + 112, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, kOutPanel.x + 54, kOutPanel.y + 111, 16, rgb(peak > -0.05f ? kRed : kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dBFS", kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 112, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);

        // What is threaded on the machine right now.
        const int path = choice(kPath);
        std::string line;
        if (path == 2) line = "ELECTRONICS ONLY, NO TAPE";
        else line = std::string(kSpeedNames[choice(kSpeed)]) + " IPS  /  " + upper(kFormulaNames[choice(kFormula)]) + "  /  " + kHeadNames[choice(kHead)];
        label(line.c_str(), kOutPanel.x + 16, kOutPanel.y + 148, 9, rgb(kCopper), ALIGN_LEFT | ALIGN_MIDDLE);
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
                fillColor(rgb(kCopper));
                fill();
            }
            label(upper(kParams[sel.id].choices[i]).c_str(), b.x + (i + 0.5f) * w, b.y + b.h * 0.5f, 9, rgb(i == active ? kBg : kText),
                  ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawLink() {
        const bool active = values_[kLink] > 0.5f;
        label("LINK", kLinkX, kLinkY - 20, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        beginPath();
        roundedRect(kLinkX - 14, kLinkY - 7, 28, 14, 7);
        fillColor(active ? rgb(kCopper) : rgb(kTrack));
        fill();
        beginPath();
        circle(active ? kLinkX + 7 : kLinkX - 7, kLinkY, 5);
        fillColor(rgb(active ? kBg : kDim));
        fill();
        // Lines from the switch to the two knobs it ties together.
        for (float dir : {-1.0f, 1.0f}) {
            beginPath();
            moveTo(kLinkX + dir * 20, kLinkY);
            lineTo(kLinkX + dir * 40, kLinkY);
            strokeColor(rgb(active ? kCopper : kTrack, active ? 0.6f : 1.0f));
            strokeWidth(1.5f);
            stroke();
        }
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
            strokeColor(rgb(kCopper));
            strokeWidth(3.5f);
            stroke();
        }

        beginPath();
        circle(k.x, k.y, k.r - 6);
        fillColor(rgb(0x2b2825));
        fill();
        beginPath();
        moveTo(k.x + std::cos(angle) * (k.r - 14), k.y + std::sin(angle) * (k.r - 14));
        lineTo(k.x + std::cos(angle) * (k.r - 7), k.y + std::sin(angle) * (k.r - 7));
        strokeColor(rgb(kText));
        strokeWidth(2);
        stroke();

        label(k.label, k.x, k.y - k.r - 14, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        if (p.unit[0] == 'd') std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (k.id == kBias) std::snprintf(text, sizeof(text), "%+.0f %%", v);
        else if (k.id == kNoise && v < 0.5f) std::snprintf(text, sizeof(text), "OFF");
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, k.x, k.y + k.r + 15, 11, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeUI)
};

UI* createUI() { return new TapeUI(); }

END_NAMESPACE_DISTRHO
