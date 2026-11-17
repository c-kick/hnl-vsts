// UI parts shared by the plugins: the preset bar and the help tips.
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

// ------------------------------------------------------------ preset bar

/**
 * Previous / name / next, with a drop-down list under the name. The list ends with a
 * "Save as..." row; choosing it turns the name into a text box (Enter or the tick saves,
 * Escape or the cross leaves it). Presets from `firstUser` on are the user's own and have a
 * cross to delete them, which asks once more before it does.
 */
class PresetBar {
public:
    static constexpr float kWidth = 222, kHeight = 24, kRow = 22;
    static constexpr int kMaxRows = 16; // a longer list continues in a further column
    static constexpr size_t kMaxText = 20; // characters in a name
    /** mouseDown and key results below zero. kSave: text() is the name. kDelete: target() is the preset. kEdit: call startEdit(). */
    enum { kNotMine = -2, kHandled = -1, kSave = -3, kDelete = -4, kEdit = -5 };

    PresetBar(float x, float y) : prev_{x, y, 22, kHeight}, name_{x + 26, y, 170, kHeight}, next_{x + 200, y, 22, kHeight} {}

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
        return {m.x + (row / kMaxRows) * name_.w, m.y + 4 + (row % kMaxRows) * kRow, name_.w, kRow};
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
    Box menuOrigin() const { return {name_.x, name_.y + name_.h + 2, 0, 0}; }
    Box menuBox(int rows) const {
        const int shown = rows < kMaxRows ? rows : kMaxRows, columns = (rows + kMaxRows - 1) / kMaxRows;
        const Box o = menuOrigin();
        return {o.x, o.y, name_.w * columns, shown * kRow + 8};
    }
    int rowAt(float x, float y, int rows) const {
        const Box m = menuBox(rows);
        if (!m.contains(x, y)) return -1;
        const int row = static_cast<int>((y - m.y - 4) / kRow), column = static_cast<int>((x - m.x) / name_.w);
        if (row < 0 || row >= kMaxRows) return -1;
        const int index = column * kMaxRows + row;
        return index < 0 || index >= rows ? -1 : index;
    }

    Box prev_, name_, next_;
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
