// HNL Thickener: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Widgets.hpp"
#include "dsp/Thickener.hpp"
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
const uint32_t kLeft = 0x7fc4e8; // the left channel in the plot; the right one is the accent
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kPlotPanel = {12, 52, 484, 194}, kLevelPanel = {504, 52, 304, 194};
const Rect kSwitchPanel = {12, 254, 796, 70}, kCopyPanel = {12, 332, 796, 120}, kBusPanel = {12, 460, 796, 120};
const Rect kPlot = {46, 78, 436, 140}; // the curves, inside the plot panel
const float kPlotTopDb = 9.0f, kPlotBottomDb = -9.0f, kPlotLowHz = 20.0f, kPlotHighHz = 20000.0f;

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kAmount, 69, 396, 24, "AMOUNT"}, {kDelay, 183, 396, 24, "DELAY"}, {kDelayMix, 296, 396, 24, "DELAY MIX"}, {kGuard, 410, 396, 24, "LOW GUARD"},
    {kLowCut, 524, 396, 24, "LOW CUT"}, {kHighCut, 637, 396, 24, "HIGH CUT"}, {kClean, 751, 396, 24, "CLEAN HIT"},
    {kRoomMix, 69, 524, 24, "ROOM MIX"}, {kSize, 183, 524, 24, "ROOM SIZE"}, {kDecay, 296, 524, 24, "DECAY"}, {kDamp, 410, 524, 24, "DAMPING"},
    {kCrush, 524, 524, 24, "CRUSH"}, {kDrive, 637, 524, 24, "DRIVE"}, {kOutput, 751, 524, 24, "OUTPUT"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; };
const Selector kSelectors[] = {
    {kSide, {24, 286, 246, 26}, "DELAYED SIDE"},
    {kRoom, {290, 286, 150, 26}, "ROOM"},
    {kMatch, {460, 286, 150, 26}, "LEVEL MATCH"},
    {kListen, {630, 286, 166, 26}, "LISTEN"},
};

// Lines between groups of knobs: x, and the top of the panel they stand in.
const float kDividers[4][2] = {{467.5f, 332}, {694.5f, 332}, {467.5f, 460}, {694.5f, 460}};

