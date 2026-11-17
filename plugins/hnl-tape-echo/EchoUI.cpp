// HNL Tape Echo: user interface (NanoVG). All coordinates are in a fixed
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

const uint32_t kBg = 0x121514, kPanel = 0x1a1f1d, kPanelEdge = 0x29322e, kTrack = 0x313a36, kWell = 0x0b0d0c;
const uint32_t kText = 0xe3ece7, kDim = 0x86968f, kMint = 0x6fd3a8, kTapeBrown = 0x8a6f52;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kTapePanel = {12, 52, 536, 170}, kOutPanel = {556, 52, 252, 170};
const Rect kSwitchPanel = {12, 230, 796, 70}, kKnobPanel = {12, 308, 796, 120};

// The tape runs left to right over the record head and the three playback heads.
const float kTapeY = 84;
const Rect kRecHead = {28, 94, 72, 52};
const Rect kHeadBox[3] = {{120, 94, 128, 52}, {262, 94, 128, 52}, {404, 94, 128, 52}};
const Rect kTapeMeter = {76, 170, 456, 10};

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kInput, 56, 372, 22, "INPUT"}, {kTime, 130, 372, 22, "TIME"}, {kIntensity, 204, 372, 22, "INTENSITY"},
    {kEcho, 290, 372, 22, "ECHO"}, {kReverb, 364, 372, 22, "REVERB"},
    {kBass, 450, 372, 22, "BASS"}, {kTreble, 524, 372, 22, "TREBLE"},
    {kWobble, 610, 372, 22, "WOBBLE"}, {kNoise, 684, 372, 22, "NOISE"}, {kWidth, 758, 372, 22, "WIDTH"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);
const float kKnobDividers[] = {247, 407, 567};

static const char* const kSyncNames[] = {"Free", "Sync"};
static const char* const kDryNames[] = {"Off", "On"};

// Two-way switches are drawn like the selectors, with their own names.
struct Selector { uint32_t id; Rect box; const char* label; const char* const* names; int count; };
const Selector kSelectors[] = {
    {kSync, {24, 262, 150, 26}, "ECHO TIME", kSyncNames, 2},
    {kDivision, {186, 262, 380, 26}, "DIVISION (HEAD 1)", kDivisionNames, 5},
    {kDry, {646, 262, 150, 26}, "DRY SIGNAL", kDryNames, 2},
};

