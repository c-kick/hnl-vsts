// HNL Digital Piano: user interface (NanoVG). All coordinates are in a fixed
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

const uint32_t kBg = 0x14131a, kPanel = 0x1c1b24, kPanelEdge = 0x2d2b3a, kTrack = 0x35334a, kWell = 0x0c0b10;
const uint32_t kText = 0xe8e6f0, kDim = 0x8f8ca3, kAccent = 0xf0b45a;
const uint32_t kIvory = 0xe9e4d8, kIvoryEdge = 0x9a958a, kEbony = 0x1d1c22;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kKnobPanel = {12, 52, 596, 120}, kOutPanel = {616, 52, 192, 120};
const Rect kSwitchPanel = {12, 180, 796, 64}, kKeyPanel = {12, 252, 796, 176};

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kVolume, 48, 114, 20, "VOLUME"}, {kTone, 114, 114, 20, "TONE"}, {kVelocity, 180, 114, 20, "VELOCITY"},
    {kDecay, 246, 114, 20, "DECAY"}, {kRelease, 312, 114, 20, "RELEASE"},
    {kDetune, 378, 114, 20, "DETUNE"}, {kWidth, 444, 114, 20, "WIDTH"}, {kReverb, 510, 114, 20, "REVERB"},
    {kTune, 576, 114, 20, "TUNE"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; const char* const* names; int count; };
const Selector kSelectors[] = {
    {kVoice, {24, 208, 520, 26}, "VOICE", kVoiceNames, kVoiceChoices},
    {kOctave, {596, 208, 200, 26}, "OCTAVE", kOctaveNames, 5},
};

// The keyboard on screen: C2 to C7, 36 white keys.
const int kFirstKey = 36, kLastKey = 96, kWhiteKeys = 36;
const Rect kKeys = {24, 266, 772, 150};
const float kWhiteW = kKeys.w / kWhiteKeys, kBlackW = 13, kBlackH = 92;
const int kWhiteSteps[7] = {0, 2, 4, 5, 7, 9, 11};

bool isBlack(int note) {
    const int s = note % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}
/** Left edge of a white key, or the centre line of a black one. */
float keyX(int note) {
    const int octave = (note - kFirstKey) / 12, s = (note - kFirstKey) % 12;
    int white = 0;
    for (int i = 0; i < 7; ++i)
        if (kWhiteSteps[i] < s) ++white;
    return kKeys.x + (octave * 7 + white) * kWhiteW;
}

