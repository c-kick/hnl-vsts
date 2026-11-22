// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Sampler: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
//
// The UI keeps no instrument of its own: it shows the document of the plugin's
// sampler (dsp/Sampler.hpp), which it reaches through the plugin instance, and
// changes it there. Six pages share one set of drawing and mouse code: a page
// is a list of controls, each naming the setting it shows.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/UserPresets.hpp"
#include "../../common/ui/Skin.hpp"
#include "../../common/ui/Widgets.hpp"
#include "dsp/Analysis.hpp"
#include "dsp/Sampler.hpp"
#include "Help.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;
using namespace hnl::sampler;
typedef hnl::ui::Box Box;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) { return hnl::ui::rgb(hex, alpha); }

/** The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp). */
hnl::ui::Skin classicLook() {
    hnl::ui::Skin k = hnl::ui::classicSkin(0x14131a, 0x1c1b24, 0x2d2b3a, 0x35334a, 0x0c0b10, 0xe8e6f0, 0x8f8ca3, 0x5ac8f0, 0x272536, 0x423f58);
    k.knobHi = k.knobLo = k.knobEdge = 0x272536;
    return k;
}
const hnl::ui::Skin kClassic = classicLook();
const uint32_t kWarm = 0xf0b45a;                   // the second colour: the pages, the host's own knobs, what a loop or a trigger is marked with
const uint32_t kIvory = 0xd9d5cb, kEbony = 0x26252e, kKeyPrint = 0x55524a; // the keyboard is a picture of one, the same in every look

const float kPi = 3.14159265f;
const float kPresetX = 272, kHeaderY = 12;
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24; // help, and right of it the choice of look
const float kVolumeX = 742, kMeterX = 772, kVoicesX = 924;            // the output, between the instrument's name and the help button
const Box kOutputWindow = {765, 15, 165, 25};                         // the solid looks show level and voices in a display
const Box kPageArea = {12, 86, 976, 480}, kKeys = {12, 584, 976, 46}, kRange = {12, 572, 976, 8};
const float kKeyW = kKeys.w / 128.0f;
const float kBlackLength = 0.62f; // of a white key

enum Page { kPageMap, kPageSample, kPageSynth, kPageMod, kPageMix, kPageFiles, kPageCount };
const char* const kPageNames[kPageCount] = {"MAP", "SAMPLE", "SYNTH", "MOD", "MIX", "FILES"};
Box partTab(int p) { return {12.0f + p * 56.0f, 54, 52, 24}; }
Box pageTab(int p) { return {988.0f - (kPageCount - p) * 76.0f, 54, 72, 24}; }

enum Scope { kHost, kOfPart, kOfZone, kShared };
enum Style { kKnob, kChoiceBox, kToggleBox, kNumber };
/** A control on a page: which setting, where, and how it looks. `part` -1 means the chosen part. */
struct Ctl {
    int scope, id, style;
    float x, y, w;
    std::string label;
    int part;
    Box box() const { return style == kKnob ? Box{x - w * 0.5f, y - 26, w, 62} : Box{x, y - 12, w, 30}; }
    Box face() const { return style == kKnob ? Box{x - 15, y - 15, 30, 30} : Box{x, y, w, 18}; }
};

const std::vector<Info>& infoOf(int scope) { return scope == kOfPart ? partInfo() : (scope == kOfZone ? zoneInfo() : globalInfo()); }

bool isBlack(int key) {
    const int s = key % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}
std::string keyName(int key) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(kNames[key % 12]) + std::to_string(key / 12 - 1);
}
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return s;
}
std::string fileName(const std::string& path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}
std::string extension(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : SampleCache::lower(name.substr(dot));
}

/** A value as text, with its unit. */
std::string valueText(const Info& p, double v, bool key = false) {
    char s[48];
    if (key) return keyName(static_cast<int>(v));
    if (p.kind == kChoice || p.kind == kToggle) return p.choices != nullptr ? p.choices[static_cast<int>(v)] : (v >= 0.5 ? "On" : "Off");
    const std::string unit = p.unit == "cents" ? "ct" : p.unit;
    if (p.unit == "Hz" && v >= 1000.0) std::snprintf(s, sizeof(s), "%.2f kHz", v / 1000.0);
    else if (p.unit == "ms" && v >= 1000.0) std::snprintf(s, sizeof(s), "%.2f s", v / 1000.0);
    else if (p.kind == kInteger || std::fabs(v) >= 100.0) std::snprintf(s, sizeof(s), "%.0f%s%s", v, unit.empty() ? "" : " ", unit.c_str());
    else std::snprintf(s, sizeof(s), std::fabs(v) < 10.0 ? "%.2f%s%s" : "%.1f%s%s", v, unit.empty() ? "" : " ", unit.c_str());
    return s;
}

/** A length in a sound as time. */
std::string timeText(double frames, double sampleRate) {
    char s[32];
    const double seconds = sampleRate > 0 ? frames / sampleRate : 0.0;
    if (seconds < 1.0) std::snprintf(s, sizeof(s), "%.1f ms", 1000.0 * seconds);
    else std::snprintf(s, sizeof(s), "%.3f s", seconds);
    return s;
}

/** The instruments the user saved: text files next to the presets of the other plugins. */
struct Library {
    std::vector<std::string> names;
    hnl::presetfile::fs::path dir = hnl::presetfile::folder("HNL Sampler");

    static constexpr const char* kExt = ".hnlsampler";
    void rescan() {
        names.clear();
        if (dir.empty()) return;
        std::error_code ec;
        hnl::presetfile::fs::directory_iterator it(dir, ec), end;
        for (int seen = 0; !ec && it != end && seen < 2000; it.increment(ec), ++seen) {
            const std::string name = it->path().stem().u8string();
            if (it->path().extension().u8string() == kExt && !name.empty() && hnl::presetfile::cleanName(name) == name && names.size() < 45) names.push_back(name);
        }
        std::sort(names.begin(), names.end());
    }
    hnl::presetfile::fs::path file(const std::string& name) const { return dir / hnl::presetfile::fs::u8path(name + kExt); }
    bool save(const std::string& raw, const std::string& text, std::string& name) {
        name = hnl::presetfile::cleanName(raw);
        if (name.empty() || dir.empty()) return false;
        std::error_code ec;
        hnl::presetfile::fs::create_directories(dir, ec);
        const hnl::presetfile::fs::path temp = dir / hnl::presetfile::fs::u8path(name + ".tmp");
        std::FILE* f = hnl::presetfile::open(temp, true);
        if (f == nullptr) return false;
        const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        if (std::fclose(f) != 0 || !written) { hnl::presetfile::fs::remove(temp, ec); return false; }
        hnl::presetfile::fs::rename(temp, file(name), ec); // moved over the old one, so that a full disk never leaves half an instrument
        if (ec) { hnl::presetfile::fs::remove(temp, ec); return false; }
        rescan();
        return true;
    }
    static bool read(const hnl::presetfile::fs::path& file, std::string& text) {
        std::FILE* f = hnl::presetfile::open(file, false);
        if (f == nullptr) return false;
        text.assign(text::kMaxText + 1, '\0');
        text.resize(std::fread(&text[0], 1, text.size(), f));
        std::fclose(f);
        return text.size() <= text::kMaxText;
    }
};

struct Entry {
    std::string name;
    bool folder;
};

} // namespace

