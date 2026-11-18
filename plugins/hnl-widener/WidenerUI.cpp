// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Widener: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Widgets.hpp"
#include "dsp/Widener.hpp"
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
const uint32_t kLeft = 0x7fc4e8; // the left channel in the plot; the right one is the accent, mono is green
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kPlotPanel = {12, 52, 484, 194}, kImagePanel = {504, 52, 304, 194};
const Rect kSwitchPanel = {12, 254, 796, 70}, kMakePanel = {12, 332, 796, 120}, kShapePanel = {12, 460, 796, 120};
const Rect kPlot = {46, 78, 436, 140}; // the curves, inside the plot panel
const float kPlotTopDb = 9.0f, kPlotBottomDb = -21.0f, kPlotLowHz = 20.0f, kPlotHighHz = 20000.0f;
const float kFanX = 656, kFanY = 170, kFanR = 80; // the fan: its middle at the bottom, and how far it reaches

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kAmount, 69, 396, 24, "AMOUNT"}, {kHaas, 183, 396, 24, "HAAS"}, {kHaasTime, 296, 396, 24, "HAAS TIME"}, {kDiffuse, 410, 396, 24, "DIFFUSE"},
    {kSize, 524, 396, 24, "SIZE"}, {kDetune, 637, 396, 24, "DETUNE"}, {kCents, 751, 396, 24, "CENTS"},
    {kWidth, 78, 524, 24, "STEREO WIDTH"}, {kLow, 211, 524, 24, "WIDEN ABOVE"}, {kHigh, 344, 524, 24, "WIDEN BELOW"},
    {kFocus, 476, 524, 24, "FOCUS"}, {kDrift, 609, 524, 24, "DRIFT"}, {kOutput, 742, 524, 24, "OUTPUT"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; };
const Selector kSelectors[] = {
    {kHaasMode, {24, 286, 294, 26}, "HAAS"},
    {kBass, {334, 286, 130, 26}, "BASS"},
    {kMatch, {480, 286, 110, 26}, "LEVEL MATCH"},
    {kListen, {606, 286, 190, 26}, "LISTEN"},
};

// Lines between groups of knobs: x, and the top of the panel they stand in.
const float kDividers[5][2] = {{126.5f, 332}, {353.5f, 332}, {580.5f, 332}, {410.5f, 460}, {675.5f, 460}};