const float kPresetX = 204, kHeaderY = 14;
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x242b28, 0x3c4843, kText, kDim, kMint};

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class EchoUI : public UI {
public:
    EchoUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(UI_BASE_WIDTH - 24, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kTapeMeter.x - 52, kTapeMeter.y - 8, kTapeMeter.w + 56, 44}, "TAPE LEVEL", help::kTape);
        help_.add({kOutPanel.x, kOutPanel.y, kOutPanel.w, kOutPanel.h}, "OUTPUT", help::kOutput);
        for (int k = 0; k < 3; ++k) {
            const Rect& b = kHeadBox[k];
            help_.add({b.x, b.y, b.w, b.h}, kParams[kHead1 + k].name, help::forParam(kHead1 + k));
        }
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
        rect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
        fillColor(rgb(kBg));
        fill();

        label("HNL TAPE ECHO", 16, 27, 18, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (const Rect* p : {&kTapePanel, &kOutPanel, &kSwitchPanel, &kKnobPanel}) panel(*p);

        drawTape();
        drawOutput();
        for (const Selector& sel : kSelectors) drawSelector(sel);

        for (float x : kKnobDividers) {
            beginPath();
            moveTo(x, kKnobPanel.y + 16);
            lineTo(x, kKnobPanel.y + kKnobPanel.h - 16);
            strokeColor(rgb(kPanelEdge));
            strokeWidth(1);
            stroke();
        }
        for (const Knob& k : kKnobs) drawKnob(k);

        presetBar_.drawMenu(*this, kTheme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
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

        for (int k = 0; k < 3; ++k)
            if (kHeadBox[k].contains(x, y)) {
                change(kHead1 + k, values_[kHead1 + k] > 0.5f ? 0.0f : 1.0f, true);
                return true;
            }
        for (const Selector& sel : kSelectors) {
            if (!sel.box.contains(x, y)) continue;
            const int choice = static_cast<int>((x - sel.box.x) / (sel.box.w / sel.count));
            change(sel.id, static_cast<float>(choice < 0 ? 0 : (choice >= sel.count ? sel.count - 1 : choice)), true);
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
        change(kKnobs[drag_].id, normToParam(kKnobs[drag_].id, dragNorm_), false);
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
        const int hit = knobAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        change(id, normToParam(id, paramToNorm(id, values_[id]) + step * static_cast<float>(ev.delta.getY())), true);
        return true;
    }

private:
    bool synced() const { return values_[kSync] > 0.5f; }

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
            if (presets::inPreset(id) && std::fabs(values_[id] - list_.value(preset_, id)) > tolerance) return true;
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

    // --------------------------------------------------------------- tape

    static float tapePos(float db) { // -30..+12 dB -> 0..1
        const float t = (db + 30.0f) / 42.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawTape() {
        label("TAPE LOOP", 28, 68, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("CLICK A HEAD TO SWITCH IT", kTapePanel.x + kTapePanel.w - 16, 68, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);

        // The tape, and where each head touches it.
        beginPath();
        moveTo(kRecHead.x, kTapeY);
        lineTo(kHeadBox[2].x + kHeadBox[2].w, kTapeY);
        strokeColor(rgb(kTapeBrown));
        strokeWidth(3);
        lineCap(BUTT);
        stroke();
        const auto contact = [&](const Rect& b, const Color& colour) {
            const float cx = b.x + b.w * 0.5f;
            hnl::ui::triangle(*this, cx - 5, b.y, cx + 5, b.y, cx, kTapeY + 2, colour);
        };

        beginPath();
        roundedRect(kRecHead.x, kRecHead.y, kRecHead.w, kRecHead.h, 5);
        fillColor(rgb(kWell));
        fill();
        strokeColor(rgb(kPanelEdge));
        strokeWidth(1);
        stroke();
        contact(kRecHead, rgb(kDim));
        label("RECORD", kRecHead.x + kRecHead.w * 0.5f, kRecHead.y + 18, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label(synced() ? "SYNC" : "FREE", kRecHead.x + kRecHead.w * 0.5f, kRecHead.y + 35, 11, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);

        const float time = values_[kMeterTime]; // where the motor is now
        for (int k = 0; k < 3; ++k) {
            const Rect& b = kHeadBox[k];
            const bool active = values_[kHead1 + k] > 0.5f;
            beginPath();
            roundedRect(b.x, b.y, b.w, b.h, 5);
            fillColor(rgb(active ? kMint : kTrack));
            fill();
            contact(b, rgb(active ? kMint : kTrack));
            char text[24];
            std::snprintf(text, sizeof(text), "HEAD %d", k + 1);
            label(text, b.x + b.w * 0.5f, b.y + 17, 9, rgb(active ? kBg : kDim, active ? 0.8f : 1.0f), ALIGN_CENTER | ALIGN_MIDDLE);
            std::snprintf(text, sizeof(text), "%.0f ms", time * (k + 1));
            label(text, b.x + b.w * 0.5f, b.y + 35, 15, rgb(active ? kBg : kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Record level.
        const Rect& m = kTapeMeter;
        const float top = tapePos(values_[kMeterTape]);
        label("LEVEL", 28, m.y + 5, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        rect(m.x, m.y, m.w, m.h);
        fillColor(rgb(kWell));
        fill();
        struct Zone { float from, to; uint32_t colour; };
        const Zone zones[3] = {{-30, -3, kGreen}, {-3, 3, kYellow}, {3, 12, kRed}};
        for (const Zone& z : zones) {
            const float a = tapePos(z.from), b = std::min(tapePos(z.to), top);
            if (b <= a) continue;
            beginPath();
            rect(m.x + m.w * a, m.y, m.w * (b - a), m.h);
            fillColor(rgb(z.colour));
            fill();
        }
        static const int kMarks[] = {-30, -20, -10, -3, 0, 3, 6, 12};
        for (int db : kMarks) {
            char text[16];
            std::snprintf(text, sizeof(text), db > 0 ? "+%d" : "%d", db);
            label(text, m.x + m.w * tapePos(static_cast<float>(db)), m.y + 24, 8, rgb(db > 0 ? kRed : kDim),
                  (db == 12 ? ALIGN_RIGHT : db == -30 ? ALIGN_LEFT : ALIGN_CENTER) | ALIGN_MIDDLE);
        }
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

        // What the machine is doing right now.
        std::string heads;
        for (int k = 0; k < 3; ++k)
            if (values_[kHead1 + k] > 0.5f) heads += (heads.empty() ? "" : "+") + std::to_string(k + 1);
        std::string line = heads.empty() ? "NO HEADS" : (heads.size() > 1 ? "HEADS " : "HEAD ") + heads;
        if (values_[kReverb] >= 0.5f) line += "  /  SPRING";
        if (values_[kDry] < 0.5f) line += "  /  WET ONLY";
        label(line.c_str(), kOutPanel.x + 16, kOutPanel.y + 148, 9, rgb(kMint), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const Rect& b = sel.box;
        const int active = choice(sel.id);
        const bool idle = sel.id == kDivision && !synced(); // the division only counts with sync on
        const float w = b.w / sel.count;
        label(sel.label, b.x, b.y - 13, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        fillColor(rgb(kTrack, idle ? 0.5f : 1.0f));
        fill();
        for (int i = 0; i < sel.count; ++i) {
            if (i == active) {
                beginPath();
                roundedRect(b.x + i * w + 2, b.y + 2, w - 4, b.h - 4, 3);
                fillColor(rgb(idle ? kDim : kMint, idle ? 0.35f : 1.0f));
                fill();
            }
            label(upper(sel.names[i]).c_str(), b.x + (i + 0.5f) * w, b.y + b.h * 0.5f, 9,
                  rgb(i == active && !idle ? kBg : (idle ? kDim : kText)), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawKnob(const Knob& k) {
        const ParamInfo& p = kParams[k.id];
        const bool idle = k.id == kTime && synced(); // the tempo sets the time
        const float a0 = 0.75f * kPi, a1 = 2.25f * kPi;
        const float angle = a0 + paramToNorm(k.id, values_[k.id]) * (a1 - a0);
        // Parameters centred on zero grow from the middle.
        const float from = p.min < 0.0f && k.id != kInput ? a0 + paramToNorm(k.id, 0.0f) * (a1 - a0) : a0;

        beginPath();
        arc(k.x, k.y, k.r, a0, a1, CW);
        strokeColor(rgb(kTrack));
        strokeWidth(3.5f);
        lineCap(ROUND);
        stroke();

        if (!idle && std::fabs(angle - from) > 0.01f) {
            beginPath();
            arc(k.x, k.y, k.r, std::min(from, angle), std::max(from, angle), CW);
            strokeColor(rgb(kMint));
            strokeWidth(3.5f);
            stroke();
        }

        beginPath();
        circle(k.x, k.y, k.r - 6);
        fillColor(rgb(0x242b28));
        fill();
        beginPath();
        moveTo(k.x + std::cos(angle) * (k.r - 14), k.y + std::sin(angle) * (k.r - 14));
        lineTo(k.x + std::cos(angle) * (k.r - 7), k.y + std::sin(angle) * (k.r - 7));
        strokeColor(rgb(idle ? kDim : kText));
        strokeWidth(2);
        stroke();

        label(k.label, k.x, k.y - k.r - 14, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        if (idle) std::snprintf(text, sizeof(text), "SYNC");
        else if (k.id == kTime) std::snprintf(text, sizeof(text), "%.0f ms", v);
        else if (p.unit[0] == 'd') std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (k.id == kNoise && v < 0.5f) std::snprintf(text, sizeof(text), "OFF");
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, k.x, k.y + k.r + 15, 11, rgb(idle ? kDim : kText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoUI)
};

UI* createUI() { return new EchoUI(); }

END_NAMESPACE_DISTRHO