class SamplerUI : public UI {
public:
    SamplerUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        sampler_ = Sampler::of(getPluginInstancePointer());
#endif
        if (sampler_ == nullptr) { // no plugin to talk to (a preview of the drawing): an instrument of its own
            own_.reset(new Sampler());
            sampler_ = own_.get();
        }
        doc_ = sampler_->document();
        revision_ = sampler_->revision();
        library_.rescan();
        const char* home = std::getenv("USERPROFILE");
        if (home == nullptr) home = std::getenv("HOME");
        home_ = home != nullptr ? home : "/";
        folder_ = home_;
        std::string last; // where a sound was last taken from
        std::error_code ec;
        if (!library_.dir.empty() && Library::read(library_.dir / "last-folder.txt", last) && !last.empty() && last.size() < 2000 && fs::is_directory(fs::u8path(last), ec)) folder_ = last;
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        layout();
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount) return;
        // Within the parameter's range, whatever the host sends, as the plugin itself does.
        value = hnl::cleanParam(value, kParams[index].min, kParams[index].max, kParams[index].def);
        if (values_[index] == value) return;
        values_[index] = value;
        repaint();
    }
    void stateChanged(const char*, const char*) override {}

    /** The plugin's document may have changed without the UI: a sound was read, a song was opened. */
    void uiIdle() override {
        sampler_->idle();
        const bool look = skin_.poll(); // another HNL window chose a look
        const uint64_t now = sampler_->revision();
        const int zone = sampler_->engine.meters.zone.load(std::memory_order_relaxed);
        uint32_t keys = 0;
        for (int i = 0; i < 4; ++i) keys ^= sampler_->engine.meters.keys[i].load(std::memory_order_relaxed) * (i + 1);
        if (!look && now == revision_ && keys == lastKeys_ && zone == 0) return;
        lastKeys_ = keys;
        if (now != revision_) {
            refresh();
            if (drag_ < 0 && area_ == kAreaNone) layout(); // what a page offers depends on what is loaded
        }
        repaint();
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = static_cast<float>(getScaleFactor());
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme theme = k.theme();
        hnl::ui::faceplate(*this, k, UI_BASE_WIDTH, UI_BASE_HEIGHT);

        hnl::ui::title(*this, "SAMPLER", rgb(k.plateText), rgb(k.plateDim));
        std::string name = doc_->name;
        if (!note_.empty()) name = note_;
        else if (sampler_->loading() > 0) name += "  (reading " + std::to_string(sampler_->loading()) + ")";
        label("INSTRUMENT", kPresetX - 8, kHeaderY + 12, 7.5f, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        presetBar_.drawBar(*this, theme, name.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);
        drawHeaderMeters();

        for (int p = 0; p < kParts; ++p) {
            const Box b = partTab(p);
            const bool sounding = sampler_->engine.meters.partPeak[p].exchange(0.0f, std::memory_order_relaxed) > 1e-4f;
            const bool filled = !doc_->parts[p].zones.empty() || doc_->parts[p].v[kPOscLevel] > 0;
            const std::string number = std::to_string(p + 1);
            if (k.solid) { // a key with its light; beside the number a small light that shows the part sounding
                const float down = p == part_ ? 1.0f : 0.0f;
                hnl::ui::keys(*this, k, b, 1, p == part_ ? 0 : -1, [&number](int) { return number.c_str(); }, k.accent, k.text);
                beginPath();
                circle(b.x + b.w - 11, b.y + 15 + down, 3.5f);
                fillColor(rgb(0x000000, 0.55f));
                UI::fill();
                if (sounding) dot(b.x + b.w - 11, b.y + 15 + down, 2.5f, k.green);
                if (filled) fillBox({b.x + 7, b.y + b.h - 3.5f, b.w - 14, 1.5f}, k.keyText, 0, 0.55f);
                continue;
            }
            fillBox(b, p == part_ ? k.accent : k.track, 4);
            label(number.c_str(), b.x + b.w * 0.5f - (sounding ? 5 : 0), b.y + 12, 11, rgb(p == part_ ? k.bg : k.text), ALIGN_CENTER | ALIGN_MIDDLE);
            if (sounding) dot(b.x + b.w * 0.5f + 8, b.y + 12, 3, p == part_ ? k.bg : k.green);
            if (filled) fillBox({b.x + 6, b.y + b.h - 3, b.w - 12, 1.5f}, p == part_ ? k.bg : k.dim, 0);
        }
        for (int p = 0; p < kPageCount; ++p) {
            const Box b = pageTab(p);
            if (k.solid) {
                hnl::ui::keys(*this, k, b, 1, p == page_ ? 0 : -1, [p](int) { return kPageNames[p]; }, kWarm, k.text);
                continue;
            }
            fillBox(b, p == page_ ? kWarm : k.track, 4);
            label(kPageNames[p], b.x + b.w * 0.5f, b.y + 12, 10, rgb(p == page_ ? k.bg : k.text), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        for (const Titled& t : panels_) {
            if (t.glass) hnl::ui::displayPanel(*this, k, t.box);
            else hnl::ui::platePanel(*this, k, t.box);
            label(t.title.c_str(), t.box.x + 10, t.box.y + 11, 9, rgb(t.glass ? k.dim : k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        }
        switch (page_) {
        case kPageMap: drawMap(); break;
        case kPageSample: drawSample(); break;
        case kPageSynth: drawEnvelopes(); break;
        case kPageMod: drawSteps(); break;
        case kPageMix: drawStrips(); break;
        default: drawFiles(); break;
        }
        for (size_t i = 0; i < ctls_.size(); ++i) drawControl(ctls_[i], static_cast<int>(i) == drag_);
        for (const Button& b : buttons_) drawButton(b);
        drawNameEdit();
        drawKeyboard();
        drawMenu();
        std::vector<std::string> names = presetNames();
        presetBar_.drawMenu(*this, theme, static_cast<int>(names.size()), -1, 1, !library_.dir.empty() && names.size() < 46, [&names](int i) { return names[static_cast<size_t>(i)].c_str(); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && menu_.items.empty() && drag_ < 0) help_.drawTip(*this, theme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (!ev.press) return release();

        note_.clear();
        if (editKind_ != kEditNone) endNameEdit(true); // a click anywhere keeps what was typed
        if (!presetBar_.isBusy() && skin_.mouseDown(x, y) != hnl::ui::SkinMenu::kNotMine) { // the cog, or its list while it is open
            help_.hide();
            menu_.items.clear();
            repaint();
            return true;
        }
        if (!presetBar_.isBusy() && presetBar_.nameBox().contains(x, y)) library_.rescan();
        const std::vector<std::string> names = presetNames();
        const int picked = presetBar_.mouseDown(x, y, -1, static_cast<int>(names.size()), 1, !library_.dir.empty() && names.size() < 46);
        if (picked != hnl::ui::PresetBar::kNotMine) {
            help_.hide();
            presetAction(picked, names);
            repaint();
            return true;
        }
        if (!menu_.items.empty()) { // a list is open: this click chooses from it or closes it
            const int row = menuRow(x, y);
            const std::function<void(int)> chosen = menu_.chosen;
            menu_.items.clear();
            if (row >= 0 && chosen) chosen(row);
            repaint();
            return true;
        }
        if (help_.buttonHit(x, y)) {
            help_.on = !help_.on;
            help_.motion(x, y);
            repaint();
            return true;
        }
        for (int p = 0; p < kParts; ++p)
            if (partTab(p).contains(x, y)) { part_ = p; zone_ = 0; view0_ = view1_ = 0; layout(); repaint(); return true; }
        for (int p = 0; p < kPageCount; ++p)
            if (pageTab(p).contains(x, y)) { page_ = p; if (p == kPageFiles) readFolder(); layout(); repaint(); return true; }
        const bool twice = ev.time - lastClickTime_ < 350 && std::fabs(x - lastX_) < 4 && std::fabs(y - lastY_) < 4;
        lastClickTime_ = ev.time;
        lastX_ = x;
        lastY_ = y;

        if (kKeys.contains(x, y)) {
            play(keyAt(x, y), y);
            return true;
        }
        for (const Button& b : buttons_)
            if (b.box.contains(x, y)) {
                if (b.on) {
                    const std::function<void()> action = b.action; // a copy: the action may lay the page out anew
                    action();
                    refresh();
                    layout();
                }
                repaint();
                return true;
            }
        if (nameEditDown(x, y)) return true;
        for (size_t i = 0; i < ctls_.size(); ++i) {
            const Ctl& c = ctls_[i];
            if (!c.box().contains(x, y) || !usable(c)) continue;
            const Info* info = c.scope == kHost ? nullptr : &infoOf(c.scope)[static_cast<size_t>(c.id)];
            if (twice || (ev.mod & kModifierControl)) { // back to the default
                set(c, c.scope == kHost ? kParams[c.id].def : info->def, true);
                return true;
            }
            if (c.style == kChoiceBox) {
                std::vector<std::string> items;
                for (int k = 0; k <= static_cast<int>(info->max - info->min); ++k) items.push_back(info->choices[k + static_cast<int>(info->min)]);
                const Ctl copy = c;
                const double low = info->min;
                openMenu(c.x, c.y + 20, items, static_cast<int>(get(c) - low), [this, copy, low](int row) { set(copy, row + low, true); });
                return true;
            }
            if (c.style == kToggleBox) { set(c, get(c) >= 0.5 ? 0.0 : 1.0, true); return true; }
            drag_ = static_cast<int>(i);
            dragY_ = y;
            dragValue_ = get(c);
            dragNorm_ = c.scope == kHost ? paramToNorm(static_cast<uint32_t>(c.id), static_cast<float>(dragValue_)) : toNorm(*info, dragValue_);
            if (c.scope == kHost) editParameter(static_cast<uint32_t>(c.id), true);
            repaint();
            return true;
        }
        if (page_ == kPageMap && mapBox().contains(x, y)) return mapDown(x, y);
        if (page_ == kPageSample && waveBox().contains(x, y)) return waveDown(x);
        if (page_ == kPageMod && stepsBox().contains(x, y)) { area_ = kAreaSteps; stepDrag(x, y); return true; }
        if (page_ == kPageFiles && listBox().contains(x, y)) return fileDown(y, twice);
        return false;
    }

    bool onMotion(const MotionEvent& ev) override {
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (presetBar_.isOpen()) {
            if (presetBar_.motion(x, y, static_cast<int>(presetNames().size()))) repaint();
            return true;
        }
        if (skin_.isOpen()) {
            if (skin_.motion(x, y)) repaint();
            return true;
        }
        if (!menu_.items.empty()) {
            const int row = menuRow(x, y);
            if (row != menu_.hover) { menu_.hover = row; repaint(); }
            return true;
        }
        if (mouseKey_ >= 0) {
            if (kKeys.contains(x, y) && keyAt(x, y) != mouseKey_) play(keyAt(x, y), y);
            return true;
        }
        if (area_ == kAreaMap) { mapDrag(x, y); return true; }
        if (area_ == kAreaWave) { waveDrag(x); return true; }
        if (area_ == kAreaSteps) { stepDrag(x, y); return true; }
        if (drag_ < 0) {
            if (help_.motion(x, y)) repaint();
            return false;
        }
        const Ctl& c = ctls_[static_cast<size_t>(drag_)];
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        if (c.scope == kHost) {
            dragNorm_ = std::clamp(dragNorm_ + (dragY_ - y) / range, 0.0, 1.0);
            set(c, normToParam(static_cast<uint32_t>(c.id), static_cast<float>(dragNorm_)), false);
        } else {
            const Info& info = infoOf(c.scope)[static_cast<size_t>(c.id)];
            if (info.max - info.min > 100000.0) { // a place in a sound: a pixel is a hundredth of what the wave view shows
                const double span = view1_ > view0_ ? view1_ - view0_ : 48000.0;
                dragValue_ += (dragY_ - y) * span / ((ev.mod & kModifierShift) ? 20000.0 : 800.0);
                set(c, dragValue_, false);
            } else {
                dragNorm_ = std::clamp(dragNorm_ + (dragY_ - y) / range, 0.0, 1.0);
                set(c, fromNorm(info, dragNorm_), false);
            }
        }
        dragY_ = y;
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (presetBar_.isBusy() || skin_.isOpen() || drag_ >= 0 || !menu_.items.empty()) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        const float dy = static_cast<float>(ev.delta.getY());
        if (page_ == kPageFiles && listBox().contains(x, y)) {
            scroll_ = std::clamp(scroll_ - static_cast<int>(dy > 0 ? 3 : -3), 0, std::max(0, static_cast<int>(entries_.size()) - 5));
            repaint();
            return true;
        }
        if (page_ == kPageSample && waveBox().contains(x, y)) { // closer to or further from the place under the mouse
            const AudioData* d = zoneData();
            if (d == nullptr) return false;
            const double frames = static_cast<double>(d->frames()), at = view0_ + (x - waveBox().x) / waveBox().w * (view1_ - view0_);
            const double span = std::clamp((view1_ - view0_) * (dy > 0 ? 0.7 : 1.0 / 0.7), 64.0, frames);
            view0_ = std::clamp(at - (x - waveBox().x) / waveBox().w * span, 0.0, frames - span);
            view1_ = view0_ + span;
            repaint();
            return true;
        }
        for (const Ctl& c : ctls_) {
            if (!c.box().contains(x, y) || c.style != kKnob || !usable(c)) continue;
            const double step = ((ev.mod & kModifierShift) ? 0.004 : 0.02) * dy;
            if (c.scope == kHost) set(c, normToParam(static_cast<uint32_t>(c.id), paramToNorm(static_cast<uint32_t>(c.id), static_cast<float>(get(c))) + static_cast<float>(step)), true);
            else set(c, fromNorm(infoOf(c.scope)[static_cast<size_t>(c.id)], toNorm(infoOf(c.scope)[static_cast<size_t>(c.id)], get(c)) + step), true);
            return true;
        }
        return false;
    }

    bool onKeyboard(const KeyboardEvent& ev) override {
        if (!ev.press) return presetBar_.isEditing() || editKind_ != kEditNone;
        if (ev.key == 0x1B && !menu_.items.empty()) { menu_.items.clear(); repaint(); return true; }
        if (editKind_ != kEditNone) { // a name being typed
            if (ev.key == 0x1B) endNameEdit(false);
            else if (ev.key == 0x0D) endNameEdit(true);
            else if (ev.key == 0x08 && !editText_.empty()) editText_.pop_back();
            repaint();
            return true;
        }
        const int result = presetBar_.key(ev.key);
        if (result == hnl::ui::PresetBar::kNotMine) return false;
        presetAction(result, presetNames());
        repaint();
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override {
        if (editKind_ != kEditNone) { // plain letters, figures and signs
            const unsigned code = ev.string[0] != '\0' && ev.string[1] == '\0' ? static_cast<unsigned char>(ev.string[0]) : (ev.string[0] == '\0' ? ev.character : 0u);
            if (code >= 0x20 && code < 0x7F && editText_.size() < 16) editText_ += static_cast<char>(code);
            repaint();
            return true;
        }
        if (!presetBar_.character(ev.string, ev.character)) return false;
        repaint();
        return true;
    }

private:
    struct Titled { Box box; std::string title; bool glass = false; }; // glass: a display, where the solid looks have a window
    struct Button { Box box; std::string text; std::function<void()> action; bool on = true, main = false; };
    enum EditKind { kEditNone, kEditMacro, kEditPart };
    struct Menu {
        std::vector<std::string> items;
        float x = 0, y = 0;
        int current = -1, hover = -1;
        std::function<void(int)> chosen;
    };
    enum Area { kAreaNone, kAreaMap, kAreaWave, kAreaSteps };

    // ------------------------------------------------------------ document

    void refresh() {
        doc_ = sampler_->document();
        revision_ = sampler_->revision();
        const int count = static_cast<int>(doc_->parts[part_].zones.size());
        if (zone_ >= count) zone_ = std::max(0, count - 1);
    }
    const Part& part() const { return doc_->parts[part_]; }
    const Zone* zone() const { return zone_ < static_cast<int>(part().zones.size()) ? &part().zones[static_cast<size_t>(zone_)] : nullptr; }
    const AudioData* zoneData() const {
        const Zone* z = zone();
        const Sample* s = z != nullptr ? doc_->findSample(z->sample) : nullptr;
        return s != nullptr ? s->data.get() : nullptr;
    }
    bool usable(const Ctl& c) const { return c.scope != kOfZone || zone() != nullptr; }

    double get(const Ctl& c) const {
        switch (c.scope) {
        case kHost: return values_[c.id];
        case kOfPart: return doc_->parts[c.part < 0 ? part_ : c.part].v[c.id];
        case kOfZone: return zone() != nullptr ? zone()->v[c.id] : zoneInfo()[static_cast<size_t>(c.id)].def;
        default: return doc_->g[c.id];
        }
    }

    void set(const Ctl& c, double value, bool gesture) {
        if (c.scope == kHost) {
            const uint32_t id = static_cast<uint32_t>(c.id);
            if (gesture) editParameter(id, true);
            values_[id] = static_cast<float>(value);
            setParameterValue(id, values_[id]);
            if (gesture) editParameter(id, false);
        } else {
            const int p = c.part < 0 ? part_ : c.part, z = zone_, id = c.id, scope = c.scope;
            const double clean = cleanValue(infoOf(scope)[static_cast<size_t>(id)], value);
            sampler_->edit([=](Instrument& doc) {
                if (scope == kOfPart) doc.parts[p].v[id] = clean;
                else if (scope == kShared) doc.g[id] = clean;
                else if (z < static_cast<int>(doc.parts[p].zones.size())) {
                    Zone& zn = doc.parts[p].zones[static_cast<size_t>(z)];
                    zn.v[id] = clean;
                    // A range keeps its low end at or under its high end.
                    if (id == kZLoKey && zn.v[kZHiKey] < clean) zn.v[kZHiKey] = clean;
                    if (id == kZHiKey && zn.v[kZLoKey] > clean) zn.v[kZLoKey] = clean;
                    if (id == kZLoVel && zn.v[kZHiVel] < clean) zn.v[kZHiVel] = clean;
                    if (id == kZHiVel && zn.v[kZLoVel] > clean) zn.v[kZLoVel] = clean;
                }
            });
            refresh();
        }
        repaint();
    }

    void editZone(const std::function<void(Zone&, Instrument&)>& change) {
        const int p = part_, z = zone_;
        sampler_->edit([&](Instrument& doc) {
            if (z < static_cast<int>(doc.parts[p].zones.size())) change(doc.parts[p].zones[static_cast<size_t>(z)], doc);
        });
        refresh();
    }

    bool release() {
        if (mouseKey_ >= 0) { play(-1, 0); return true; }
        if (area_ != kAreaNone) { area_ = kAreaNone; repaint(); return true; }
        if (drag_ < 0) return false;
        if (ctls_[static_cast<size_t>(drag_)].scope == kHost) editParameter(static_cast<uint32_t>(ctls_[static_cast<size_t>(drag_)].id), false);
        drag_ = -1;
        repaint();
        return true;
    }

    /** Lets go of the key the mouse holds and presses `key` (-1 for none) on the chosen part's channel. */
    void play(int key, float y) {
        const int wanted = static_cast<int>(part().v[kPChannel]);
        const uint8_t channel = static_cast<uint8_t>(wanted > 0 ? wanted - 1 : 0);
        if (mouseKey_ >= 0) sendNote(channel, static_cast<uint8_t>(mouseKey_), 0);
        mouseKey_ = key < 0 ? -1 : std::clamp(key, 0, 127);
        if (mouseKey_ >= 0) {
            const float t = std::clamp((y - kKeys.y) / kKeys.h, 0.0f, 1.0f);
            sendNote(channel, static_cast<uint8_t>(mouseKey_), static_cast<uint8_t>(30.0f + 97.0f * t));
        }
        repaint();
    }

    // -------------------------------------------------------------- layout

    /** A panel with a row of controls in it; returns the x where the next panel can start. */
    float panel(const char* title, const char* helpText, float x, float y, float h, int scope, std::initializer_list<std::pair<int, int>> items, float pitch = 46) {
        const float w = pitch * static_cast<float>(items.size()) + 14;
        panels_.push_back({{x, y, w, h}, title});
        help_.add({x, y, w, h}, title, helpText);
        float cx = x + 7 + pitch * 0.5f;
        for (const std::pair<int, int>& it : items) {
            add(scope, it.first, it.second, it.second == kKnob ? cx : cx - pitch * 0.5f + 3, it.second == kKnob ? y + h - 40 : y + h - 46, pitch - (it.second == kKnob ? 0 : 6));
            cx += pitch;
        }
        return x + w + 6;
    }
    void add(int scope, int id, int style, float x, float y, float w, int ofPart = -1, const char* text = nullptr) {
        std::string name = text != nullptr ? text : (scope == kHost ? kParams[id].name : infoOf(scope)[static_cast<size_t>(id)].name);
        // Names too long for the room a control has.
        static const char* const kShort[][2] = {{"Bend range", "Bend"}, {"Transpose", "Transp"}, {"Loop start", "Loop st"}, {"Loop length", "Loop len"}, {"Filter env", "Env"},
                                                {"Filter velocity", "Velocity"}, {"Pulse width", "Width"}, {"Low velocity", "Low vel"}, {"High velocity", "High vel"},
                                                {"Round robin of", "RR count"}, {"Round robin turn", "RR turn"}, {"Off by", "Choked by"}, {"Loop in release", "In release"}, {"Step length", "Length"},
                                                {"Step smooth", "Smooth"}, {"Sample level", "Sample"}};
        for (const auto& pair : kShort)
            if (text == nullptr && (scope == kOfPart || scope == kOfZone) && name == pair[0]) name = pair[1];
        // Inside a panel the panel's title says which envelope or LFO it is.
        for (const char* lead : {"Env 1 ", "Env 2 ", "Env 3 ", "LFO 1 ", "LFO 2 ", "LFO 3 ", "Osc ", "Mod ", "Delay ", "Reverb ", "Chorus ", "Noise "})
            if (text == nullptr && name.compare(0, std::strlen(lead), lead) == 0 && name.size() > std::strlen(lead)) name = name.substr(std::strlen(lead));
        ctls_.push_back({scope, id, style, x, y, w, upper(name), ofPart});
    }

    void layout() {
        ctls_.clear();
        panels_.clear();
        buttons_.clear();
        help_.clear();
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK", hnl::ui::SkinMenu::help());
        help_.add(presetBar_.nameBox(), "INSTRUMENT", help::kInstrument);
        help_.add({12, 54, 448, 24}, "PARTS", help::kParts);
        help_.add({532, 54, 456, 24}, "PAGES", help::kPages);
        help_.add({kKeys.x, kRange.y, kKeys.w, kKeys.h + 12}, "KEYBOARD", help::kKeyboard);
        help_.add({kMeterX - 10, 8, 150, 36}, "OUTPUT", help::kOutput);
        add(kHost, kVolume, kKnob, kVolumeX, 27, 36);
        help_.add({kVolumeX - 70, 8, 90, 40}, "VOLUME", help::kVolume);
        const float x0 = kPageArea.x, y0 = kPageArea.y;
        const int K = kKnob, C = kChoiceBox, T = kToggleBox, N = kNumber;
        if (page_ == kPageSynth) {
            const float h = kRowH;
            float x = panel("OSCILLATOR", help::kOsc, x0, y0, h, kOfPart, {{kPOscWave, C}, {kPOscLevel, K}, {kPOscCoarse, K}, {kPOscFine, K}, {kPOscWidth, K}}, 52);
            x = panel("NOISE", help::kNoise, x, y0, h, kOfPart, {{kPNoiseLevel, K}, {kPNoiseColour, C}}, 52);
            x = panel("MOD OSCILLATOR", help::kModOsc, x, y0, h, kOfPart, {{kPModMode, C}, {kPModRatio, K}, {kPModFine, K}, {kPModAmount, K}}, 52);
            panel("UNISON AND CHORD", help::kUnison, x, y0, h, kOfPart, {{kPUnison, K}, {kPDetune, K}, {kPSpread, K}, {kPChord1, K}, {kPChord2, K}, {kPChord3, K}}, 52);
            x = panel("SATURATION", help::kSat, x0, y0 + h + 6, h, kOfPart, {{kPSatType, C}, {kPDrive, K}}, 52);
            x = panel("FILTER", help::kFilter, x, y0 + h + 6, h, kOfPart,
                      {{kPFilterType, C}, {kPCutoff, K}, {kPResonance, K}, {kPMorph, K}, {kPKeyTrack, K}, {kPFilterEnv, K}, {kPFilterVel, K}}, 52);
            x = panel("PLAYING", help::kPlay, x, y0 + h + 6, h, kOfPart, {{kPMode, C}, {kPGlide, K}, {kPBend, K}, {kPVoices, K}, {kPVelocity, K}, {kPTranspose, K}, {kPFine, K}}, 43.4f);
            // These three shift what the SAMPLE page sets for each zone, for the whole part, and the matrix can move them.
            panel("START AND LOOP SHIFT", help::kShift, x, y0 + h + 6, h, kOfPart, {{kPStart, K}, {kPLoopStart, K}, {kPLoopLength, K}}, 43.4f);
            x = x0;
            for (int e = 0; e < kEnvs; ++e) {
                static const char* const kTitles[kEnvs] = {"ENVELOPE 1: LEVEL", "ENVELOPE 2: FILTER", "ENVELOPE 3: FREE, FOR THE MATRIX"};
                x = panel(kTitles[e], help::kEnv, x, y0 + 2 * (h + 6), 480 - 2 * (h + 6), kOfPart,
                          {{envParam(e, 0), K}, {envParam(e, 1), K}, {envParam(e, 2), K}, {envParam(e, 3), K}, {envParam(e, 4), K}, {envParam(e, 5), K}, {envParam(e, 6), K}}, 43.6f);
            }
        } else if (page_ == kPageMod) {
            float x = x0;
            for (int l = 0; l < kLfos; ++l) {
                static const char* const kTitles[kLfos] = {"LFO 1", "LFO 2", "LFO 3"};
                x = panel(kTitles[l], help::kLfo, x, y0, kRowH, kOfPart,
                          {{lfoParam(l, kLfoWave), C}, {lfoParam(l, kLfoRate), K}, {lfoParam(l, kLfoSync), C}, {lfoParam(l, kLfoFade), K}, {lfoParam(l, kLfoPhase), K}, {lfoParam(l, kLfoRetrig), C}}, 51.0f);
            }
            const float sy = y0 + kRowH + 6, my = matrixY();
            x = panel("STEPS", help::kSteps, x0, sy, 112, kOfPart, {}, 52) + 3 * 52; // its three controls a little higher than a row's, in the middle of the taller panel
            panels_.back().box.w += 3 * 52;
            help_.add(panels_.back().box, "STEPS", help::kSteps);
            add(kOfPart, kPSeqSteps, K, x0 + 7 + 26, sy + 62, 52);
            add(kOfPart, kPSeqSync, C, x0 + 7 + 52 + 3, sy + 56, 46);
            add(kOfPart, kPSeqSmooth, K, x0 + 7 + 130, sy + 62, 52);
            panels_.push_back({{x, sy, 392, 112}, "", true});
            help_.add(stepsBox(), "STEPS", help::kSteps);
            panels_.push_back({{x + 398, sy, 976 - (x + 398 - x0), 112}, "MACROS"});
            help_.add(panels_.back().box, "MACROS", help::kMacros);
            for (int m = 0; m < kMacros; ++m) add(kHost, kMacro1 + m, kKnob, macroX(m), sy + 62, 44, -1, doc_->macroNames[m].empty() ? nullptr : doc_->macroNames[m].c_str());
            panels_.push_back({{x0, my, 976, y0 + 480 - my}, "MATRIX"});
            help_.add(panels_.back().box, "MATRIX", help::kMatrix);
            for (int s = 0; s < kSlots; ++s) {
                const float rx = x0 + 60 + (s / 6) * 480.0f, ry = my + 32 + (s % 6) * 37.0f;
                add(kOfPart, slotParam(s, kSlotSource), C, rx, ry, 110, -1, "");
                add(kOfPart, slotParam(s, kSlotVia), C, rx + 116, ry, 110, -1, "");
                add(kOfPart, slotParam(s, kSlotDest), C, rx + 232, ry, 110, -1, "");
                add(kOfPart, slotParam(s, kSlotAmount), N, rx + 348, ry, 60, -1, "");
            }
        } else if (page_ == kPageMix) {
            for (int p = 0; p < kParts; ++p) {
                const float x = x0 + p * 92.0f;
                panels_.push_back({{x, y0, 88, 480}, "PART " + std::to_string(p + 1)});
                add(kOfPart, kPLevel, K, x + 28, y0 + 60, 44, p);
                add(kOfPart, kPPan, K, x + 28, y0 + 124, 44, p);
                add(kOfPart, kPDelaySend, K, x + 28, y0 + 188, 44, p, "DELAY");
                add(kOfPart, kPReverbSend, K, x + 28, y0 + 252, 44, p, "REVERB");
                add(kOfPart, kPChorusSend, K, x + 28, y0 + 316, 44, p, "CHORUS");
                add(kOfPart, kPChannel, C, x + 6, y0 + 366, 76, p);
                add(kOfPart, kPOutput, C, x + 6, y0 + 400, 76, p);
                add(kOfPart, kPMute, T, x + 6, y0 + 430, 76, p);
                add(kOfPart, kPMpe, T, x + 6, y0 + 454, 76, p);
            }
            help_.add({x0, y0, 732, 480}, "PARTS", help::kMix);
            const float fx = x0 + 740;
            panels_.push_back({{fx, y0, 236, 190}, "DELAY"});
            help_.add(panels_.back().box, "DELAY", help::kFx);
            add(kShared, kGDelayTime, K, fx + 40, y0 + 62, 60);
            add(kShared, kGDelaySync, C, fx + 80, y0 + 54, 70);
            add(kShared, kGDelayFeedback, K, fx + 196, y0 + 62, 60);
            add(kShared, kGDelayTone, K, fx + 40, y0 + 140, 60);
            add(kShared, kGDelayWidth, K, fx + 118, y0 + 140, 60);
            add(kShared, kGDelayLevel, K, fx + 196, y0 + 140, 60);
            panel("REVERB", help::kFx, fx, y0 + 196, 100, kShared, {{kGReverbDecay, K}, {kGReverbDamp, K}, {kGReverbLevel, K}}, 74);
            panel("CHORUS", help::kFx, fx, y0 + 302, 100, kShared, {{kGChorusRate, K}, {kGChorusDepth, K}, {kGChorusLevel, K}}, 74);
            panels_.push_back({{fx, y0 + 408, 236, 72}, "TUNING"});
            help_.add(panels_.back().box, "TUNING", help::kTuning);
            buttons_.push_back({{fx + 170, y0 + 438, 56, 22}, "EQUAL", [this] {
                sampler_->edit([](Instrument& doc) { for (double& t : doc.tuning) t = 0.0; doc.tuningName.clear(); });
            }});
        } else if (page_ == kPageMap) {
            const float py = y0 + mapBox().h + 6;
            panels_.push_back({{x0, py, 976, 480 - mapBox().h - 6}, "ZONE"});
            help_.add(mapBox(), "ZONES", help::kMap);
            help_.add(panels_.back().box, "ZONE", help::kZone);
            static const int kIds[14] = {kZLoKey, kZHiKey, kZLoVel, kZHiVel, kZRoot, kZTune, kZKeyTrack, kZGain, kZPan, kZGroup, kZOffBy, kZSeqLength, kZSeqPosition, kZTrigger};
            for (int i = 0; i < 14; ++i) add(kOfZone, kIds[i], kIds[i] == kZTrigger ? C : N, x0 + 10 + i * 59.0f, py + 38, 54);
            const bool any = zone() != nullptr;
            buttons_.push_back({{x0 + 10 + 14 * 59.0f, py + 36, 62, 22}, "COPY", [this] {
                const int p = part_, z = zone_;
                sampler_->edit([=](Instrument& doc) {
                    std::vector<Zone>& zones = doc.parts[p].zones;
                    if (z >= static_cast<int>(zones.size()) || static_cast<int>(zones.size()) >= kMaxPartZones) return;
                    Zone copy = zones[static_cast<size_t>(z)];
                    copy.id = doc.nextId++;
                    zones.push_back(copy);
                });
                refresh();
                zone_ = std::max(0, static_cast<int>(part().zones.size()) - 1);
            }, any});
            buttons_.push_back({{x0 + 10 + 14 * 59.0f + 66, py + 36, 62, 22}, "DELETE", [this] {
                const int p = part_, z = zone_;
                sampler_->edit([=](Instrument& doc) {
                    if (z < static_cast<int>(doc.parts[p].zones.size())) doc.parts[p].zones.erase(doc.parts[p].zones.begin() + z);
                    doc.dropUnusedSamples();
                });
            }, any});
            if (!any) addSoundButton(mapBox());
        } else if (page_ == kPageSample) {
            panels_.push_back({{x0, y0 + 372, 976, 108}, "SOUND"});
            help_.add(waveBox(), "SOUND", help::kWave);
            help_.add({x0, y0 + 372, 600, 108}, "START, END AND LOOP", help::kLoop);
            help_.add({x0 + 600, y0 + 372, 376, 108}, "TOOLS", help::kTools);
            static const int kIds[9] = {kZStart, kZEnd, kZLoop, kZLoopStart, kZLoopEnd, kZCrossfade, kZLoopRelease, kZReverse, kZOneShot};
            for (int i = 0; i < 9; ++i) add(kOfZone, kIds[i], kIds[i] == kZLoop ? C : (i >= 6 ? T : N), x0 + 10 + i * 68.0f, y0 + 408, 62);
            const float bx = x0 + 632;
            const bool any = zoneData() != nullptr; // the tools need a sound that has been read
            buttons_.push_back({{bx, y0 + 396, 84, 22}, "FIND LOOP", [this] { findLoopHere(); }, any});
            buttons_.push_back({{bx + 90, y0 + 396, 84, 22}, "DETECT PITCH", [this] { detectPitchHere(); }, any});
            buttons_.push_back({{bx, y0 + 422, 84, 22}, "NORMALISE", [this] { normaliseHere(); }, any});
            buttons_.push_back({{bx + 90, y0 + 422, 84, 22}, "SLICE AT HITS", [this] { sliceHere(); }, any});
            buttons_.push_back({{bx + 180, y0 + 396, 84, 22}, "SHOW ALL", [this] { view0_ = view1_ = 0; }, any});
            if (zone() == nullptr) addSoundButton(waveBox());
        } else {
            panels_.push_back({listBox(), ""});
            panels_.push_back({{x0 + 640, y0, 336, 480}, "CHOSEN"});
            help_.add(listBox(), "FILES", help::kFiles);
            help_.add(panels_.back().box, "CHOSEN FILE", help::kFileActions);
            const float bx = x0 + 652;
            const int kind = chosenKind();
            if (kind == kFileSound) {
                buttons_.push_back({{bx, y0 + 150, 150, 24}, "ADD AS ZONE", [this] { useFile(false); }, true, true});
                buttons_.push_back({{bx + 158, y0 + 150, 150, 24}, "ADD ON NEXT KEY", [this] { useFile(true); }});
                buttons_.push_back({{bx, y0 + 180, 150, 24}, "STOP PREVIEW", [this] { sampler_->preview(""); }});
            } else if (kind != kFileNone) {
                const std::string text = kind == kFileImport ? "IMPORT INTO PART " + std::to_string(part_ + 1) : (kind == kFileTuning ? "USE THIS TUNING" : "LOAD INSTRUMENT");
                buttons_.push_back({{bx, y0 + 150, 150, 24}, text, [this] { useFile(false); }, true, true});
            }
            buttons_.push_back({{x0 + 6, y0 + 6, 40, 20}, "UP", [this] {
                const fs::path up = fs::u8path(folder_).parent_path();
                folder_ = up.empty() || up.u8string() == folder_ ? "" : up.u8string(); // above a drive: the list of drives
                readFolder();
            }});
            buttons_.push_back({{x0 + 50, y0 + 6, 46, 20}, "HOME", [this] { folder_ = home_; readFolder(); }});
#ifdef _WIN32
            buttons_.push_back({{x0 + 100, y0 + 6, 52, 20}, "DRIVES", [this] { folder_.clear(); readFolder(); }});
#else
            buttons_.push_back({{x0 + 100, y0 + 6, 52, 20}, "ROOT", [this] { folder_ = "/"; readFolder(); }});
#endif
        }
    }

    /** In the middle of an empty map or wave view: the way to the files. */
    void addSoundButton(const Box& in) {
        buttons_.push_back({{in.x + in.w * 0.5f - 70, in.y + in.h * 0.5f + 12, 140, 26}, "ADD A SOUND", [this] { page_ = kPageFiles; readFolder(); }, true, true});
    }
    float matrixY() const { return kPageArea.y + kRowH + 6 + 112 + 6; }
    float macroX(int m) const { return kPageArea.x + 176 + 398 + 30 + m * 46.5f; }
    static constexpr float kRowH = 92; // a panel with one row of knobs

    // ------------------------------------------------------------- drawing

    /** The look: in every one of them what is on or lit has this plugin's own two colours. */
    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }
    /** The dark of what is written on something lit. */
    static uint32_t inkOn(const hnl::ui::Skin& k) { return k.solid ? k.well : k.bg; }
    /** A word of the plugin about what it did: in the second colour, or printed where the plate is solid. */
    static uint32_t noteOn(const hnl::ui::Skin& k) { return k.solid ? k.plateText : kWarm; }
    /** Where a graph or a list is shown: a dark box, or a window in the plate. */
    void window(const Box& b, float radius) {
        const hnl::ui::Skin k = skin();
        if (k.solid) hnl::ui::displayPanel(*this, k, b);
        else fillBox(b, k.well, radius);
    }
    /** A line of the grid behind a graph. */
    void gridLine(float x0, float y0, float x1, float y1) {
        const hnl::ui::Skin k = skin();
        line(x0, y0, x1, y1, k.solid ? k.dim : k.panelEdge, 1.0f, k.solid ? 0.3f : 1.0f);
    }

    void drawButton(const Button& b) {
        const hnl::ui::Skin k = skin();
        if (!k.solid) {
            fillBox(b.box, b.main ? k.accent : k.track, 4, b.on ? 1.0f : 0.45f);
            label(b.text.c_str(), b.box.x + b.box.w * 0.5f, b.box.y + b.box.h * 0.5f, 9, rgb(b.main ? k.bg : k.text, b.on ? 1.0f : 0.4f), ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }
        if (b.main) { // the one to press: a key with its light on
            hnl::ui::keys(*this, k, b.box, 1, 0, [&b](int) { return b.text.c_str(); }, k.accent, k.text);
        } else {
            hnl::ui::button(*this, k, b.box, "");
            label(b.text.c_str(), b.box.x + b.box.w * 0.5f, b.box.y + b.box.h * 0.5f, 9, rgb(k.keyText), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        if (!b.on) fillBox(b.box, k.bg, 4, 0.6f); // not to be pressed now
    }

    void fillBox(const Box& b, uint32_t colour, float radius, float alpha = 1.0f) {
        beginPath();
        if (radius > 0) roundedRect(b.x, b.y, b.w, b.h, radius);
        else rect(b.x, b.y, b.w, b.h);
        fillColor(rgb(colour, alpha));
        UI::fill();
    }
    void edge(const Box& b, uint32_t colour, float radius, float width = 1.0f) {
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, radius);
        strokeColor(rgb(colour));
        strokeWidth(width);
        stroke();
    }
    void dot(float x, float y, float r, uint32_t colour) {
        beginPath();
        circle(x, y, r);
        fillColor(rgb(colour));
        UI::fill();
    }
    void line(float x0, float y0, float x1, float y1, uint32_t colour, float width = 1.0f, float alpha = 1.0f) {
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strokeColor(rgb(colour, alpha));
        strokeWidth(width);
        stroke();
    }
    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }
    /** Text cut to a width, with two dots where it was cut. */
    void fitted(std::string text, float x, float y, float size, float width, const Color& colour, int align = ALIGN_LEFT | ALIGN_MIDDLE) {
        const size_t most = static_cast<size_t>(std::max(2.0f, width / (0.58f * size)));
        if (text.size() > most) {
            size_t cut = most - 2;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut; // not in the middle of a character
            text = text.substr(0, cut) + "..";
        }
        label(text.c_str(), x, y, size, colour, align);
    }

    void drawControl(const Ctl& c, bool dragging) {
        const bool on = usable(c);
        const double v = get(c);
        const Info* info = c.scope == kHost ? nullptr : &infoOf(c.scope)[static_cast<size_t>(c.id)];
        const bool isKey = c.scope == kOfZone && (c.id == kZLoKey || c.id == kZHiKey || c.id == kZRoot);
        std::string text;
        if (c.scope == kHost) {
            char s[32];
            std::snprintf(s, sizeof(s), c.id == kVolume ? "%.1f dB" : "%.0f %%", v);
            text = s;
        } else {
            text = valueText(*info, v, isKey);
            if (c.scope == kOfZone && (c.id == kZEnd || c.id == kZLoopEnd) && v == 0.0) text = "end";
        }
        const hnl::ui::Skin k = skin();
        const Color dim = rgb(k.plateDim, on ? 1.0f : 0.4f), bright = rgb(k.plateText, on ? 1.0f : 0.4f);
        const Color moving = rgb(hnl::ui::printed(k, k.accent)); // the value of what is being dragged
        if (c.style == kKnob) {
            const float a0 = 0.75f * kPi, a1 = 2.25f * kPi, r = 13;
            const float norm = c.scope == kHost ? paramToNorm(static_cast<uint32_t>(c.id), static_cast<float>(v)) : static_cast<float>(toNorm(*info, v));
            const double lo = c.scope == kHost ? kParams[c.id].min : info->min, hi = c.scope == kHost ? kParams[c.id].max : info->max;
            const bool centred = lo < 0 && hi > 0 && lo == -hi; // centred on zero: from the middle
            const float angle = a0 + norm * (a1 - a0), from = centred ? a0 + 0.5f * (a1 - a0) : a0;
            const uint32_t own = c.scope == kHost ? kWarm : k.accent; // the host's own knobs in the second colour
            if (k.solid) {
                hnl::ui::knob(*this, k, c.x, c.y, r - 1.5f, norm, centred ? 0.5f : 0.0f, hnl::ui::printed(k, own)); // a little smaller, so that a printed scale stays clear of the name above it
            } else { // this plugin's small flat knob: a thinner arc and a larger cap than the shared one
                beginPath();
                arc(c.x, c.y, r, a0, a1, CW);
                strokeColor(rgb(k.track));
                strokeWidth(3);
                lineCap(ROUND);
                stroke();
                if (std::fabs(angle - from) > 0.01f) {
                    beginPath();
                    arc(c.x, c.y, r, std::min(from, angle), std::max(from, angle), CW);
                    strokeColor(rgb(own, on ? 1.0f : 0.4f));
                    strokeWidth(3);
                    stroke();
                }
                dot(c.x, c.y, r - 5, k.knobLo);
                line(c.x + std::cos(angle) * (r - 10), c.y + std::sin(angle) * (r - 10), c.x + std::cos(angle) * (r - 5.5f), c.y + std::sin(angle) * (r - 5.5f), k.pointer, 1.6f);
            }
            if (c.scope == kHost && c.id == kVolume) { // in the header: name and value beside the knob
                label("VOLUME", c.x - 20, c.y - 6, 7.5f, dim, ALIGN_RIGHT | ALIGN_MIDDLE);
                label(text.c_str(), c.x - 20, c.y + 6, 8, dragging ? moving : bright, ALIGN_RIGHT | ALIGN_MIDDLE);
                return;
            }
            if (!(editKind_ == kEditMacro && c.scope == kHost && c.id == static_cast<int>(kMacro1) + editIndex_)) fitted(c.label, c.x, c.y - 21, 7.5f, c.w + 4, dim, ALIGN_CENTER | ALIGN_MIDDLE);
            label(text.c_str(), c.x, c.y + 23, 8, dragging ? moving : bright, ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }
        if (!c.label.empty() && c.style != kToggleBox) fitted(c.label, c.x + 1, c.y - 7, 7.5f, c.w + 6, dim);
        const Box b = c.face();
        const bool lit = c.style == kToggleBox && v >= 0.5 && on;
        if (c.style == kToggleBox && !c.label.empty()) text = c.label; // lit when on
        const float room = c.style == kChoiceBox ? b.w - 13 : b.w; // a list has its arrow at the right
        const float tx = b.x + room * 0.5f + (c.style == kChoiceBox ? 1 : 0), size = c.style == kChoiceBox && b.w < 60 ? 8.5f : 9;
        if (k.solid) { // a key with a light, a key that opens a list, or a figure in a small display
            if (c.style == kToggleBox) {
                hnl::ui::keys(*this, k, b, 1, lit ? 0 : -1, [&text](int) { return text.c_str(); }, k.accent, k.text);
            } else if (c.style == kChoiceBox) {
                hnl::ui::button(*this, k, b, "");
                fitted(text, tx, b.y + 9.5f, size, room + 4, rgb(k.keyText), ALIGN_CENTER | ALIGN_MIDDLE);
                hnl::ui::triangle(*this, b.x + b.w - 10, b.y + 7, b.x + b.w - 4, b.y + 7, b.x + b.w - 7, b.y + 11, rgb(k.keyText, 0.6f));
            } else {
                hnl::ui::displayPanel(*this, k, b);
                fitted(text, tx, b.y + 9.5f, size, room + 4, dragging ? rgb(k.accent) : rgb(k.text, on ? 1.0f : 0.4f), ALIGN_CENTER | ALIGN_MIDDLE);
            }
            if (!on && c.style != kNumber) fillBox(b, k.bg, 3, 0.6f); // not to be pressed now
            return;
        }
        fillBox(b, lit ? k.accent : (c.style == kNumber ? k.well : k.track), 3, on ? 1.0f : 0.5f);
        fitted(text, tx, b.y + 9.5f, size, room + 4, lit ? rgb(k.bg) : (dragging ? moving : bright), ALIGN_CENTER | ALIGN_MIDDLE);
        if (c.style == kChoiceBox) hnl::ui::triangle(*this, b.x + b.w - 10, b.y + 7, b.x + b.w - 4, b.y + 7, b.x + b.w - 7, b.y + 11, dim);
    }

    void drawHeaderMeters() {
        const hnl::ui::Skin k = skin();
        if (k.solid) hnl::ui::displayPanel(*this, k, kOutputWindow);
        for (int c = 0; c < 2; ++c) {
            const float y = 20.0f + c * 9.0f, x = kMeterX, w = 100;
            const float t = std::clamp((values_[kMeterOutL + c] + 48.0f) / 54.0f, 0.0f, 1.0f);
            fillBox({x, y, w, 6}, k.solid ? k.track : k.well, 0); // solid: the segments that are not lit
            fillBox({x, y, w * std::min(t, 36.0f / 54.0f), 6}, k.green, 0);
            if (t > 36.0f / 54.0f) fillBox({x + w * 36.0f / 54.0f, y, w * (std::min(t, 45.0f / 54.0f) - 36.0f / 54.0f), 6}, k.yellow, 0);
            if (t > 45.0f / 54.0f) fillBox({x + w * 45.0f / 54.0f, y, w * (t - 45.0f / 54.0f), 6}, k.red, 0);
            hnl::ui::segments(*this, k, x, y, w, 6);
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%.0f of 64", values_[kMeterVoices]);
        label("VOICES", kVoicesX, 21, 7.5f, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        label(text, kVoicesX, 33, 8, rgb(values_[kMeterVoices] > 0 ? k.accent : k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawKeyboard() {
        const hnl::ui::Skin k = skin();
        fillBox(kRange, k.well, 2);
        if (const Zone* z = zone())
            fillBox({kKeys.x + static_cast<float>(z->v[kZLoKey]) * kKeyW, kRange.y, static_cast<float>(z->v[kZHiKey] - z->v[kZLoKey] + 1) * kKeyW, kRange.h}, k.accent, 2);
        if (k.solid) fillBox({kKeys.x - 1, kKeys.y - 1, kKeys.w + 2, kKeys.h + 2}, k.well, 2); // the bed the keys lie in: on the flat look the dark window itself
        uint32_t sounding[4];
        for (int i = 0; i < 4; ++i) sounding[i] = sampler_->engine.meters.keys[i].load(std::memory_order_relaxed);
        // Every key has a column of the same width, so that the keys stand under their place in the zone
        // map. A white key widens below the black keys, to halfway into the black column beside it.
        const auto lit = [&](int key) { return key == mouseKey_ || ((sounding[key >> 5] >> (key & 31)) & 1u) != 0; };
        for (int key = 0; key < 128; ++key) {
            if (isBlack(key)) continue;
            const float left = kKeys.x + key * kKeyW - (key > 0 && isBlack(key - 1) ? 0.5f * kKeyW : 0.0f);
            const float right = kKeys.x + (key + 1) * kKeyW + (key < 127 && isBlack(key + 1) ? 0.5f * kKeyW : 0.0f);
            fillBox({left + 0.5f, kKeys.y, right - left - 1, kKeys.h}, lit(key) ? kWarm : kIvory, 1.5f);
            if (key % 12 == 0) label(keyName(key).c_str(), 0.5f * (left + right), kKeys.y + kKeys.h - 6, 6.5f, rgb(kKeyPrint), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        for (int key = 0; key < 128; ++key) {
            if (!isBlack(key)) continue;
            const float x = kKeys.x + key * kKeyW;
            fillBox({x - 0.5f, kKeys.y, kKeyW + 1, kKeys.h * kBlackLength + 1}, k.well, 0); // the gap around it
            fillBox({x + 0.5f, kKeys.y, kKeyW - 1, kKeys.h * kBlackLength}, lit(key) ? kWarm : kEbony, 1.5f);
        }
    }

    /** The key under a place on the keyboard: below a black key, the white key on that side. */
    static int keyAt(float x, float y) {
        const float at = (x - kKeys.x) / kKeyW;
        int key = std::clamp(static_cast<int>(at), 0, 127);
        if (isBlack(key) && y > kKeys.y + kKeys.h * kBlackLength) key += at - static_cast<float>(key) < 0.5f ? -1 : 1;
        return std::clamp(key, 0, 127);
    }

    // ----------------------------------------------------------------- map

    Box mapBox() const { return {kPageArea.x, kPageArea.y, kPageArea.w, 398}; }
    Box zoneBox(const Zone& z) const {
        const Box m = mapBox();
        const float top = m.y + m.h * (1.0f - static_cast<float>(z.v[kZHiVel]) / 127.0f), bottom = m.y + m.h * (1.0f - static_cast<float>(z.v[kZLoVel] - 1) / 127.0f);
        return {m.x + static_cast<float>(z.v[kZLoKey]) * kKeyW, top, static_cast<float>(z.v[kZHiKey] - z.v[kZLoKey] + 1) * kKeyW, bottom - top};
    }

    void drawMap() {
        const hnl::ui::Skin k = skin();
        const Box m = mapBox();
        window(m, 4);
        for (int key = 0; key < 128; ++key) {
            if (isBlack(key)) fillBox({m.x + key * kKeyW, m.y, kKeyW, m.h}, 0x000000, 0, 0.25f); // the shade of a black key's column
            if (key % 12 == 0) gridLine(m.x + key * kKeyW, m.y, m.x + key * kKeyW, m.y + m.h);
        }
        const std::vector<Zone>& zones = part().zones;
        for (size_t i = 0; i < zones.size(); ++i) {
            const Box b = zoneBox(zones[i]);
            const bool chosen = static_cast<int>(i) == zone_;
            const Sample* s = doc_->findSample(zones[i].sample);
            const uint32_t colour = s != nullptr && s->missing ? k.red : (zones[i].v[kZTrigger] >= 0.5 ? kWarm : k.accent);
            fillBox({b.x + 0.5f, b.y + 0.5f, b.w - 1, b.h - 1}, colour, 2, chosen ? 0.55f : 0.22f);
            edge({b.x + 0.5f, b.y + 0.5f, b.w - 1, b.h - 1}, chosen ? k.text : colour, 2, chosen ? 1.5f : 0.8f);
            if (b.w > 40 && s != nullptr) { // its sound, its keys and, when not all, its velocities
                std::string text = fileName(s->path) + "   " + keyName(static_cast<int>(zones[i].v[kZLoKey])) + " to " + keyName(static_cast<int>(zones[i].v[kZHiKey]));
                if (zones[i].v[kZLoVel] > 1 || zones[i].v[kZHiVel] < 127) text += "   velocity " + std::to_string(static_cast<int>(zones[i].v[kZLoVel])) + " to " + std::to_string(static_cast<int>(zones[i].v[kZHiVel]));
                fitted(text, b.x + 5, b.y + 10, 8, b.w - 8, rgb(k.text));
            }
            // The root key: a mark at the bottom of the zone.
            if (chosen) fillBox({m.x + static_cast<float>(zones[i].v[kZRoot]) * kKeyW + 1, b.y + b.h - 4, kKeyW - 2, 3}, k.text, 0);
        }
        // The scales: octaves along the bottom, velocity up the right side.
        for (int key = 0; key < 128; key += 12) label(keyName(key).c_str(), m.x + key * kKeyW + 3, m.y + m.h - 7, 6.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        static const int kVels[3] = {127, 64, 1};
        for (int vel : kVels) {
            const float y = m.y + m.h * (1.0f - (vel - 0.5f) / 127.0f);
            label(vel == 127 ? "VELOCITY 127" : std::to_string(vel).c_str(), m.x + m.w - 4, std::clamp(y, m.y + 7, m.y + m.h - 7), 6.5f, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        if (zones.empty()) label("No sounds in this part yet.", m.x + m.w * 0.5f, m.y + m.h * 0.5f - 8, 11, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        const Zone* z = zone();
        const Sample* s = z != nullptr ? doc_->findSample(z->sample) : nullptr;
        if (s != nullptr) fitted(s->missing ? missingText(*s) + ": " + s->path : s->path, kPageArea.x + 50, m.y + m.h + 17, 9, 900, rgb(s->missing ? hnl::ui::printed(k, k.red) : k.plateText)); // on the plate, under the map
    }

    bool mapDown(float x, float y) {
        const std::vector<Zone>& zones = part().zones;
        int hit = -1;
        for (int pass = 0; pass < 2 && hit < 0; ++pass) // the chosen zone first, then the one drawn last (on top)
            for (int i = static_cast<int>(zones.size()) - 1; i >= 0 && hit < 0; --i)
                if ((pass == 1 || i == zone_) && zoneBox(zones[static_cast<size_t>(i)]).contains(x, y)) hit = i;
        if (hit < 0) return true;
        zone_ = hit;
        const Box b = zoneBox(zones[static_cast<size_t>(hit)]);
        // Which part of the zone the mouse holds: an edge, or the middle to move it.
        grab_ = x < b.x + std::min(5.0f, b.w * 0.3f) ? 1 : (x > b.x + b.w - std::min(5.0f, b.w * 0.3f) ? 2 : (y < b.y + 5 ? 3 : (y > b.y + b.h - 5 ? 4 : 0)));
        area_ = kAreaMap;
        grabKey_ = static_cast<int>((x - mapBox().x) / kKeyW);
        view0_ = view1_ = 0;
        repaint();
        return true;
    }

    void mapDrag(float x, float y) {
        const Box m = mapBox();
        const int key = std::clamp(static_cast<int>((x - m.x) / kKeyW), 0, 127), vel = std::clamp(static_cast<int>(std::lround((1.0f - (y - m.y) / m.h) * 127.0f)), 1, 127);
        const int grab = grab_, by = key - grabKey_;
        if (grab == 0 && by == 0) return;
        editZone([=](Zone& z, Instrument&) {
            double lo = z.v[kZLoKey], hi = z.v[kZHiKey];
            if (grab == 0) { // moved as a whole, its root with it
                const double shift = std::clamp(static_cast<double>(by), -lo, 127.0 - hi);
                z.v[kZLoKey] = lo + shift; z.v[kZHiKey] = hi + shift; z.v[kZRoot] = std::clamp(z.v[kZRoot] + shift, 0.0, 127.0);
            } else if (grab == 1) z.v[kZLoKey] = std::min(static_cast<double>(key), hi);
            else if (grab == 2) z.v[kZHiKey] = std::max(static_cast<double>(key), lo);
            else if (grab == 3) z.v[kZHiVel] = std::max(static_cast<double>(vel), z.v[kZLoVel]);
            else z.v[kZLoVel] = std::min(static_cast<double>(vel), z.v[kZHiVel]);
        });
        grabKey_ = key;
        repaint();
    }

    // -------------------------------------------------------------- sample

    Box waveBox() const { return {kPageArea.x, kPageArea.y, kPageArea.w, 366}; }
    float waveX(double frame) const { return waveBox().x + static_cast<float>((frame - view0_) / (view1_ - view0_)) * waveBox().w; }
    /** The markers of the chosen zone, as places in its sound: start, end, loop start, loop end. */
    void markers(const Zone& z, double frames, double out[4]) const {
        out[0] = z.v[kZStart];
        out[1] = z.v[kZEnd] > 0 && z.v[kZEnd] < frames ? z.v[kZEnd] : frames;
        out[2] = z.v[kZLoopStart];
        out[3] = z.v[kZLoopEnd] > 0 && z.v[kZLoopEnd] < out[1] ? z.v[kZLoopEnd] : out[1];
    }

    void drawSample() {
        const hnl::ui::Skin k = skin();
        const Box w = waveBox();
        window(w, 4);
        const Zone* z = zone();
        const AudioData* d = zoneData();
        if (z == nullptr || d == nullptr || d->frames() < 2) {
            const Sample* s = z != nullptr ? doc_->findSample(z->sample) : nullptr;
            const std::string why = z == nullptr ? "No sounds in this part yet."
                                    : s == nullptr || !s->missing ? "Reading the sound..."
                                    : missingText(*s) == "Not found" ? "This zone's sound was not found on disk." : "This zone's sound was not loaded: " + s->error + ".";
            label(why.c_str(),
                  w.x + w.w * 0.5f, w.y + w.h * 0.5f - 8, 11, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }
        const double frames = static_cast<double>(d->frames());
        if (view1_ <= view0_ || view1_ > frames) { view0_ = 0; view1_ = frames; }
        double mark[4];
        markers(*z, frames, mark);
        if (z->v[kZLoop] >= 0.5 && mark[3] > mark[2])
            fillBox({std::max(w.x, waveX(mark[2])), w.y, std::min(w.x + w.w, waveX(mark[3])) - std::max(w.x, waveX(mark[2])), w.h}, k.accent, 0, 0.10f);
        const int columns = static_cast<int>(w.w);
        std::vector<float> shape(static_cast<size_t>(2 * columns));
        const int channels = d->channels > 1 && d->samples[1].size() == d->samples[0].size() ? 2 : 1;
        for (int c = 0; c < channels; ++c) {
            waveformOverview(d->samples[c].data(), d->frames(), static_cast<size_t>(view0_), static_cast<size_t>(view1_), columns, shape.data());
            const float mid = w.y + w.h * (channels == 1 ? 0.5f : 0.25f + 0.5f * c), half = w.h * (channels == 1 ? 0.46f : 0.23f);
            gridLine(w.x, mid, w.x + w.w, mid);
            beginPath(); // the outline of the wave: its highest values from left to right, its lowest back again
            for (int i = 0; i < columns; ++i) {
                const float y = mid - half * std::clamp(shape[static_cast<size_t>(2 * i + 1)], -1.0f, 1.0f) - 0.5f;
                if (i == 0) moveTo(w.x + i, y); else lineTo(w.x + i, y);
            }
            for (int i = columns - 1; i >= 0; --i) lineTo(w.x + i, mid - half * std::clamp(shape[static_cast<size_t>(2 * i)], -1.0f, 1.0f) + 0.5f);
            closePath();
            fillColor(rgb(k.accent, 0.85f));
            UI::fill();
        }
        { // a ruler of time, under the flags of start and end
            const double seconds = (view1_ - view0_) / d->sampleRate, rough = seconds / 9.0, power = std::pow(10.0, std::floor(std::log10(std::max(rough, 1e-9))));
            const double step = power * (rough / power < 2.0 ? 2.0 : (rough / power < 5.0 ? 5.0 : 10.0));
            for (double t = std::ceil(view0_ / d->sampleRate / step) * step; t * d->sampleRate < view1_ && step > 0; t += step) {
                const float x = waveX(t * d->sampleRate);
                char text[24];
                if (step >= 1.0) std::snprintf(text, sizeof(text), "%.0f s", t);
                else if (step >= 0.01) std::snprintf(text, sizeof(text), "%.2f s", t);
                else std::snprintf(text, sizeof(text), "%.1f ms", 1000.0 * t);
                line(x, w.y + 14, x, w.y + 19, k.dim, 1.0f, 0.7f);
                if (x < w.x + w.w - 40) label(text, x + 3, w.y + 21, 6.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            }
        }
        static const char* const kLetters[4] = {"S", "E", "LS", "LE"};
        for (int i = 0; i < 4; ++i) {
            if (i >= 2 && z->v[kZLoop] < 0.5) continue;
            const float x = waveX(mark[i]);
            if (x < w.x - 1 || x > w.x + w.w + 1) continue;
            const uint32_t colour = i < 2 ? k.text : kWarm;
            line(x, w.y, x, w.y + w.h, colour, 1.2f);
            const float fw = i < 2 ? 12.0f : 17.0f; // the flag: to the right of a start, to the left of an end
            fillBox({i % 2 == 0 ? x : x - fw, i < 2 ? w.y : w.y + w.h - 12, fw, 12}, colour, 2);
            label(kLetters[i], (i % 2 == 0 ? x : x - fw) + fw * 0.5f, (i < 2 ? w.y : w.y + w.h - 12) + 6.5f, 8, rgb(inkOn(k)), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        if (sampler_->engine.meters.zone.load(std::memory_order_relaxed) == z->id) { // where the newest note of this zone is
            const double s = z->v[kZStart], e = mark[1];
            const float x = waveX(s + sampler_->engine.meters.position.load(std::memory_order_relaxed) * (e - s));
            if (x >= w.x && x <= w.x + w.w) line(x, w.y, x, w.y + w.h, k.green, 1.0f);
        }
        const Sample* s = doc_->findSample(z->sample);
        char facts[160];
        std::snprintf(facts, sizeof(facts), "%s   %.0f Hz   %d bit%s   %s   %.2f s", d->format.c_str(), d->sampleRate, d->bits, d->isFloat ? " float" : "", channels == 2 ? "stereo" : "mono",
                      frames / d->sampleRate);
        fitted(s != nullptr ? fileName(s->path) + "   " + facts : facts, kPageArea.x + 56, kPageArea.y + 383, 9, 560, rgb(k.plateText));
        // The places are counted in samples; under each, the same as time.
        const double places[5] = {mark[0], mark[1], mark[2], mark[3], z->v[kZCrossfade]};
        static const int kColumn[5] = {0, 1, 3, 4, 5};
        for (int i = 0; i < 5; ++i) label(timeText(places[i], d->sampleRate).c_str(), kPageArea.x + 10 + kColumn[i] * 68.0f + 31, kPageArea.y + 435, 7.5f, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        label("Places are in samples, with the time under them.", kPageArea.x + 10, kPageArea.y + 462, 8, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        if (!toolNote_.empty()) label(toolNote_.c_str(), kPageArea.x + 632, kPageArea.y + 462, 9, rgb(noteOn(k)), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    bool waveDown(float x) {
        const Zone* z = zone();
        const AudioData* d = zoneData();
        if (z == nullptr || d == nullptr) return true;
        if (view1_ <= view0_ || view1_ > static_cast<double>(d->frames())) { view0_ = 0; view1_ = static_cast<double>(d->frames()); }
        double mark[4];
        markers(*z, static_cast<double>(d->frames()), mark);
        grab_ = -1;
        float nearest = 8;
        for (int i = 0; i < 4; ++i) {
            if (i >= 2 && z->v[kZLoop] < 0.5) continue;
            if (std::fabs(waveX(mark[i]) - x) < nearest) { nearest = std::fabs(waveX(mark[i]) - x); grab_ = i; }
        }
        if (grab_ >= 0) area_ = kAreaWave;
        return true;
    }

    void waveDrag(float x) {
        const AudioData* d = zoneData();
        if (d == nullptr || grab_ < 0) return;
        const double frames = static_cast<double>(d->frames());
        const double at = std::clamp(std::floor(view0_ + (x - waveBox().x) / waveBox().w * (view1_ - view0_)), 0.0, frames);
        const int grab = grab_;
        editZone([=](Zone& z, Instrument&) {
            const double end = z.v[kZEnd] > 0 ? z.v[kZEnd] : frames, loopEnd = z.v[kZLoopEnd] > 0 ? z.v[kZLoopEnd] : end;
            if (grab == 0) z.v[kZStart] = std::min(at, end - 2);
            else if (grab == 1) z.v[kZEnd] = std::max(at, z.v[kZStart] + 2);
            else if (grab == 2) z.v[kZLoopStart] = std::clamp(at, z.v[kZStart], loopEnd - 2);
            else z.v[kZLoopEnd] = std::clamp(at, z.v[kZLoopStart] + 2, end);
        });
        repaint();
    }

    /** One channel of the chosen zone's sound as the analysis wants it: left and right added up. */
    std::vector<float> monoOf(const AudioData& d) const {
        std::vector<float> mono(d.samples[0]);
        if (d.channels > 1 && d.samples[1].size() == mono.size())
            for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + d.samples[1][i]);
        return mono;
    }

    void findLoopHere() {
        const Zone* z = zone();
        const AudioData* d = zoneData();
        if (z == nullptr || d == nullptr) return;
        double mark[4];
        markers(*z, static_cast<double>(d->frames()), mark);
        // Between the loop markers when a loop is set, otherwise in the second half of what plays.
        const size_t from = static_cast<size_t>(z->v[kZLoop] >= 0.5 ? mark[2] : mark[0] + 0.3 * (mark[1] - mark[0])), to = static_cast<size_t>(z->v[kZLoop] >= 0.5 ? mark[3] : mark[1]);
        const bool stereo = d->channels > 1 && d->samples[1].size() == d->samples[0].size();
        const LoopResult found = findLoop(d->samples[0].data(), stereo ? d->samples[1].data() : nullptr, d->frames(), d->sampleRate, from, to, std::min(0.25, 0.4 * (to - from) / d->sampleRate));
        if (found.end <= found.start) { toolNote_ = "No loop found there."; return; }
        editZone([&](Zone& zn, Instrument&) {
            zn.v[kZLoop] = 1;
            zn.v[kZLoopStart] = static_cast<double>(found.start);
            zn.v[kZLoopEnd] = static_cast<double>(found.end);
        });
        char text[64];
        std::snprintf(text, sizeof(text), "Loop found, a %.0f%% match.", 100.0 * found.quality);
        toolNote_ = text;
    }

    void detectPitchHere() {
        const AudioData* d = zoneData();
        if (d == nullptr) return;
        const std::vector<float> mono = monoOf(*d);
        const PitchResult found = detectPitch(mono.data(), mono.size(), d->sampleRate);
        if (found.key < 0 || found.confidence < 0.5) { toolNote_ = "No clear pitch in this sound."; return; }
        editZone([&](Zone& z, Instrument&) {
            z.v[kZRoot] = std::clamp(found.key, 0, 127);
            z.v[kZTune] = -found.cents; // played back down by as much as it is sharp
        });
        char text[64];
        std::snprintf(text, sizeof(text), "%s %+.0f cents (%.1f Hz).", keyName(found.key).c_str(), found.cents, found.hz);
        toolNote_ = text;
    }

    void normaliseHere() {
        const AudioData* d = zoneData();
        if (d == nullptr) return;
        const bool stereo = d->channels > 1 && d->samples[1].size() == d->samples[0].size();
        const double gain = normaliseGainDb(d->samples[0].data(), stereo ? d->samples[1].data() : nullptr, d->frames(), 0.0);
        editZone([&](Zone& z, Instrument&) { z.v[kZGain] = cleanValue(zoneInfo()[kZGain], gain); });
        char text[64];
        std::snprintf(text, sizeof(text), "Gain set to %+.1f dB.", gain);
        toolNote_ = text;
    }

    /** Cuts the chosen zone into one zone per hit, on the keys from C2 up. */
    void sliceHere() {
        const Zone* z = zone();
        const AudioData* d = zoneData();
        if (z == nullptr || d == nullptr) return;
        const std::vector<float> mono = monoOf(*d);
        std::vector<size_t> hits = detectTransients(mono.data(), mono.size(), d->sampleRate, 0.5);
        double mark[4];
        markers(*z, static_cast<double>(d->frames()), mark);
        hits.erase(std::remove_if(hits.begin(), hits.end(), [&](size_t h) { return h < mark[0] || h >= mark[1] - 2; }), hits.end());
        if (hits.size() < 2) { toolNote_ = "Fewer than two hits found."; return; }
        if (hits.size() > 64) hits.resize(64);
        const double end = mark[1];
        const int p = part_, index = zone_;
        sampler_->edit([&](Instrument& doc) {
            std::vector<Zone>& zones = doc.parts[p].zones;
            if (index >= static_cast<int>(zones.size())) return;
            const Zone whole = zones[static_cast<size_t>(index)];
            zones.erase(zones.begin() + index);
            for (size_t i = 0; i < hits.size() && static_cast<int>(zones.size()) < kMaxPartZones; ++i) {
                Zone s = whole;
                s.id = doc.nextId++;
                const int key = std::min(127, 36 + static_cast<int>(i));
                s.v[kZLoKey] = s.v[kZHiKey] = s.v[kZRoot] = key;
                s.v[kZStart] = static_cast<double>(hits[i]);
                s.v[kZEnd] = i + 1 < hits.size() ? static_cast<double>(hits[i + 1]) : end;
                s.v[kZLoop] = 0;
                s.v[kZOneShot] = 1;
                zones.push_back(s);
            }
        });
        refresh();
        zone_ = 0;
        char text[64];
        std::snprintf(text, sizeof(text), "%zu slices, from C2 up.", hits.size());
        toolNote_ = text;
        view0_ = view1_ = 0;
    }

    // ------------------------------------------------------ synth and mod

    /** The shape of each envelope, over its knobs. */
    void drawEnvelopes() {
        for (int e = 0; e < kEnvs; ++e) {
            const Box& p = panels_[static_cast<size_t>(8 + e)].box; // after the two rows of panels above
            const Box g = {p.x + 10, p.y + 24, p.w - 20, p.h - 24 - 70};
            window(g, 3);
            const double* v = &part().v[envParam(e, 0)];
            const double times[5] = {v[kEnvDelay], v[kEnvAttack], v[kEnvHold], v[kEnvDecay], v[kEnvRelease]};
            double total = 0;
            for (double t : times) total += std::sqrt(t); // long stages are drawn shorter than they are, so that short ones stay visible
            const float unit = (g.w - 40) / static_cast<float>(std::max(total, 1e-3)), base = g.y + g.h - 6, top = g.y + 8, sustain = base - (base - top) * static_cast<float>(0.01 * v[kEnvSustain]);
            float x = g.x + 4;
            beginPath();
            moveTo(x, base);
            x += unit * static_cast<float>(std::sqrt(times[0])); lineTo(x, base);
            x += unit * static_cast<float>(std::sqrt(times[1])); lineTo(x, top);
            x += unit * static_cast<float>(std::sqrt(times[2])); lineTo(x, top);
            const float bend = static_cast<float>(0.008 * v[kEnvCurve]);
            const float dx = unit * static_cast<float>(std::sqrt(times[3]));
            for (int i = 1; i <= 8; ++i) lineTo(x + dx * i / 8.0f, sustain + (top - sustain) * std::pow(1.0f - i / 8.0f, 1.0f + 3.0f * bend));
            x += dx;
            lineTo(x + 32, sustain);
            x += 32;
            const float rx = unit * static_cast<float>(std::sqrt(times[4]));
            for (int i = 1; i <= 8; ++i) lineTo(x + rx * i / 8.0f, base + (sustain - base) * std::pow(1.0f - i / 8.0f, 1.0f + 3.0f * bend));
            strokeColor(rgb(skin().accent));
            strokeWidth(1.6f);
            stroke();
        }
    }

    Box stepsBox() const { return panels_.size() > 4 ? Box{panels_[4].box.x + 8, panels_[4].box.y + 8, panels_[4].box.w - 16, panels_[4].box.h - 16} : Box{}; }

    void drawSteps() {
        const hnl::ui::Skin k = skin();
        const Box b = stepsBox();
        if (!k.solid) fillBox(b, k.well, 3); // solid: its whole panel is the window
        const int steps = static_cast<int>(part().v[kPSeqSteps]);
        const float w = b.w / kSteps, mid = b.y + b.h * 0.5f;
        gridLine(b.x, mid, b.x + b.w, mid);
        for (int i = 0; i < kSteps; ++i) {
            const float v = static_cast<float>(0.01 * part().v[kPSeqValue + i]), h = v * (b.h * 0.5f - 3);
            fillBox({b.x + i * w + 2, h >= 0 ? mid - h : mid, w - 4, std::max(1.5f, std::fabs(h))}, k.accent, 1, i < steps ? 1.0f : 0.25f);
        }
        for (int column = 0; column < 2; ++column) {
            static const char* const kHeads[4] = {"SOURCE", "VIA", "TARGET", "AMOUNT"};
            for (int h = 0; h < 4; ++h) label(kHeads[h], kPageArea.x + 62 + column * 480.0f + h * 116.0f, matrixY() + 22, 7.5f, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        }
        for (int s = 0; s < kSlots; ++s) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", s + 1);
            label(text, kPageArea.x + 44 + (s / 6) * 480.0f, matrixY() + 41 + (s % 6) * 37.0f, 9, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        label("Click a macro's name to change it.", kPageArea.x + 976 - 10, kPageArea.y + kRowH + 6 + 11, 7.5f, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void stepDrag(float x, float y) {
        const Box b = stepsBox();
        const int step = std::clamp(static_cast<int>((x - b.x) / (b.w / kSteps)), 0, kSteps - 1);
        double value = std::clamp(100.0 * (b.y + b.h * 0.5f - y) / (b.h * 0.5f - 3), -100.0, 100.0);
        if (std::fabs(value) < 4.0) value = 0.0; // the middle catches
        const int p = part_;
        sampler_->edit([=](Instrument& doc) { doc.parts[p].v[kPSeqValue + step] = std::floor(value + 0.5); });
        refresh();
        repaint();
    }

    void drawStrips() {
        const hnl::ui::Skin k = skin();
        for (int p = 0; p < kParts; ++p) {
            const float x = kPageArea.x + p * 92.0f;
            if (p == part_) edge({x, kPageArea.y, 88, 480}, hnl::ui::printed(k, k.accent), 6);
            const std::string& name = doc_->parts[p].name;
            char count[24];
            std::snprintf(count, sizeof(count), "%zu zones", doc_->parts[p].zones.size());
            label(count, x + 78, kPageArea.y + 11, 7.5f, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
            if (editKind_ == kEditPart && editIndex_ == p) continue;
            fitted(name.empty() ? "name" : name, x + 10, kPageArea.y + 24, 8, 76, rgb(name.empty() ? k.plateDim : k.plateText, name.empty() ? 0.6f : 1.0f));
        }
        label(doc_->tuningName.empty() ? "Equal temperament" : doc_->tuningName.c_str(), kPageArea.x + 750, kPageArea.y + 449, 10, rgb(k.plateText), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // --------------------------------------------------------------- files

    Box listBox() const { return {kPageArea.x, kPageArea.y, 634, 480}; }
    static constexpr int kRows = 22;

    static constexpr int kMaxSeen = 1000000;      // entries of a folder that are looked at
    static constexpr size_t kMaxEntries = 100000; // and those among them that are shown

    /** Why a sound is not there: not found on disk, or found and not read, with the reader's reason. */
    static std::string missingText(const Sample& s) {
        return s.error.empty() || s.error == "could not open the file" ? "Not found" : "Not loaded, " + s.error;
    }

    void readFolder() {
        entries_.clear();
        scroll_ = 0;
        chosen_ = -1;
        std::error_code ec;
        if (folder_.empty()) { // above the drives
#ifdef _WIN32
            for (char drive = 'A'; drive <= 'Z'; ++drive) {
                const std::string root = std::string(1, drive) + ":\\";
                if (fs::exists(fs::u8path(root), ec)) entries_.push_back({root, true});
            }
#else
            folder_ = "/";
#endif
            if (folder_.empty()) return;
        }
        folder_ = fs::u8path(folder_).lexically_normal().u8string();
        // The whole folder is read, so that what is shown is in order and nothing is left out without a word.
        // The two limits are there for a folder beyond any use, and the page says so when one is reached.
        folderCut_ = false;
        fs::directory_iterator it(fs::u8path(folder_), fs::directory_options::skip_permission_denied, ec), end;
        for (int seen = 0; !ec && it != end; it.increment(ec), ++seen) {
            if (seen >= kMaxSeen || entries_.size() >= kMaxEntries) { folderCut_ = true; break; }
            const std::string name = it->path().filename().u8string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code type;
            if (it->is_directory(type)) entries_.push_back({name, true});
            else if (it->is_regular_file(type)) { // not a pipe or a device, which may never answer when it is opened
                const std::string ext = extension(name);
                if (isAudioFileName(name) || ext == ".sfz" || ext == ".sf2" || ext == ".scl" || ext == Library::kExt) entries_.push_back({name, false});
            }
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return a.folder != b.folder ? a.folder : SampleCache::lower(a.name) < SampleCache::lower(b.name);
        });
    }
    std::string chosenPath() const {
        if (chosen_ < 0 || chosen_ >= static_cast<int>(entries_.size())) return "";
        return folder_.empty() ? entries_[static_cast<size_t>(chosen_)].name : (fs::u8path(folder_) / fs::u8path(entries_[static_cast<size_t>(chosen_)].name)).u8string();
    }

    void drawFiles() {
        const hnl::ui::Skin k = skin();
        const Box b = listBox();
        if (k.solid) hnl::ui::displayPanel(*this, k, {b.x + 3, b.y + 30, b.w - 6, b.h - 33}); // the list in a window, under the keys and the folder's name
        fitted(folder_.empty() ? "This computer" : folder_, b.x + 160, b.y + 16, 10, b.w - (folderCut_ ? 330 : 170), rgb(k.plateText));
        if (folderCut_) label("Too many to show them all", b.x + b.w - 10, b.y + 16, 9, rgb(noteOn(k)), ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int row = 0; row < kRows; ++row) {
            const int i = scroll_ + row;
            if (i >= static_cast<int>(entries_.size())) break;
            const float y = b.y + 32 + row * 20.0f;
            if (i == chosen_) fillBox({b.x + 4, y, b.w - 8, 20}, k.button, 3);
            const Entry& e = entries_[static_cast<size_t>(i)];
            if (e.folder) fillBox({b.x + 12, y + 6, 12, 9}, kWarm, 2);
            else dot(b.x + 18, y + 10, 3, isAudioFileName(e.name) ? k.accent : k.green);
            fitted(e.name, b.x + 34, y + 10, 10, b.w - 50, rgb(e.folder ? k.text : (isAudioFileName(e.name) ? k.text : k.green)));
        }
        if (entries_.empty()) label("Nothing here that the sampler can use.", b.x + b.w * 0.5f, b.y + 120, 10, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        if (static_cast<int>(entries_.size()) > kRows) { // how far down the list is
            const float h = b.h - 40, part = static_cast<float>(kRows) / entries_.size();
            fillBox({b.x + b.w - 5, b.y + 34 + h * scroll_ / entries_.size(), 2.5f, h * part}, k.dim, 1);
        }
        const float x = kPageArea.x + 652, y = kPageArea.y;
        const int kind = chosenKind();
        if (chosen_ >= 0 && chosen_ < static_cast<int>(entries_.size())) fitted(entries_[static_cast<size_t>(chosen_)].name, x, y + 34, 11, 312, rgb(k.plateText));
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textLineHeight(1.4f);
        fillColor(rgb(k.plateDim));
        const std::string partText = "part " + std::to_string(part_ + 1);
        if (kind == kFileNone && chosen_ < 0) textBox(x, y + 30, 312, ("Click a sound to hear it, double-click to add it to " + partText + ". Double-click a folder to open it.").c_str(), nullptr);
        else textBox(x, y + 50, 312, fileFacts_.c_str(), nullptr);
        if (kind == kFileSound)
            textBox(x, y + 214, 312, ("ADD AS ZONE puts it on all keys of " + partText + ", or on the keys its file names.\nADD ON NEXT KEY gives it one key, after the highest zone: for a drum kit.").c_str(), nullptr);
        float line = y + (kind == kFileSound ? 276 : 226);
        for (const std::string& n : notes_) {
            if (line > y + 466) break;
            fitted(n, x, line, 9, 312, rgb(noteOn(k)));
            line += 14;
        }
    }

    bool fileDown(float y, bool twice) {
        const int row = static_cast<int>((y - listBox().y - 32) / 20.0f);
        if (y < listBox().y + 32 || row < 0 || scroll_ + row >= static_cast<int>(entries_.size())) return true;
        chosen_ = scroll_ + row;
        const Entry entry = entries_[static_cast<size_t>(chosen_)];
        const std::string path = chosenPath();
        fileFacts_.clear();
        notes_.clear();
        if (entry.folder) {
            if (twice) { folder_ = path; readFolder(); }
            else fileFacts_ = "A folder. Double-click opens it.";
        } else if (isAudioFileName(entry.name)) {
            AudioInfo info;
            AudioData meta;
            std::string error;
            bool usable = true;
            if (probeAudioFile(fs::u8path(path), info, &meta, error)) {
                char text[200];
                std::snprintf(text, sizeof(text), "%s, %.0f Hz, %d bit%s, %d channel%s, %.2f s", info.format.c_str(), info.sampleRate, info.bits, info.isFloat ? " float" : "", info.channels,
                              info.channels == 1 ? "" : "s", info.sampleRate > 0 ? static_cast<double>(info.frames) / info.sampleRate : 0.0);
                fileFacts_ = text;
                if (meta.rootKey >= 0) fileFacts_ += ". Root " + keyName(std::min(127, meta.rootKey));
                if (!meta.loops.empty()) fileFacts_ += ". Has a loop";
                if (meta.tempo > 0) { std::snprintf(text, sizeof(text), ". %.1f bpm", meta.tempo); fileFacts_ += text; }
                // A file can be described without being read; what it would take in memory is known from that.
                if (!hnl::sampler::audiofile::fitsInMemory(info.frames, info.channels)) { fileFacts_ += ".\nCannot be used: " + std::string(hnl::sampler::audiofile::kTooLarge); usable = false; }
            } else { fileFacts_ = "Cannot be read: " + error; usable = false; }
            if (!usable) sampler_->preview("");
            else if (twice) useFile(false);
            else sampler_->preview(path);
        } else {
            const std::string ext = extension(entry.name);
            fileFacts_ = ext == ".sfz" ? "An SFZ instrument. Import puts it into the chosen part." : ext == ".sf2" ? "A SoundFont. Import puts its first preset into the chosen part."
                         : ext == ".scl" ? "A Scala tuning. Load makes it the tuning of this instrument." : "An instrument saved by this sampler. Load replaces the one that is here.";
            if (twice) useFile(false);
        }
        layout(); // the buttons follow the kind of file
        repaint();
        return true;
    }

    enum FileKind { kFileNone, kFileSound, kFileImport, kFileTuning, kFileInstrument };
    int chosenKind() const {
        if (chosen_ < 0 || chosen_ >= static_cast<int>(entries_.size()) || entries_[static_cast<size_t>(chosen_)].folder) return kFileNone;
        const std::string& name = entries_[static_cast<size_t>(chosen_)].name;
        const std::string ext = extension(name);
        return isAudioFileName(name) ? kFileSound : (ext == ".sfz" || ext == ".sf2" ? kFileImport : (ext == ".scl" ? kFileTuning : kFileInstrument));
    }

    // --------------------------------------------------------------- names

    Box nameBox(int kind, int index) const {
        if (kind == kEditMacro) return {macroX(index) - 23, kPageArea.y + kRowH + 6 + 62 - 28, 46, 14};
        return {kPageArea.x + index * 92.0f + 5, kPageArea.y + 17, 78, 14};
    }
    /** A click on a macro's name (MOD) or a part's name (MIX) starts typing a new one. */
    bool nameEditDown(float x, float y) {
        const int kind = page_ == kPageMod ? kEditMacro : (page_ == kPageMix ? kEditPart : kEditNone);
        if (kind == kEditNone) return false;
        for (int i = 0; i < 8; ++i) {
            if (!nameBox(kind, i).contains(x, y)) continue;
            editKind_ = kind;
            editIndex_ = i;
            editText_ = kind == kEditMacro ? doc_->macroNames[i] : doc_->parts[i].name;
            if (editText_.size() > 16) editText_.resize(16);
            getWindow().focus(); // so that the typing comes to this window
            repaint();
            return true;
        }
        return false;
    }
    void endNameEdit(bool keep) {
        const int kind = editKind_, index = editIndex_;
        const std::string typed = hnl::sampler::text::oneLine(editText_, hnl::sampler::text::kMaxName);
        editKind_ = kEditNone;
        if (keep) {
            sampler_->edit([=](Instrument& doc) {
                if (kind == kEditMacro) doc.macroNames[index] = typed; // empty: Macro 1 to 8 again
                else doc.parts[index].name = typed;
            });
            refresh();
        }
        layout();
        repaint();
    }
    void drawNameEdit() {
        if (editKind_ == kEditNone || (editKind_ == kEditMacro) != (page_ == kPageMod) || (editKind_ == kEditPart && page_ != kPageMix)) return;
        const Box b = nameBox(editKind_, editIndex_);
        const hnl::ui::Skin k = skin();
        fillBox(b, k.solid ? k.popup : k.well, 2); // as the name box of the instruments
        edge(b, k.accent, 2);
        fitted(editText_ + "|", b.x + 3, b.y + b.h * 0.5f, 8, b.w + 6, rgb(k.text));
    }

    /** Does what belongs to the chosen file: a sound becomes a zone, an instrument is imported or loaded, a tuning is taken. */
    void useFile(bool nextKey) {
        if (chosen_ < 0 || chosen_ >= static_cast<int>(entries_.size()) || entries_[static_cast<size_t>(chosen_)].folder) return;
        const std::string path = chosenPath(), ext = extension(path);
        std::string error;
        notes_.clear();
        if (!library_.dir.empty()) { // the folder to open in next time
            std::error_code ec;
            hnl::presetfile::fs::create_directories(library_.dir, ec);
            if (std::FILE* f = hnl::presetfile::open(library_.dir / "last-folder.txt", true)) {
                std::fwrite(folder_.data(), 1, folder_.size(), f);
                std::fclose(f);
            }
        }
        if (isAudioFileName(path)) {
            int lo = 0, hi = 127, root = 60;
            if (nextKey) {
                lo = 36;
                for (const Zone& z : part().zones) lo = std::max(lo, static_cast<int>(z.v[kZHiKey]) + 1);
                lo = hi = root = std::min(127, lo);
            }
            if (!sampler_->addZone(part_, path, lo, hi, root)) {
                // Its path would not come back from a saved instrument (a line end or a tab in a name), or there is no room.
                notes_.push_back(hnl::sampler::text::oneLine(path, hnl::sampler::text::kMaxPath) != path ? "Not added: its name or folder has a line end or a tab in it, or is too long."
                                                                                                             : "Not added: this part or this instrument is full.");
            } else {
                if (nextKey) { // on one key a sound plays as it is, whatever note its file names
                    const int p = part_, key = lo;
                    sampler_->edit([=](Instrument& doc) {
                        if (doc.parts[p].zones.empty()) return;
                        Zone& z = doc.parts[p].zones.back();
                        z.take &= ~Zone::kTakeRoot;
                        z.v[kZRoot] = key;
                    });
                }
                refresh();
                zone_ = std::max(0, static_cast<int>(part().zones.size()) - 1);
                notes_.push_back(nextKey ? "Added on " + keyName(lo) + "." : "Added to part " + std::to_string(part_ + 1) + ".");
            }
        } else if (ext == ".sfz" || ext == ".sf2") {
            if (sampler_->import(part_, path, 0, error)) {
                notes_ = sampler_->notes();
                notes_.insert(notes_.begin(), "Imported into part " + std::to_string(part_ + 1) + ".");
            } else notes_.push_back("Not imported: " + error);
            zone_ = 0;
        } else if (ext == ".scl") {
            std::string text;
            Tuning tuning;
            if (Library::read(fs::u8path(path), text) && parseScala(text, "", tuning, error)) {
                const std::string name = fileName(path);
                sampler_->edit([&](Instrument& doc) {
                    for (int k = 0; k < 128; ++k) doc.tuning[k] = hnl::isFinite(tuning.cents[k]) ? std::clamp(tuning.cents[k], -12800.0, 12800.0) : 0.0;
                    doc.tuningName = hnl::sampler::text::oneLine(name, hnl::sampler::text::kMaxName);
                });
                notes_.push_back("Tuning loaded.");
            } else notes_.push_back("Not a tuning this can read: " + error);
        } else if (ext == Library::kExt) {
            std::string text;
            notes_.push_back(Library::read(fs::u8path(path), text) && sampler_->setText(text) ? "Instrument loaded." : "Not an instrument this can read.");
            zone_ = 0;
        }
        refresh();
        layout();
    }

    // --------------------------------------------------------------- menus

    void openMenu(float x, float y, const std::vector<std::string>& items, int current, std::function<void(int)> chosen) {
        menu_.items = items;
        menu_.current = menu_.hover = current;
        menu_.chosen = std::move(chosen);
        const Box m = menuBoxFor(x, y);
        menu_.x = std::min(x, UI_BASE_WIDTH - 6 - m.w);
        menu_.y = y + m.h > UI_BASE_HEIGHT - 6 ? std::max(6.0f, y - 22 - m.h) : y;
        repaint();
    }
    static constexpr float kMenuRow = 18, kMenuW = 104;
    static constexpr int kMenuRows = 13;
    Box menuBoxFor(float x, float y) const {
        const int n = static_cast<int>(menu_.items.size()), columns = (n + kMenuRows - 1) / kMenuRows;
        return {x, y, kMenuW * columns, kMenuRow * std::min(n, kMenuRows) + 6};
    }
    int menuRow(float x, float y) const {
        const Box m = menuBoxFor(menu_.x, menu_.y);
        if (!m.contains(x, y)) return -1;
        const int index = static_cast<int>((x - m.x) / kMenuW) * kMenuRows + std::clamp(static_cast<int>((y - m.y - 3) / kMenuRow), 0, kMenuRows - 1);
        return index >= 0 && index < static_cast<int>(menu_.items.size()) ? index : -1;
    }
    void drawMenu() {
        if (menu_.items.empty()) return;
        const Box m = menuBoxFor(menu_.x, menu_.y);
        const hnl::ui::Skin k = skin();
        fillBox(m, k.popup, 4);
        edge(m, k.popupEdge, 4);
        for (int i = 0; i < static_cast<int>(menu_.items.size()); ++i) {
            const float x = m.x + (i / kMenuRows) * kMenuW, y = m.y + 3 + (i % kMenuRows) * kMenuRow;
            if (i == menu_.hover) fillBox({x + 3, y, kMenuW - 6, kMenuRow}, k.button, 3);
            fitted(menu_.items[static_cast<size_t>(i)], x + 9, y + kMenuRow * 0.5f, 9.5f, kMenuW - 14, rgb(i == menu_.current ? k.accent : k.text));
        }
    }

    // --------------------------------------------------------- instruments

    /** What the instrument list shows: Init, then the saved ones. */
    std::vector<std::string> presetNames() const {
        std::vector<std::string> names = {"Init"};
        names.insert(names.end(), library_.names.begin(), library_.names.end());
        return names;
    }

    void presetAction(int picked, const std::vector<std::string>& names) {
        typedef hnl::ui::PresetBar Bar;
        if (picked == 0) {
            sampler_->edit([](Instrument& doc) { Sampler::initDocument(doc); });
        } else if (picked > 0 && picked < static_cast<int>(names.size())) {
            std::string text;
            if (!Library::read(library_.file(names[static_cast<size_t>(picked)]), text) || !sampler_->setText(text)) note_ = "Could not load";
        } else if (picked == Bar::kEdit) {
            presetBar_.startEdit(hnl::presetfile::cleanName(doc_->name == "Init" || doc_->name == "Empty" ? "My instrument" : doc_->name));
            getWindow().focus(); // so that the typing comes to this window
            return;
        } else if (picked == Bar::kSave) {
            std::string name;
            const std::string typed = presetBar_.text();
            sampler_->edit([&typed](Instrument& doc) { doc.name = hnl::sampler::text::oneLine(typed, hnl::sampler::text::kMaxName); });
            if (!library_.save(typed, sampler_->text(), name)) note_ = "Could not save";
        } else if (picked == Bar::kDelete) {
            std::error_code ec;
            const int target = presetBar_.target();
            if (target < 1 || target >= static_cast<int>(names.size()) || !hnl::presetfile::fs::remove(library_.file(names[static_cast<size_t>(target)]), ec)) note_ = "Could not delete";
            library_.rescan();
        } else return;
        zone_ = 0;
        view0_ = view1_ = 0;
        refresh();
        layout();
    }

    Sampler* sampler_ = nullptr;
    std::unique_ptr<Sampler> own_;
    std::shared_ptr<const Instrument> doc_;
    uint64_t revision_ = 0;
    uint32_t lastKeys_ = 0;
    float values_[kParamCount];

    int page_ = kPageMap, part_ = 0, zone_ = 0;
    int editKind_ = kEditNone, editIndex_ = 0;
    std::string editText_;
    std::vector<Ctl> ctls_;
    std::vector<Titled> panels_;
    std::vector<Button> buttons_;
    Menu menu_;
    int drag_ = -1, area_ = kAreaNone, grab_ = 0, grabKey_ = 0, mouseKey_ = -1;
    float dragY_ = 0, lastX_ = 0, lastY_ = 0;
    double dragNorm_ = 0, dragValue_ = 0, view0_ = 0, view1_ = 0;
    uint lastClickTime_ = 0;

    std::string folder_, home_, fileFacts_, toolNote_, note_;
    std::vector<Entry> entries_;
    bool folderCut_ = false; // the folder has more than is shown
    std::vector<std::string> notes_;
    int scroll_ = 0, chosen_ = -1;

    Library library_;
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerUI)
};

UI* createUI() { return new SamplerUI(); }

END_NAMESPACE_DISTRHO