const float kPresetX = 552, kHeaderY = 14; // the preset bar ends 10 px before the help button
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x2c2826, 0x4a433e, kText, kDim, kAccent};

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class WidenerUI : public UI {
public:
    WidenerUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(UI_BASE_WIDTH - 24, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kPlotPanel.x, kPlotPanel.y, kPlotPanel.w, kPlotPanel.h}, "RESPONSE", help::kResponse);
        help_.add({kImagePanel.x, kImagePanel.y, kImagePanel.w, kImagePanel.h}, "IMAGE", help::kImage);
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

        hnl::ui::title(*this, "WIDENER", rgb(kText), rgb(kDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (const Rect* p : {&kPlotPanel, &kImagePanel, &kSwitchPanel, &kMakePanel, &kShapePanel}) panel(*p);

        drawResponse();
        drawImage();

        for (const Selector& sel : kSelectors) drawSelector(sel);
        for (const float* at : kDividers) {
            beginPath();
            moveTo(at[0], at[1] + 16);
            lineTo(at[0], at[1] + 104);
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
        if (!presetBar_.character(ev.string, ev.character)) return false;
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

    // ----------------------------------------------------------- response

    hnl::WidenerParams settings() const {
        hnl::WidenerParams p;
        p.amount = values_[kAmount];
        p.haas = values_[kHaas];
        p.haasMs = values_[kHaasTime];
        p.haasMode = choice(kHaasMode);
        p.diffuse = values_[kDiffuse];
        p.size = values_[kSize];
        p.low = values_[kLow];
        p.high = values_[kHigh];
        p.listen = choice(kListen);
        return p;
    }

    static float plotX(float hz) { return kPlot.x + kPlot.w * std::log(hz / kPlotLowHz) / std::log(kPlotHighHz / kPlotLowHz); }
    static float plotY(float db) {
        const float t = (kPlotTopDb - db) / (kPlotTopDb - kPlotBottomDb);
        return kPlot.y + kPlot.h * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
    }

    /** What a sound in the middle comes out as on the left, on the right and in mono; Detune, Drift and Focus are left out. */
    void drawResponse() {
        const Rect& p = kPlotPanel;
        const bool moving = values_[kAmount] > 0.0f && (values_[kDetune] > 0.0f || values_[kDrift] > 0.0f);
        label("RESPONSE", p.x + 16, p.y + 16, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label(moving ? "WITHOUT DETUNE AND DRIFT" : "TO A SOUND IN THE MIDDLE", p.x + 84, p.y + 16, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label("LEFT", p.x + p.w - 118, p.y + 16, 8, rgb(kLeft), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("RIGHT", p.x + p.w - 78, p.y + 16, 8, rgb(kAccent), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("MONO", p.x + p.w - 40, p.y + 16, 8, rgb(kGreen), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("dB", p.x + p.w - 16, p.y + 16, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);

        beginPath();
        rect(kPlot.x, kPlot.y, kPlot.w, kPlot.h);
        fillColor(rgb(kWell));
        fill();
        char text[96];
        for (int db = -18; db <= 6; db += 6) {
            const float y = std::floor(plotY(static_cast<float>(db))) + 0.5f;
            beginPath();
            moveTo(kPlot.x, y);
            lineTo(kPlot.x + kPlot.w, y);
            strokeColor(rgb(db == 0 ? kTrack : kPanelEdge));
            strokeWidth(1);
            stroke();
            std::snprintf(text, sizeof(text), db > 0 ? "+%d" : "%d", db);
            label(text, kPlot.x - 6, y, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        static const struct { float hz; const char* text; } kMarks[] = {{50, "50"}, {100, "100"}, {200, "200"}, {500, "500"}, {1000, "1k"},
                                                                         {2000, "2k"}, {5000, "5k"}, {10000, "10k"}};
        for (const auto& m : kMarks) {
            const float x = std::floor(plotX(m.hz)) + 0.5f;
            beginPath();
            moveTo(x, kPlot.y);
            lineTo(x, kPlot.y + kPlot.h);
            strokeColor(rgb(kPanelEdge));
            strokeWidth(1);
            stroke();
            label(m.text, x, kPlot.y + kPlot.h + 10, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // With Haas, Diffuse and Detune all at nothing a sound in the middle passes as it is, and the
        // lines are flat: say so, and say what Stereo Width and Bass then still work on.
        const bool makes = values_[kAmount] > 0.0f && (values_[kHaas] > 0.0f || values_[kDiffuse] > 0.0f || values_[kDetune] > 0.0f);
        if (!makes) {
            const bool own = std::fabs(values_[kWidth] - 100.0f) > 0.05f || choice(kBass) != 0;
            const float x = kPlot.x + kPlot.w * 0.5f, y = kPlot.y + kPlot.h * 0.72f;
            label("A SOUND IN THE MIDDLE PASSES UNCHANGED", x, y, 9, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
            label(own ? "STEREO WIDTH AND BASS ONLY CHANGE A SOUND THAT IS STEREO ALREADY" : "TURN UP HAAS, DIFFUSE OR DETUNE TO WIDEN IT", x, y + 14, 8, rgb(kDim),
                  ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Towards the right the rises and dips lie closer together than the plot can show. Each line
        // is therefore drawn as the band between its lowest and its highest point per column: a plain
        // line where it moves slowly, a see-through band with an edge above and below where it does not.
        const hnl::WidenerCurve curve(settings(), getSampleRate());
        const int columns = static_cast<int>(kPlot.w), per = 8;
        std::vector<hnl::WidenerCurve::Point> points(static_cast<size_t>(columns * per + 1));
        for (size_t i = 0; i < points.size(); ++i)
            points[i] = curve.at(kPlotLowHz * std::pow(static_cast<double>(kPlotHighHz / kPlotLowHz), static_cast<double>(i) / (per * columns)));
        static const uint32_t kColours[3] = {kGreen, kAccent, kLeft}; // mono underneath, left on top
        const size_t count = static_cast<size_t>(columns);
        std::vector<float> low(count), high(count), lowSeen(count), highSeen(count);
        std::vector<char> crowded(count);
        for (int line = 0; line < 3; ++line) {
            for (int i = 0; i < columns; ++i) {
                float lowest = 1e9f, highest = -1e9f, before = 0.0f, rising = 0.0f;
                int turns = 0;
                for (int k = 0; k <= per; ++k) { // the first point of the next column too, so that the columns join
                    const hnl::WidenerCurve::Point& at = points[static_cast<size_t>(i * per + k)];
                    const float db = static_cast<float>(line == 0 ? at.mono : line == 1 ? at.right : at.left);
                    lowest = std::min(lowest, db);
                    highest = std::max(highest, db);
                    if (k > 0 && (db - before) * rising < 0.0f) ++turns;
                    if (k > 0 && db != before) rising = db - before;
                    before = db;
                }
                lowSeen[static_cast<size_t>(i)] = plotY(lowest);
                highSeen[static_cast<size_t>(i)] = plotY(highest);
                crowded[static_cast<size_t>(i)] = turns >= 1;
            }
            // Where rises and dips follow each other within a few columns, the few points taken per column miss their tips:
            // there the band takes the lowest and highest of its neighbours too.
            for (int i = 0; i < columns; ++i) {
                const size_t at = static_cast<size_t>(i);
                low[at] = lowSeen[at];
                high[at] = highSeen[at];
                if (!crowded[at]) continue;
                for (int j = std::max(i - 4, 0); j <= std::min(i + 4, columns - 1); ++j) {
                    if (!crowded[static_cast<size_t>(j)]) continue;
                    low[at] = std::max(low[at], lowSeen[static_cast<size_t>(j)]);
                    high[at] = std::min(high[at], highSeen[static_cast<size_t>(j)]);
                }
            }
            beginPath();
            for (int i = 0; i < columns; ++i) {
                const float x = kPlot.x + static_cast<float>(i) + 0.5f;
                if (i == 0) moveTo(x, high[0]);
                else lineTo(x, high[static_cast<size_t>(i)]);
            }
            for (int i = columns - 1; i >= 0; --i) lineTo(kPlot.x + static_cast<float>(i) + 0.5f, low[static_cast<size_t>(i)]);
            closePath();
            fillColor(rgb(kColours[line], 0.3f));
            fill();
            for (const std::vector<float>* edge : {&high, &low}) {
                beginPath();
                for (int i = 0; i < columns; ++i) {
                    const float x = kPlot.x + static_cast<float>(i) + 0.5f;
                    if (i == 0) moveTo(x, (*edge)[0]);
                    else lineTo(x, (*edge)[static_cast<size_t>(i)]);
                }
                strokeColor(rgb(kColours[line]));
                strokeWidth(1.2f);
                stroke();
            }
        }
    }

    // -------------------------------------------------------------- image

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    /** A peak meter standing up. */
    void drawBar(float x, float db) {
        const float top = kFanY - kFanR, h = kFanR, pos = levelPos(db);
        beginPath();
        rect(x, top, 8, h);
        fillColor(rgb(kWell));
        fill();
        struct Zone { float from, to; uint32_t colour; };
        const Zone zones[3] = {{-48, -12, kGreen}, {-12, -3, kYellow}, {-3, 6, kRed}};
        for (const Zone& z : zones) {
            const float a = levelPos(z.from), b = std::min(levelPos(z.to), pos);
            if (b <= a) continue;
            beginPath();
            rect(x, top + h * (1.0f - b), 8, h * (b - a));
            fillColor(rgb(z.colour));
            fill();
        }
    }

    /** The outline of a slice of the fan: `half` radians to either side of `lean` (0 = straight up). */
    void slice(float lean, float half, float r) {
        const float up = -0.5f * kPi;
        const float from = std::max(up + lean - half, -kPi), to = std::min(up + lean + half, 0.0f);
        beginPath();
        moveTo(kFanX, kFanY);
        const int steps = 1 + static_cast<int>((to - from) / 0.05f);
        for (int i = 0; i <= steps; ++i) {
            const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
            lineTo(kFanX + std::cos(a) * r, kFanY + std::sin(a) * r);
        }
        closePath();
    }

    /** How far a sound opens the fan, from its side signal against its mid in dB: 0 = mono, a quarter turn = side only. */
    static float opening(float sideDb) { return std::atan(std::pow(10.0f, sideDb / 20.0f)); }

    void drawImage() {
        const Rect& p = kImagePanel;
        label("IMAGE", p.x + 16, p.y + 16, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("IN", p.x + p.w - 44, p.y + 16, 8, rgb(kText), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("OUT", p.x + p.w - 16, p.y + 16, 8, rgb(kAccent), ALIGN_RIGHT | ALIGN_MIDDLE);

        // The fan: straight up is mono, the lines at L and R are one speaker only, beyond them (red) the
        // two channels work against each other.
        const float up = -0.5f * kPi, quarter = 0.25f * kPi;
        for (float lean : {-1.5f * quarter, 1.5f * quarter}) {
            slice(lean, 0.5f * quarter, kFanR);
            fillColor(rgb(kRed, 0.13f));
            fill();
        }
        beginPath();
        arc(kFanX, kFanY, kFanR, kPi, 2.0f * kPi, CW);
        strokeColor(rgb(kTrack));
        strokeWidth(1);
        lineCap(BUTT);
        stroke();
        for (float lean : {-2.0f * quarter, -quarter, 0.0f, quarter, 2.0f * quarter}) {
            beginPath();
            moveTo(kFanX, kFanY);
            lineTo(kFanX + std::cos(up + lean) * kFanR, kFanY + std::sin(up + lean) * kFanR);
            strokeColor(rgb(lean == 0.0f ? kTrack : kPanelEdge));
            strokeWidth(1);
            stroke();
        }
        label("L", kFanX - std::cos(quarter) * (kFanR + 9), kFanY - std::sin(quarter) * (kFanR + 9), 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label("R", kFanX + std::cos(quarter) * (kFanR + 9), kFanY - std::sin(quarter) * (kFanR + 9), 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label("MONO", kFanX, kFanY - kFanR - 8, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);

        const float in = std::max(values_[kMeterInL], values_[kMeterInR]), out = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        if (out > -70.0f) { // what comes out, leaning to the louder side
            const float lean = std::atan(std::pow(10.0f, values_[kMeterBalance] / 20.0f)) - quarter;
            slice(lean, opening(values_[kMeterSideOut]), kFanR - 6);
            fillColor(rgb(kAccent, 0.45f));
            fill();
            strokeColor(rgb(kAccent));
            strokeWidth(1.5f);
            stroke();
        }
        if (in > -70.0f) { // what goes in
            slice(0.0f, opening(values_[kMeterSideIn]), kFanR - 6);
            strokeColor(rgb(kText, 0.8f));
            strokeWidth(1.5f);
            stroke();
        }

        // Peak levels on either side of it.
        drawBar(p.x + 18, values_[kMeterInL]);
        drawBar(p.x + 30, values_[kMeterInR]);
        drawBar(p.x + p.w - 38, values_[kMeterOutL]);
        drawBar(p.x + p.w - 26, values_[kMeterOutR]);
        label("IN", p.x + 28, kFanY + 9, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label("OUT", p.x + p.w - 28, kFanY + 9, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);

        // How alike the two outputs are: from cancelling each other, over unrelated, to the same.
        const float x = p.x + 56, w = p.w - 112, cy = kFanY + 19, corr = values_[kMeterCorrelation];
        label("L/R", p.x + 16, cy + 4, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        rect(x, cy, w, 8);
        fillColor(rgb(kWell));
        fill();
        beginPath();
        rect(x, cy, w * 0.5f, 8);
        fillColor(rgb(kRed, 0.16f));
        fill();
        const float at = x + w * 0.5f * (1.0f + (corr < -1.0f ? -1.0f : (corr > 1.0f ? 1.0f : corr)));
        beginPath();
        rect(at - 2, cy - 1, 4, 10);
        fillColor(rgb(corr < 0.0f ? kRed : kText));
        fill();
        label("-1", x, cy + 16, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label("WIDE", x + w * 0.5f, cy + 16, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label("MONO", x + w, cy + 16, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);

        // The side signal against the mid going in and coming out, the peak coming out and what Level Match applies.
        readout("SIDE IN", values_[kMeterSideIn], in <= -70.0f || values_[kMeterSideIn] <= -59.5f, false, p.x + 16);
        readout("SIDE OUT", values_[kMeterSideOut], out <= -70.0f || values_[kMeterSideOut] <= -59.5f, values_[kMeterSideOut] > 0.0f, p.x + 88);
        readout("OUT PEAK", out, out <= -99.0f, out > -0.05f, p.x + 160);
        readout("MATCH", values_[kMeterMatch], choice(kMatch) == 0, false, p.x + 232);
    }

    void readout(const char* name, float value, bool none, bool hot, float x) {
        const float y = kImagePanel.y + 178;
        char text[24];
        if (none) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", value);
        label(name, x, y - 10, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, x, y + 4, 13, rgb(hot ? kRed : kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dB", x + 44, y + 5, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
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

    void drawKnob(const Knob& k) {
        const ParamInfo& p = kParams[k.id];
        const float a0 = 0.75f * kPi, a1 = 2.25f * kPi;
        const float angle = a0 + paramToNorm(k.id, values_[k.id]) * (a1 - a0);
        // Parameters centred on zero grow from the middle, and Stereo Width from "unchanged".
        const float from = p.min < 0.0f || k.id == kWidth ? a0 + paramToNorm(k.id, k.id == kWidth ? 100.0f : 0.0f) * (a1 - a0) : a0;

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
        if (k.id == kHaasTime) std::snprintf(text, sizeof(text), "%.1f ms", v);
        else if (k.id == kCents) std::snprintf(text, sizeof(text), "%.0f ct", v);
        else if (k.id == kOutput) std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (k.id == kDrift && v <= 0.0f) std::snprintf(text, sizeof(text), "Off");
        else if (k.id == kDrift) std::snprintf(text, sizeof(text), "%.2f Hz", v);
        else if (k.id == kHigh && v >= static_cast<float>(hnl::kWideHighOffHz)) std::snprintf(text, sizeof(text), "Off");
        else if ((k.id == kLow || k.id == kHigh) && v >= 1000.0f) std::snprintf(text, sizeof(text), "%.1f kHz", v / 1000.0f);
        else if (k.id == kLow || k.id == kHigh) std::snprintf(text, sizeof(text), "%.0f Hz", v);
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
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WidenerUI)
};

UI* createUI() { return new WidenerUI(); }

END_NAMESPACE_DISTRHO
