// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Digital Piano: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Skin.hpp"
#include "../../common/ui/WindowSize.hpp"
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

// The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp).
hnl::ui::Skin classicLook() {
    hnl::ui::Skin k = hnl::ui::classicSkin(0x14131a, 0x1c1b24, 0x2d2b3a, 0x35334a, 0x0c0b10, 0xe8e6f0, 0x8f8ca3, 0xf0b45a, 0x272536, 0x423f58);
    k.knobHi = k.knobLo = k.knobEdge = 0x272536;
    return k;
}
const hnl::ui::Skin kClassic = classicLook();
// The keys are a picture of a keyboard and have its colours in every look.
const uint32_t kIvory = 0xe9e4d8, kIvoryEdge = 0x9a958a, kEbony = 0x1d1c22;

typedef hnl::ui::Box Rect;

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

const float kPresetX = 524, kHeaderY = 14; // the preset bar ends 10 px before the help button
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24; // help, and right of it the choice of look

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
        size_.open(skin_); // the window opens at the size it was left at

        // Help: wide areas first, single controls on top.
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK AND SIZE", hnl::ui::SkinMenu::helpWithSizes());
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

    void uiIdle() override {
        if (skin_.poll()) repaint(); // another HNL window chose a look
        if (size_.follow(skin_)) repaint(); // the window was dragged to another size
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = size_.zoom();
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme theme = k.theme();
        hnl::ui::faceplate(*this, k, UI_BASE_WIDTH, UI_BASE_HEIGHT);

        hnl::ui::title(*this, "DIGITAL PIANO", rgb(k.plateText), rgb(k.plateDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        // In the order of the flat look. The keyboard lies in a dark well of its own, as under a lid.
        hnl::ui::platePanel(*this, k, kKnobPanel);
        hnl::ui::displayPanel(*this, k, kOutPanel);
        hnl::ui::platePanel(*this, k, kSwitchPanel);
        hnl::ui::displayPanel(*this, k, kKeyPanel);

        for (const Knob& knob : kKnobs) drawKnob(knob);
        drawOutput();
        for (const Selector& sel : kSelectors) drawSelector(sel);
        drawKeyboard();

        presetBar_.drawMenu(*this, theme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && drag_ < 0 && mouseNote_ < 0) help_.drawTip(*this, theme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = size_.zoom();
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
        if (const int cog = presetBar_.isBusy() ? hnl::ui::SkinMenu::kNotMine : skin_.mouseDown(x, y)) { // the cog, or its list while it is open
            if (cog == hnl::ui::SkinMenu::kSized) size_.choose(skin_.size()); // a size from the list: the window follows at once
            help_.hide();
            repaint();
            return true;
        }
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
        const float s = size_.zoom();
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (presetBar_.isOpen()) {
            if (presetBar_.motion(x, y, list_.count())) repaint();
            return true;
        }
        if (skin_.isOpen()) {
            if (skin_.motion(x, y)) repaint();
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
        if (!presetBar_.character(ev.string, ev.character)) return false;
        repaint();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (presetBar_.isBusy() || drag_ >= 0) return false;
        const float s = size_.zoom();
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

    /** The look: in every one of them what is on or lit has this plugin's own colour. */
    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }

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
        const hnl::ui::Skin k = skin();
        for (int note = kFirstKey; note <= kLastKey; ++note) {
            if (isBlack(note)) continue;
            const float x = keyX(note);
            beginPath();
            roundedRect(x + 0.5f, kKeys.y, kWhiteW - 1, kKeys.h, 2);
            fillColor(rgb(note == mouseNote_ ? k.accent : kIvory));
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
            fillColor(rgb(note == mouseNote_ ? k.accent : kEbony));
            fill();
        }
    }

    // ------------------------------------------------------------- output

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawOutput() {
        const hnl::ui::Skin k = skin();
        const float x = kOutPanel.x + 32, w = kOutPanel.w - 48;
        label("OUTPUT", kOutPanel.x + 16, kOutPanel.y + 16, 10, rgb(k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        for (int c = 0; c < 2; ++c) {
            const float y = kOutPanel.y + 34 + c * 16, top = levelPos(values_[kMeterOutL + c]);
            label(c == 0 ? "L" : "R", kOutPanel.x + 16, y + 5, 9, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath();
            rect(x, y, w, 10);
            fillColor(rgb(k.solid ? k.track : k.well)); // solid: the segments that are not lit
            fill();
            struct Zone { float from, to; uint32_t colour; };
            const Zone zones[3] = {{-48, -12, k.green}, {-12, -3, k.yellow}, {-3, 6, k.red}};
            for (const Zone& z : zones) {
                const float a = levelPos(z.from), b = std::min(levelPos(z.to), top);
                if (b <= a) continue;
                beginPath();
                rect(x + w * a, y, w * (b - a), 10);
                fillColor(rgb(z.colour));
                fill();
            }
            hnl::ui::segments(*this, k, x, y, w, 10);
        }
        static const int kMarks[] = {-48, -24, -12, 0, 6};
        for (int db : kMarks) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), kOutPanel.y + 70, 8, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        const float peak = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        char text[48];
        if (peak <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", peak);
        label("PEAK", kOutPanel.x + 16, kOutPanel.y + 98, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, kOutPanel.x + 50, kOutPanel.y + 97, 14, rgb(peak > -0.05f ? k.red : k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(text, sizeof(text), "%.0f", values_[kMeterVoices]);
        label(text, kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 97, 14, rgb(k.accent), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("VOICES", kOutPanel.x + kOutPanel.w - 44, kOutPanel.y + 98, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const hnl::ui::Skin k = skin();
        const Rect& b = sel.box;
        label(sel.label, b.x, b.y - 13, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        std::string text;
        hnl::ui::keys(*this, k, b, sel.count, choice(sel.id), [&](int i) {
            text = upper(sel.names[i]);
            return text.c_str();
        });
    }

    void drawKnob(const Knob& k) {
        const hnl::ui::Skin look = skin();
        // Parameters centred on zero grow from the middle.
        const float from = (k.id == kTone || k.id == kTune) ? 0.5f : 0.0f;
        hnl::ui::knob(*this, look, k.x, k.y, k.r, paramToNorm(k.id, values_[k.id]), from, look.ring, 13);

        label(k.label, k.x, k.y - k.r - 14, 9, rgb(look.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        if (k.id == kVolume) std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (k.id == kTone || k.id == kTune) std::snprintf(text, sizeof(text), k.id == kTune ? "%+.0f ct" : "%+.0f", v);
        else if (k.id == kRelease) std::snprintf(text, sizeof(text), "%.0f ms", v);
        else if (k.id == kDetune && v < 0.05f) std::snprintf(text, sizeof(text), "OFF");
        else if (k.id == kDetune) std::snprintf(text, sizeof(text), "%.1f ct", v);
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, k.x, k.y + k.r + 15, 10, rgb(look.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
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
    hnl::ui::SkinMenu skin_;
    hnl::ui::WindowSize<PianoUI> size_{*this, "digital-piano", UI_BASE_WIDTH, UI_BASE_HEIGHT};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoUI)
};

UI* createUI() { return new PianoUI(); }

END_NAMESPACE_DISTRHO