const float kPresetX = 552, kHeaderY = 14; // the preset bar ends 10 px before the help button
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x272536, 0x423f58, kText, kDim, kAccent};

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class PianoUI : public UI {
public:
    PianoUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(UI_BASE_WIDTH - 24, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kKeys.x, kKeys.y, kKeys.w, kKeys.h}, "KEYBOARD", help::kKeyboard);
        help_.add({kOutPanel.x, kOutPanel.y, kOutPanel.w, kOutPanel.h}, "OUTPUT", help::kOutput);
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

        hnl::ui::title(*this, "DIGITAL PIANO", rgb(kText));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (const Rect* p : {&kKnobPanel, &kOutPanel, &kSwitchPanel, &kKeyPanel}) panel(*p);

        for (const Knob& k : kKnobs) drawKnob(k);
        drawOutput();
        for (const Selector& sel : kSelectors) drawSelector(sel);
        drawKeyboard();

        presetBar_.drawMenu(*this, kTheme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        if (!presetBar_.isOpen() && drag_ < 0 && mouseNote_ < 0) help_.drawTip(*this, kTheme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (mouseNote_ >= 0) {
                play(-1, 0);
                return true;
            }
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

        const int key = keyAt(x, y);
        if (key >= 0) {
            play(key, velocityAt(y));
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
        if (mouseNote_ >= 0) { // sliding over the keys
            const int key = keyAt(x, y);
            if (key >= 0 && key != mouseNote_) play(key, velocityAt(y));
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

    // ----------------------------------------------------------- keyboard

    /** The key under a point, or -1. Black keys lie on top of the white ones. */
    static int keyAt(float x, float y) {
        if (!kKeys.contains(x, y)) return -1;
        if (y <= kKeys.y + kBlackH)
            for (int note = kFirstKey; note <= kLastKey; ++note)
                if (isBlack(note) && std::fabs(x - keyX(note)) <= kBlackW * 0.5f) return note;
        const int white = std::min(kWhiteKeys - 1, static_cast<int>((x - kKeys.x) / kWhiteW));
        return kFirstKey + (white / 7) * 12 + kWhiteSteps[white % 7];
    }

    /** Lower on the key is harder. */
    static int velocityAt(float y) {
        const float t = (y - kKeys.y) / kKeys.h;
        return static_cast<int>(30.0f + 97.0f * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t)));
    }

    /** Lets go of the key the mouse holds and presses `note` (-1 for none). */
    void play(int note, int velocity) {
        if (mouseNote_ >= 0) sendNote(0, static_cast<uint8_t>(mouseNote_), 0);
        mouseNote_ = note;
        if (note >= 0) sendNote(0, static_cast<uint8_t>(note), static_cast<uint8_t>(velocity));
        repaint();
    }

    void drawKeyboard() {
        for (int note = kFirstKey; note <= kLastKey; ++note) {
            if (isBlack(note)) continue;
            const float x = keyX(note);
            beginPath();
            roundedRect(x + 0.5f, kKeys.y, kWhiteW - 1, kKeys.h, 2);
            fillColor(rgb(note == mouseNote_ ? kAccent : kIvory));
            fill();
            if (note % 12 == 0) {
                char text[8];
                std::snprintf(text, sizeof(text), "C%d", note / 12 - 1);
                label(text, x + kWhiteW * 0.5f, kKeys.y + kKeys.h - 10, 8, rgb(kIvoryEdge), ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        for (int note = kFirstKey; note <= kLastKey; ++note) {
            if (!isBlack(note)) continue;
            beginPath();
            roundedRect(keyX(note) - kBlackW * 0.5f, kKeys.y, kBlackW, kBlackH, 2);
            fillColor(rgb(note == mouseNote_ ? kAccent : kEbony));
            fill();
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
            const float y = kOutPanel.y + 34 + c * 16, top = levelPos(values_[kMeterOutL + c]);
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
        static const int kMarks[] = {-48, -24, -12, 0, 6};
        for (int db : kMarks) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), kOutPanel.y + 70, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        const float peak = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        char text[48];
        if (peak <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", peak);
        label("PEAK", kOutPanel.x + 16, kOutPanel.y + 98, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, kOutPanel.x + 50, kOutPanel.y + 97, 14, rgb(peak > -0.05f ? kRed : kText), ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(text, sizeof(text), "%.0f", values_[kMeterVoices]);
        label(text, kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 97, 14, rgb(kAccent), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("VOICES", kOutPanel.x + kOutPanel.w - 44, kOutPanel.y + 98, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const Rect& b = sel.box;
        const int active = choice(sel.id);
        const float w = b.w / sel.count;
        label(sel.label, b.x, b.y - 13, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        fillColor(rgb(kTrack));
        fill();
        for (int i = 0; i < sel.count; ++i) {
            if (i == active) {
                beginPath();
                roundedRect(b.x + i * w + 2, b.y + 2, w - 4, b.h - 4, 3);
                fillColor(rgb(kAccent));
                fill();
            }
            label(upper(sel.names[i]).c_str(), b.x + (i + 0.5f) * w, b.y + b.h * 0.5f, 9, rgb(i == active ? kBg : kText), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawKnob(const Knob& k) {
        const float a0 = 0.75f * kPi, a1 = 2.25f * kPi;
        const float angle = a0 + paramToNorm(k.id, values_[k.id]) * (a1 - a0);
        // Parameters centred on zero grow from the middle.
        const float from = (k.id == kTone || k.id == kTune) ? a0 + 0.5f * (a1 - a0) : a0;

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
        fillColor(rgb(0x272536));
        fill();
        beginPath();
        moveTo(k.x + std::cos(angle) * (k.r - 13), k.y + std::sin(angle) * (k.r - 13));
        lineTo(k.x + std::cos(angle) * (k.r - 7), k.y + std::sin(angle) * (k.r - 7));
        strokeColor(rgb(kText));
        strokeWidth(2);
        stroke();

        label(k.label, k.x, k.y - k.r - 14, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        if (k.id == kVolume) std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (k.id == kTone || k.id == kTune) std::snprintf(text, sizeof(text), k.id == kTune ? "%+.0f ct" : "%+.0f", v);
        else if (k.id == kRelease) std::snprintf(text, sizeof(text), "%.0f ms", v);
        else if (k.id == kDetune && v < 0.05f) std::snprintf(text, sizeof(text), "OFF");
        else if (k.id == kDetune) std::snprintf(text, sizeof(text), "%.1f ct", v);
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, k.x, k.y + k.r + 15, 10, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int drag_ = -1, lastClick_ = -1, mouseNote_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoUI)
};

UI* createUI() { return new PianoUI(); }

END_NAMESPACE_DISTRHO
