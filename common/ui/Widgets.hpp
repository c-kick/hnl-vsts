// UI parts shared by the plugins: the logo, the meters, the preset bar and the help tips.
//
// Include after DistrhoUI.hpp. The drawing functions are templates on the
// canvas (the plugin's UI class) so they also work with the SVG stand-in in
// tools/ui-preview. Coordinates are design pixels, as in the plugin UIs.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hnl {
namespace ui {

inline DGL_NAMESPACE::Color rgb(uint32_t hex, float alpha = 1.0f) {
    return DGL_NAMESPACE::Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

/** The colours a plugin lends to the shared parts. */
struct Theme {
    uint32_t bg, panel, edge, track, popup, popupEdge, text, dim, accent;
};

template <class C> void label(C& c, const char* text, float x, float y, float size, const DGL_NAMESPACE::Color& colour, int align) {
    c.fontSize(size);
    c.textAlign(align);
    c.fillColor(colour);
    c.text(x, y, text, nullptr);
}

template <class C> void triangle(C& c, float x0, float y0, float x1, float y1, float x2, float y2, const DGL_NAMESPACE::Color& colour) {
    c.beginPath();
    c.moveTo(x0, y0);
    c.lineTo(x1, y1);
    c.lineTo(x2, y2);
    c.closePath();
    c.fillColor(colour);
    c.fill();
}

/**
 * The HNL logo as three filled shapes, `height` tall with its top left at (x, y).
 * Returns its width, so that a plugin can set its name behind it.
 */
template <class C> float logo(C& c, float x, float y, float height, const DGL_NAMESPACE::Color& colour) {
    // Outlines on a 1698 x 440 grid; -1 ends a letter.
    static const float kPoints[] = {
        0, 0, 148, 0, 148, 164, 393, 164, 393, 0, 542, 0, 542, 440, 393, 440, 393, 270, 148, 270, 148, 440, 0, 440, -1,
        580, 0, 733, 0, 1033, 300, 1033, 0, 1182, 0, 1182, 440, 991, 440, 725, 175, 725, 440, 580, 440, -1,
        1220, 0, 1365, 0, 1365, 331, 1698, 331, 1698, 440, 1220, 440, -1,
    };
    const float k = height / 440.0f;
    c.beginPath();
    bool start = true;
    for (size_t i = 0; i < sizeof(kPoints) / sizeof(kPoints[0]);) {
        if (kPoints[i] < 0) { c.closePath(); start = true; ++i; continue; }
        const float px = x + kPoints[i] * k, py = y + kPoints[i + 1] * k;
        if (start) c.moveTo(px, py); else c.lineTo(px, py);
        start = false;
        i += 2;
    }
    c.fillColor(colour);
    c.fill();
    return 1698.0f * k;
}

/** The header of a plugin: the logo with the plugin's name behind it. */
template <class C> void title(C& c, const char* name, const DGL_NAMESPACE::Color& colour) {
    const float end = 16 + logo(c, 16, 20.5f, 13, colour);
    label(c, name, end + 7, 27, 18, colour, C::ALIGN_LEFT | C::ALIGN_MIDDLE);
}

// ---------------------------------------------------------------- meters

/** A mark on a meter scale, at `pos` 0..1 along it. rank 2: long, with `text`; 1: medium; 0: short. */
struct MeterMark {
    float pos;
    int rank;
    std::string text;
};

/** What a meter measures; the same scale serves all three designs. */
struct MeterScale {
    std::vector<MeterMark> marks;
    float hot = 1.0f;      // from here on the scale is red
    float warn = 1.0f;     // from here on the lights are yellow (the same as `hot` for none)
    float zero = 0.0f;     // where a bar starts: 0 for a level, 0.5 for a change up or down
    float inkFrom = 0.0f;  // where the printed arc begins
    float low = 0.0f, high = 1.03f; // how far the needle can go
    float span = 0.78f;    // radians on either side of straight up
    const char* unit = "dB";
    const char* hotLabel = nullptr; // a word over the red lights
};

/** The printed card (face..faceRed) and the dark designs (well..red). */
struct MeterColours {
    uint32_t face, faceEdge, ink, faceRed, well, edge, track, text, dim, accent, yellow, red;
};

enum { kMeterClassic, kMeterArc, kMeterLeds, kMeterStyles };

namespace meter {

struct Dial { // an arc around a pivot under the face
    float cx, cy, span;
    float angle(float pos) const { return -1.5707963f - span + pos * 2 * span; }
    void point(float pos, float r, float& x, float& y) const {
        x = cx + std::cos(angle(pos)) * r;
        y = cy + std::sin(angle(pos)) * r;
    }
};

template <class C> void arcStroke(C& c, const Dial& d, float r, float from, float to, float width, const DGL_NAMESPACE::Color& colour) {
    if (to - from < 0.002f) return;
    c.beginPath();
    c.arc(d.cx, d.cy, r, d.angle(from), d.angle(to), C::CW);
    c.strokeColor(colour);
    c.strokeWidth(width);
    c.lineCap(C::BUTT);
    c.stroke();
}

template <class C> void line(C& c, float x0, float y0, float x1, float y1, float width, const DGL_NAMESPACE::Color& colour) {
    c.beginPath();
    c.moveTo(x0, y0);
    c.lineTo(x1, y1);
    c.strokeColor(colour);
    c.strokeWidth(width);
    c.stroke();
}

template <class C> void face(C& c, const Box& f, uint32_t fill, uint32_t edge, float width) {
    c.beginPath();
    c.roundedRect(f.x, f.y, f.w, f.h, 5);
    c.fillColor(rgb(fill));
    c.fill();
    c.strokeColor(rgb(edge));
    c.strokeWidth(width);
    c.stroke();
}

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** A needle over a printed card. */
template <class C> void classic(C& c, const Box& f, const MeterScale& s, const MeterColours& k, float pos, const char* caption) {
    face(c, f, k.face, k.faceEdge, 1.5f);
    c.beginPath(); // a printed border, as on a meter card
    c.roundedRect(f.x + 5, f.y + 5, f.w - 10, f.h - 10, 3);
    c.strokeColor(rgb(k.faceEdge, 0.55f));
    c.strokeWidth(0.8f);
    c.stroke();

    const float radius = 128, bottom = f.y + f.h;
    const Dial d = {f.x + f.w * 0.5f, bottom + 40, s.span};
    arcStroke(c, d, radius + 4.5f, s.hot, 1.0f, 9, rgb(k.faceRed)); // a red band the marks stand in
    arcStroke(c, d, radius, s.inkFrom, 1.0f, 1.6f, rgb(k.ink));
    for (const MeterMark& m : s.marks) {
        float x0, y0, x1, y1;
        d.point(m.pos, radius, x0, y0);
        d.point(m.pos, radius + (m.rank == 2 ? 11 : (m.rank == 1 ? 7 : 4)), x1, y1);
        line(c, x0, y0, x1, y1, m.rank == 2 ? 1.8f : (m.rank == 1 ? 1.1f : 0.7f), rgb(k.ink));
        if (m.rank != 2) continue;
        d.point(m.pos, radius + 22, x1, y1);
        label(c, m.text.c_str(), x1, y1, 11, rgb(m.pos > s.hot + 0.001f ? k.faceRed : k.ink), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
    }
    logo(c, f.x + 14, bottom - 21, 7, rgb(k.ink, 0.6f)); // captions in the corners, clear of the needle
    const std::string text = std::string(caption) + "  " + s.unit;
    label(c, text.c_str(), f.x + f.w - 14, bottom - 17.5f, 7.5f, rgb(k.ink, 0.65f), C::ALIGN_RIGHT | C::ALIGN_MIDDLE);

    // The needle: a shadow first, then a blade that narrows to its tip, out of a cover over the pivot.
    const float at = clamp(pos, s.low, s.high), a = d.angle(at), nx = -std::sin(a), ny = std::cos(a); // nx, ny: across the needle
    float x0, y0, x1, y1;
    d.point(at, (d.cy - (bottom - 12)) / -std::sin(a), x0, y0);
    d.point(at, radius + 8, x1, y1);
    for (int pass = 0; pass < 2; ++pass) {
        const float ox = pass == 0 ? 2.0f : 0.0f, oy = pass == 0 ? 2.5f : 0.0f;
        c.beginPath();
        c.moveTo(x0 + nx * 1.5f + ox, y0 + ny * 1.5f + oy);
        c.lineTo(x1 + nx * 0.35f + ox, y1 + ny * 0.35f + oy);
        c.lineTo(x1 - nx * 0.35f + ox, y1 - ny * 0.35f + oy);
        c.lineTo(x0 - nx * 1.5f + ox, y0 - ny * 1.5f + oy);
        c.closePath();
        c.fillColor(pass == 0 ? rgb(k.ink, 0.16f) : rgb(k.ink));
        c.fill();
    }
    c.beginPath();
    c.moveTo(d.cx - 40, bottom - 0.75f);
    c.lineTo(d.cx - 30, bottom - 15);
    c.lineTo(d.cx + 30, bottom - 15);
    c.lineTo(d.cx + 40, bottom - 0.75f);
    c.closePath();
    c.fillColor(rgb(k.ink));
    c.fill();
    c.beginPath();
    c.circle(d.cx, bottom - 7.5f, 2.2f);
    c.fillColor(rgb(k.faceEdge));
    c.fill();
}

/** A dark face with an arc that fills, a marker and the figure in the middle. */
template <class C> void arc(C& c, const Box& f, const MeterScale& s, const MeterColours& k, float pos, const char* figure, const char* caption) {
    face(c, f, k.well, k.edge, 1);
    const float radius = 128, at = clamp(pos, 0.0f, 1.0f);
    const Dial d = {f.x + f.w * 0.5f, f.y + f.h + 34, s.span + 0.02f};
    arcStroke(c, d, radius, 0.0f, s.hot, 9, rgb(k.track));
    arcStroke(c, d, radius, s.hot, 1.0f, 9, rgb(k.red, 0.28f));
    const float lo = at < s.zero ? at : s.zero, hi = at < s.zero ? s.zero : at; // the bar, from where it starts to the reading
    arcStroke(c, d, radius, lo, hi < s.hot ? hi : s.hot, 9, rgb(k.accent));
    arcStroke(c, d, radius, lo > s.hot ? lo : s.hot, hi, 9, rgb(k.red));
    for (const MeterMark& m : s.marks) {
        if (m.rank == 0) continue;
        float x0, y0, x1, y1;
        d.point(m.pos, radius + 8, x0, y0);
        d.point(m.pos, radius + (m.rank == 2 ? 14 : 11), x1, y1);
        line(c, x0, y0, x1, y1, 1, rgb(m.rank == 2 ? k.text : k.dim, m.rank == 2 ? 0.8f : 0.6f));
        if (m.rank != 2) continue;
        d.point(m.pos, radius + 24, x1, y1);
        label(c, m.text.c_str(), x1, y1, 9, rgb(m.pos > s.hot + 0.001f ? k.red : k.dim), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
    }
    float x0, y0, x1, y1; // the marker at the level of the moment
    d.point(at, radius - 9, x0, y0);
    d.point(at, radius + 7, x1, y1);
    line(c, x0, y0, x1, y1, 2, rgb(k.text));

    label(c, figure, d.cx, f.y + 96, 30, rgb(at > s.hot + 0.001f ? k.red : k.text), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
    const std::string text = std::string(s.unit) + " " + caption;
    label(c, text.c_str(), d.cx, f.y + 120, 8, rgb(k.dim), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
}

/** A row of forty lights. */
template <class C> void leds(C& c, const Box& f, const MeterScale& s, const MeterColours& k, float pos, const char* figure, const char* caption) {
    face(c, f, k.well, k.edge, 1);
    const float x = f.x + 14, w = f.w - 28, top = f.y + 50, height = 44, at = clamp(pos, 0.0f, 1.0f);
    const int count = 40;
    const float pitch = w / count, slack = 0.1f / count;
    for (int i = 0; i < count; ++i) {
        const float from = static_cast<float>(i) / count, to = static_cast<float>(i + 1) / count;
        // Upwards a light is on once the reading is past its start; downwards from the start of the bar, past its end.
        const bool on = from >= s.zero - slack ? at > from + slack : at < to - slack;
        const uint32_t colour = from >= s.hot - slack ? k.red : (from >= s.warn - slack ? k.yellow : k.accent);
        c.beginPath();
        c.roundedRect(x + i * pitch + 0.9f, top, pitch - 1.8f, height, 1.2f);
        c.fillColor(rgb(colour, on ? 1.0f : 0.14f));
        c.fill();
    }
    for (const MeterMark& m : s.marks) {
        if (m.rank != 2) continue;
        const float mx = x + m.pos * w;
        line(c, mx, top + height + 4, mx, top + height + 9, 1, rgb(k.dim));
        const int align = m.pos < 0.01f ? C::ALIGN_LEFT : (m.pos > 0.99f ? C::ALIGN_RIGHT : C::ALIGN_CENTER);
        label(c, m.text.c_str(), mx, top + height + 19, 9, rgb(m.pos > s.hot + 0.001f ? k.red : k.dim), align | C::ALIGN_MIDDLE);
    }
    label(c, caption, x, f.y + 20, 9, rgb(k.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
    if (s.hotLabel != nullptr) label(c, s.hotLabel, x + s.hot * w, top - 9, 7, rgb(k.red, 0.8f), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
    label(c, s.unit, x + w, f.y + 24, 9, rgb(k.dim), C::ALIGN_RIGHT | C::ALIGN_MIDDLE);
    label(c, figure, x + w - 20, f.y + 22, 22, rgb(at > s.hot + 0.001f ? k.red : k.text), C::ALIGN_RIGHT | C::ALIGN_MIDDLE);
}

} // namespace meter

/**
 * Draws a meter in one of the three designs on the face `f`. `pos` is the reading along the
 * scale (0..1, may lie outside), `figure` the reading as text for the designs that print it.
 */
template <class C> void drawMeter(C& c, int style, const Box& f, const MeterScale& s, const MeterColours& k, float pos, const char* figure, const char* caption) {
    if (!(pos == pos)) pos = s.zero; // not a number
    if (style == kMeterArc) meter::arc(c, f, s, k, pos, figure, caption);
    else if (style == kMeterLeds) meter::leds(c, f, s, k, pos, figure, caption);
    else meter::classic(c, f, s, k, pos, caption);
}

/**
 * The choice of meter design: an arrow at either end of the strip under the meter and the
 * name of the design between them. name() is what a plugin stores with the song.
 */
class MeterSwitch {
public:
    MeterSwitch(float left, float right, float y) : prev_{left, y, 17, 14}, next_{right - 17, y, 17, 14} {}

    int style() const { return style_; }
    const char* name() const { return names()[style_]; }
    /** Takes a stored name; an unknown one (a song from a later version) leaves the design as it is. */
    bool set(const char* name) {
        for (int i = 0; i < kMeterStyles; ++i)
            if (std::string(name) == names()[i]) { style_ = i; return true; }
        return false;
    }
    /** True when the click was on an arrow and the design changed. */
    bool mouseDown(float x, float y) {
        if (!prev_.contains(x, y) && !next_.contains(x, y)) return false;
        style_ = (style_ + (next_.contains(x, y) ? 1 : kMeterStyles - 1)) % kMeterStyles;
        return true;
    }
    Box area() const { return {prev_.x, prev_.y - 2, next_.x + next_.w - prev_.x, prev_.h + 4}; }
    const Box& prevBox() const { return prev_; }
    const Box& nextBox() const { return next_; }
    static const char* help() {
        return "Meter design: the arrows step through three looks of the meter. The choice is kept with the song and changes nothing about the sound.";
    }

    template <class C> void draw(C& c, const Theme& t) const {
        static const char* const kLabels[kMeterStyles] = {"CLASSIC", "ARC", "LEDS"};
        for (const Box* b : {&prev_, &next_}) {
            c.beginPath();
            c.roundedRect(b->x, b->y, b->w, b->h, 3);
            c.fillColor(rgb(t.track));
            c.fill();
            const float cx = b->x + b->w * 0.5f, cy = b->y + b->h * 0.5f, d = b == &prev_ ? -1.0f : 1.0f;
            triangle(c, cx + d * 3, cy, cx - d * 2.5f, cy - 3.5f, cx - d * 2.5f, cy + 3.5f, rgb(t.text));
        }
        label(c, kLabels[style_], (prev_.x + next_.x + next_.w) * 0.5f, prev_.y + prev_.h * 0.5f, 8, rgb(t.dim), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
    }

private:
    static const char* const* names() {
        static const char* const kNames[kMeterStyles] = {"classic", "arc", "leds"};
        return kNames;
    }
    Box prev_, next_;
    int style_ = kMeterClassic;
};

// ------------------------------------------------------------ preset bar

/**
 * Previous / name / next, with a drop-down list under the name. The list ends with a
 * "Save as..." row; choosing it turns the name into a text box (Enter or the tick saves,
 * Escape or the cross leaves it). Presets from `firstUser` on are the user's own and have a
 * cross to delete them, which asks once more before it does. A list of more than kMaxRows
 * continues in further columns: to the right, or to the left when the bar stands so far to
 * the right that two more columns fit on its left (a bar next to the right edge). A bar that
 * was told the width of its window (moveTo) takes the side with the most room.
 */
class PresetBar {
public:
    static constexpr float kWidth = 222, kHeight = 24, kRow = 22;
    static constexpr int kMaxRows = 16; // a longer list continues in a further column
    static constexpr size_t kMaxText = 20; // characters in a name
    /** mouseDown and key results below zero. kSave: text() is the name. kDelete: target() is the preset. kEdit: call startEdit(). */
    enum { kNotMine = -2, kHandled = -1, kSave = -3, kDelete = -4, kEdit = -5 };

    PresetBar(float x, float y) : prev_{x, y, 22, kHeight}, name_{x + 26, y, 170, kHeight}, next_{x + 200, y, 22, kHeight} {}

    /** Puts the bar somewhere else, for a window that changes width; `canvasWidth` is that width. */
    void moveTo(float x, float canvasWidth) {
        prev_.x = x;
        name_.x = x + 26;
        next_.x = x + 200;
        canvas_ = canvasWidth;
    }

    bool isOpen() const { return open_; }
    bool isEditing() const { return editing_; }
    /** True while the bar wants every click and key. */
    bool isBusy() const { return open_ || editing_; }
    const Box& nameBox() const { return name_; }
    const std::string& text() const { return text_; }
    int target() const { return target_; }

    /** Where things are, for the tests. */
    Box rowBox(int row) const {
        const Box m = menuOrigin();
        return {m.x + (leftward() ? -1 : 1) * (row / kMaxRows) * name_.w, m.y + 4 + (row % kMaxRows) * kRow, name_.w, kRow};
    }
    Box deleteBox(int row) const {
        const Box r = rowBox(row);
        return {r.x + r.w - 26, r.y, 22, kRow};
    }
    const Box& cancelBox() const { return prev_; }
    const Box& saveBox() const { return next_; }

    /** Turns the name into a text box holding `text`. */
    void startEdit(const std::string& text) {
        open_ = false;
        editing_ = true;
        text_ = text;
    }

    /**
     * Returns the preset to load (>= 0) or one of the results above. `count` presets, of which
     * those from `firstUser` on can be deleted; `canSave` is false when the list is full.
     */
    int mouseDown(float x, float y, int current, int count, int firstUser, bool canSave) {
        if (editing_) {
            if (name_.contains(x, y)) return kHandled;
            editing_ = false;
            return next_.contains(x, y) && !text_.empty() ? kSave : kHandled; // a click anywhere else leaves it
        }
        if (open_) {
            const int row = rowAt(x, y, count + 1);
            if (row >= firstUser && row < count && deleteBox(row).contains(x, y)) {
                if (confirm_ != row) {
                    confirm_ = row;
                    return kHandled;
                }
                close();
                target_ = row;
                return kDelete;
            }
            close();
            if (row == count) return canSave ? kEdit : kHandled;
            return row >= 0 ? row : kHandled;
        }
        if (name_.contains(x, y)) {
            open_ = true;
            hover_ = current;
            return kHandled;
        }
        if (count <= 0) return kNotMine;
        if (prev_.contains(x, y)) return ((current < 0 ? 0 : current - 1) + count) % count;
        if (next_.contains(x, y)) return (current < 0 ? 0 : current + 1) % count;
        return kNotMine;
    }

    /** Tracks the row under the mouse while the list is open; true when it changed. */
    bool motion(float x, float y, int count) {
        const int row = rowAt(x, y, count + 1);
        if (row == hover_) return false;
        hover_ = row;
        return true;
    }

    /** A key went down. kSave, kHandled, or kNotMine when the bar is not using the keyboard. */
    int key(unsigned code) {
        if (open_ && code == 0x1B) {
            close();
            return kHandled;
        }
        if (!editing_) return kNotMine;
        if (code == 0x1B) {
            editing_ = false;
        } else if (code == 0x0D) {
            if (text_.empty()) return kHandled;
            editing_ = false;
            return kSave;
        } else if (code == 0x08) {
            while (!text_.empty() && (static_cast<unsigned char>(text_.back()) & 0xC0) == 0x80) text_.pop_back();
            if (!text_.empty()) text_.pop_back();
        }
        return kHandled;
    }

    /** Text was typed (UTF-8, one character). True when the bar took it. */
    bool character(const char* utf8) {
        if (!editing_) return false;
        const unsigned char c = static_cast<unsigned char>(utf8[0]);
        if (c < 0x20 || c == 0x7F) return true; // Enter, Backspace and the like also arrive as text
        for (const char* bad = "\\/:*?\"<>|"; *bad; ++bad)
            if (c == static_cast<unsigned char>(*bad)) return true;
        size_t characters = 0;
        for (unsigned char b : text_) characters += (b & 0xC0) != 0x80 ? 1 : 0;
        if (characters < kMaxText && !(text_.empty() && c == ' ')) text_ += utf8;
        return true;
    }

    template <class C> void drawBar(C& c, const Theme& t, const char* text) const {
        for (const Box* b : {&prev_, &name_, &next_}) {
            c.beginPath();
            c.roundedRect(b->x, b->y, b->w, b->h, 4);
            c.fillColor(rgb(b == &name_ ? t.panel : t.track));
            c.fill();
        }
        c.beginPath();
        c.roundedRect(name_.x, name_.y, name_.w, name_.h, 4);
        c.strokeColor(rgb(editing_ ? t.accent : t.edge));
        c.strokeWidth(1);
        c.stroke();

        const float my = prev_.y + prev_.h * 0.5f;
        if (editing_) {
            cross(c, prev_.x + prev_.w * 0.5f, my, 4, rgb(t.text));
            c.beginPath(); // a tick
            c.moveTo(next_.x + 6, my);
            c.lineTo(next_.x + 10, my + 4);
            c.lineTo(next_.x + 16, my - 4);
            c.strokeColor(rgb(text_.empty() ? t.dim : t.accent));
            c.strokeWidth(1.6f);
            c.stroke();
            const std::string shown = text_ + "|";
            label(c, shown.c_str(), name_.x + 10, my, 11, rgb(t.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
            return;
        }
        triangle(c, prev_.x + 14, my - 5, prev_.x + 14, my + 5, prev_.x + 8, my, rgb(t.text));
        triangle(c, next_.x + 8, my - 5, next_.x + 8, my + 5, next_.x + 14, my, rgb(t.text));
        triangle(c, name_.x + name_.w - 18, my - 2, name_.x + name_.w - 10, my - 2, name_.x + name_.w - 14, my + 3, rgb(t.dim));
        label(c, text, name_.x + 10, my, 11, rgb(t.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
    }

    /** Draw last, so the list lies over everything else. name(i) gives the name of preset i. */
    template <class C, class NameFn> void drawMenu(C& c, const Theme& t, int count, int current, int firstUser, bool canSave, NameFn name) const {
        if (!open_) return;
        const Box m = menuBox(count + 1);
        c.beginPath();
        c.roundedRect(m.x, m.y, m.w, m.h, 4);
        c.fillColor(rgb(t.popup));
        c.fill();
        c.strokeColor(rgb(t.popupEdge));
        c.strokeWidth(1);
        c.stroke();
        for (int i = 0; i <= count; ++i) {
            const Box r = rowBox(i);
            if (i == hover_) {
                c.beginPath();
                c.roundedRect(r.x + 4, r.y, r.w - 8, kRow, 3);
                c.fillColor(rgb(t.track));
                c.fill();
            }
            const float my = r.y + kRow * 0.5f;
            if (i == count) {
                label(c, canSave ? "Save as..." : "List full: delete one first", r.x + 12, my, 11, rgb(canSave ? t.accent : t.dim), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
            } else if (i == confirm_) {
                label(c, "Delete? Click x again", r.x + 12, my, 11, rgb(t.accent), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
            } else {
                label(c, name(i), r.x + 12, my, 11, rgb(i == current ? t.accent : t.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
            }
            if (i >= firstUser && i < count) cross(c, r.x + r.w - 15, my, 3, rgb(i == confirm_ ? t.accent : t.dim));
        }
    }

private:
    template <class C> static void cross(C& c, float cx, float cy, float r, const DGL_NAMESPACE::Color& colour) {
        c.beginPath();
        c.moveTo(cx - r, cy - r);
        c.lineTo(cx + r, cy + r);
        c.moveTo(cx + r, cy - r);
        c.lineTo(cx - r, cy + r);
        c.strokeColor(colour);
        c.strokeWidth(1.4f);
        c.stroke();
    }
    void close() {
        open_ = false;
        confirm_ = -1;
    }
    bool leftward() const { return canvas_ > 0 ? name_.x > canvas_ - name_.x - name_.w : name_.x >= 2 * name_.w; }
    Box menuOrigin() const { return {name_.x, name_.y + name_.h + 2, 0, 0}; }
    Box menuBox(int rows) const {
        const int shown = rows < kMaxRows ? rows : kMaxRows, columns = (rows + kMaxRows - 1) / kMaxRows;
        const Box o = menuOrigin();
        return {leftward() ? o.x - name_.w * (columns - 1) : o.x, o.y, name_.w * columns, shown * kRow + 8};
    }
    int rowAt(float x, float y, int rows) const {
        const Box m = menuBox(rows);
        if (!m.contains(x, y)) return -1;
        const int across = static_cast<int>((x - m.x) / name_.w), columns = (rows + kMaxRows - 1) / kMaxRows;
        const int row = static_cast<int>((y - m.y - 4) / kRow), column = leftward() ? columns - 1 - across : across;
        if (row < 0 || row >= kMaxRows) return -1;
        const int index = column * kMaxRows + row;
        return index < 0 || index >= rows ? -1 : index;
    }

    Box prev_, name_, next_;
    float canvas_ = 0;
    bool open_ = false, editing_ = false;
    int hover_ = -1, confirm_ = -1, target_ = -1;
    std::string text_;
};

// -------------------------------------------------------------- help tips

/**
 * A "?" button that switches help on. While it is on, resting the mouse on
 * a control shows what that control does.
 */
class HelpTips {
public:
    bool on = false;

    void setButton(float cx, float cy) { bx_ = cx; by_ = cy; }
    bool buttonHit(float x, float y) const { return std::hypot(x - bx_, y - by_) <= 13; }
    Box buttonBox() const { return {bx_ - 12, by_ - 12, 24, 24}; }

    void clear() { tips_.clear(); hover_ = -1; }
    /** Later tips lie on top of earlier ones: add wide areas first, single controls last. */
    void add(const Box& area, const char* title, const char* text) {
        if (text != nullptr && text[0] != '\0') tips_.push_back({area, title, text});
    }

    /** Follows the mouse; true when a repaint is needed. */
    bool motion(float x, float y) {
        int hit = -1;
        if (on)
            for (int i = static_cast<int>(tips_.size()) - 1; i >= 0 && hit < 0; --i)
                if (tips_[i].area.contains(x, y)) hit = i;
        const bool changed = hit != hover_ || (hit >= 0 && (x != mx_ || y != my_));
        hover_ = hit;
        mx_ = x;
        my_ = y;
        return changed;
    }
    void hide() { hover_ = -1; }

    template <class C> void drawButton(C& c, const Theme& t) const {
        c.beginPath();
        c.circle(bx_, by_, 11);
        c.fillColor(rgb(on ? t.accent : t.track));
        c.fill();
        label(c, "?", bx_, by_ + 0.5f, 13, rgb(on ? t.bg : t.text), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
    }

    /** Draw last. width and height are the window's size in design pixels. */
    template <class C> void drawTip(C& c, const Theme& t, float width, float height) {
        if (!on || hover_ < 0) return;
        const Tip& tip = tips_[hover_];
        const float w = 260, pad = 10, textW = w - 2 * pad;
        c.fontSize(11);
        c.textAlign(C::ALIGN_LEFT | C::ALIGN_TOP);
        c.textLineHeight(1.35f);
        float bounds[4] = {0, 0, 0, 0};
        c.textBoxBounds(0, 0, textW, tip.text, nullptr, bounds);
        const float h = pad + 16 + (bounds[3] - bounds[1]) + pad;

        // Under the control (above it when there is no room), so the control stays
        // visible. For a large area such as a meter, next to the mouse instead.
        const Box& area = tip.area;
        const bool large = area.h > 120;
        float x = large ? mx_ + 14 : area.x + area.w * 0.5f - w * 0.5f;
        float y = large ? my_ + 18 : area.y + area.h + 6;
        if (y + h > height - 6) y = (large ? my_ - 12 : area.y - 6) - h;
        if (x + w > width - 6) x = width - 6 - w;
        if (x < 6) x = 6;
        if (y < 6) y = 6;

        c.beginPath();
        c.roundedRect(x, y, w, h, 5);
        c.fillColor(rgb(t.popup));
        c.fill();
        c.strokeColor(rgb(t.popupEdge));
        c.strokeWidth(1);
        c.stroke();
        label(c, tip.title, x + pad, y + pad, 10, rgb(t.accent), C::ALIGN_LEFT | C::ALIGN_TOP);
        c.fontSize(11);
        c.textAlign(C::ALIGN_LEFT | C::ALIGN_TOP);
        c.fillColor(rgb(t.text));
        c.textBox(x + pad, y + pad + 16, textW, tip.text, nullptr);
    }

private:
    struct Tip {
        Box area;
        const char* title;
        const char* text;
    };
    std::vector<Tip> tips_;
    int hover_ = -1;
    float bx_ = 0, by_ = 0, mx_ = 0, my_ = 0;
};

} // namespace ui
} // namespace hnl