const float kPresetX = 552, kHeaderY = 14; // the preset bar ends 10 px before the help button
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x2c2826, 0x4a433e, kText, kDim, kAccent};

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class ThickenerUI : public UI {
public:
    ThickenerUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(UI_BASE_WIDTH - 24, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kPlotPanel.x, kPlotPanel.y, kPlotPanel.w, kPlotPanel.h}, "RESPONSE", help::kResponse);
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

        hnl::ui::title(*this, "THICKENER", rgb(kText), rgb(kDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (const Rect* p : {&kPlotPanel, &kLevelPanel, &kSwitchPanel, &kCopyPanel, &kBusPanel}) panel(*p);

        drawResponse();
        drawLevels();

        for (const Selector& sel : kSelectors) drawSelector(sel);
        for (const float* at : {kDividers[0], kDividers[1], kDividers[2], kDividers[3]}) {
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

    // ----------------------------------------------------------- response

    hnl::ThickenerParams settings() const {
        hnl::ThickenerParams p;
        p.amount = values_[kAmount];
        p.delayMs = values_[kDelay];
        p.delayMix = values_[kDelayMix];
        p.side = choice(kSide);
        p.guard = values_[kGuard];
        p.lowCut = values_[kLowCut];
        p.highCut = values_[kHighCut];
        p.room = choice(kRoom) != 0;
        p.roomMix = values_[kRoomMix];
        p.listen = choice(kListen) != 0;
        return p;
    }

    static float plotX(float hz) { return kPlot.x + kPlot.w * std::log(hz / kPlotLowHz) / std::log(kPlotHighHz / kPlotLowHz); }
    static float plotY(float db) {
        const float t = (kPlotTopDb - db) / (kPlotTopDb - kPlotBottomDb);
        return kPlot.y + kPlot.h * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
    }

    /** What each channel does to a steady tone; the room, Clean Hit, Crush and Drive are left out. */
    void drawResponse() {
        const Rect& p = kPlotPanel;
        const hnl::ThickenerParams now = settings();
        label("RESPONSE", p.x + 16, p.y + 16, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        char text[96];
        const double step = 1000.0 / now.delayMs;
        std::snprintf(text, sizeof(text), "%s %.1f MS LATE: DIPS FROM %.0f HZ, EVERY %.0f HZ", now.side == hnl::kSideLeft ? "LEFT" : "RIGHT", now.delayMs, 0.5 * step, step);
        label(text, p.x + 84, p.y + 16, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label("LEFT", p.x + p.w - 84, p.y + 16, 8, rgb(kLeft), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("RIGHT", p.x + p.w - 40, p.y + 16, 8, rgb(kAccent), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("dB", p.x + p.w - 16, p.y + 16, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);

        beginPath();
        rect(kPlot.x, kPlot.y, kPlot.w, kPlot.h);
        fillColor(rgb(kWell));
        fill();
        for (int db = -6; db <= 6; db += 6) {
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

        // Towards the right the dips lie closer together than the plot can show: there each
        // column is drawn from its lowest to its highest point. The channel with the longer
        // delay goes first, so that the other one stays readable on top of it.
        const int columns = static_cast<int>(kPlot.w);
        for (int channel : {now.side == hnl::kSideLeft ? 0 : 1, now.side == hnl::kSideLeft ? 1 : 0}) {
            beginPath();
            for (int i = 0; i <= columns; ++i) {
                float low = 1e9f, high = -1e9f, last = 0.0f;
                for (int k = 0; k < 8; ++k) {
                    const double hz = kPlotLowHz * std::pow(static_cast<double>(kPlotHighHz / kPlotLowHz), (i + k / 8.0) / columns);
                    last = static_cast<float>(hnl::thickenerResponseDb(hz, channel, now));
                    low = std::min(low, last);
                    high = std::max(high, last);
                }
                const float x = kPlot.x + static_cast<float>(i);
                if (i == 0) moveTo(x, plotY(low));
                lineTo(x, plotY(low));
                if (high - low > 0.5f) {
                    lineTo(x, plotY(high));
                    lineTo(x, plotY(last));
                }
            }
            strokeColor(rgb(channel ? kAccent : kLeft));
            strokeWidth(1.5f);
            stroke();
        }
    }

    // ------------------------------------------------------------- levels

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawLevels() {
        const Rect& p = kLevelPanel;
        const float x = p.x + 56, w = p.w - 72;
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
        static const int kMarks[] = {-48, -36, -24, -12, 0, 6};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), p.y + 108, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // How alike the two outputs are: from cancelling each other, over unrelated, to the same.
        const float cy = p.y + 124, corr = values_[kMeterCorrelation];
        label("L/R", p.x + 16, cy + 5, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        rect(x, cy, w, 10);
        fillColor(rgb(kWell));
        fill();
        beginPath();
        rect(x, cy, w * 0.5f, 10);
        fillColor(rgb(kRed, 0.16f));
        fill();
        const float at = x + w * 0.5f * (1.0f + (corr < -1.0f ? -1.0f : (corr > 1.0f ? 1.0f : corr)));
        beginPath();
        rect(at - 2, cy - 1, 4, 12);
        fillColor(rgb(corr < 0.0f ? kRed : kText));
        fill();
        label("-1", x, cy + 19, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label("WIDE", x + w * 0.5f, cy + 19, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label("MONO", x + w, cy + 19, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);

        // The peak coming out, what Level Match applies and how far Crush turns the copy down.
        const float out = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        readout("OUT PEAK", out, out <= -99.0f, out > -0.05f, p.x + 16);
        readout("MATCH", values_[kMeterMatch], choice(kMatch) == 0, false, p.x + 112);
        readout("CRUSH", values_[kMeterCrush] < 0.05f ? 0.0f : -values_[kMeterCrush], values_[kCrush] <= 0.0f, false, p.x + 208);
    }

    void readout(const char* name, float value, bool none, bool hot, float x) {
        const float y = kLevelPanel.y + 172;
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
        if (k.id == kDelay) std::snprintf(text, sizeof(text), "%.1f ms", v);
        else if (k.id == kDecay) std::snprintf(text, sizeof(text), "%.2f s", v);
        else if (k.id == kOutput) std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (k.id == kGuard && v < static_cast<float>(hnl::kGuardOffHz)) std::snprintf(text, sizeof(text), "Off");
        else if (k.id == kLowCut && v <= static_cast<float>(hnl::kLowCutOffHz)) std::snprintf(text, sizeof(text), "Off");
        else if (k.id == kHighCut && v >= static_cast<float>(hnl::kHighCutOffHz)) std::snprintf(text, sizeof(text), "Off");
        else if (k.id == kHighCut && v >= 1000.0f) std::snprintf(text, sizeof(text), "%.1f kHz", v / 1000.0f);
        else if (k.id == kGuard || k.id == kLowCut || k.id == kHighCut) std::snprintf(text, sizeof(text), "%.0f Hz", v);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThickenerUI)
};

UI* createUI() { return new ThickenerUI(); }

END_NAMESPACE_DISTRHO
