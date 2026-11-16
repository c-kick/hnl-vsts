// UI parts shared by the plugins: the preset bar and the help tips.
//
// Include after DistrhoUI.hpp. The drawing functions are templates on the
// canvas (the plugin's UI class) so they also work with the SVG stand-in in
// tools/ui-preview. Coordinates are design pixels, as in the plugin UIs.
#pragma once

#include <cmath>
#include <cstdint>
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

/** Previous / name / next, with a drop-down list under the name. */
class PresetBar {
public:
    static constexpr float kWidth = 222, kHeight = 24, kRow = 22;
    static constexpr int kMaxRows = 16; // a longer list continues in a further column
    enum { kNotMine = -2, kHandled = -1 }; // mouseDown results below zero

    PresetBar(float x, float y) : prev_{x, y, 22, kHeight}, name_{x + 26, y, 170, kHeight}, next_{x + 200, y, 22, kHeight} {}

    bool isOpen() const { return open_; }
    const Box& nameBox() const { return name_; }

    /** Returns the preset to load (>= 0), kHandled, or kNotMine when the click was elsewhere. */
    int mouseDown(float x, float y, int current, int count) {
        if (open_) {
            open_ = false;
            const int row = rowAt(x, y, count);
            return row >= 0 ? row : kHandled;
        }
        if (name_.contains(x, y)) {
            open_ = true;
            hover_ = current;
            return kHandled;
        }
        if (prev_.contains(x, y)) return ((current < 0 ? 0 : current - 1) + count) % count;
        if (next_.contains(x, y)) return (current < 0 ? 0 : current + 1) % count;
        return kNotMine;
    }

    /** Tracks the row under the mouse while the list is open; true when it changed. */
    bool motion(float x, float y, int count) {
        const int row = rowAt(x, y, count);
        if (row == hover_) return false;
        hover_ = row;
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
        c.strokeColor(rgb(t.edge));
        c.strokeWidth(1);
        c.stroke();

        const float my = prev_.y + prev_.h * 0.5f;
        triangle(c, prev_.x + 14, my - 5, prev_.x + 14, my + 5, prev_.x + 8, my, rgb(t.text));
        triangle(c, next_.x + 8, my - 5, next_.x + 8, my + 5, next_.x + 14, my, rgb(t.text));
        triangle(c, name_.x + name_.w - 18, my - 2, name_.x + name_.w - 10, my - 2, name_.x + name_.w - 14, my + 3, rgb(t.dim));
        label(c, text, name_.x + 10, my, 11, rgb(t.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
    }

    /** Draw last, so the list lies over everything else. name(i) gives the name of preset i. */
    template <class C, class NameFn> void drawMenu(C& c, const Theme& t, int count, int current, NameFn name) const {
        if (!open_) return;
        const Box m = menuBox(count);
        c.beginPath();
        c.roundedRect(m.x, m.y, m.w, m.h, 4);
        c.fillColor(rgb(t.popup));
        c.fill();
        c.strokeColor(rgb(t.popupEdge));
        c.strokeWidth(1);
        c.stroke();
        for (int i = 0; i < count; ++i) {
            const float x = m.x + (i / kMaxRows) * name_.w, y = m.y + 4 + (i % kMaxRows) * kRow;
            if (i == hover_) {
                c.beginPath();
                c.roundedRect(x + 4, y, name_.w - 8, kRow, 3);
                c.fillColor(rgb(t.track));
                c.fill();
            }
            label(c, name(i), x + 12, y + kRow * 0.5f, 11, rgb(i == current ? t.accent : t.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
        }
    }

private:
    Box menuBox(int count) const {
        const int rows = count < kMaxRows ? count : kMaxRows, columns = (count + kMaxRows - 1) / kMaxRows;
        return {name_.x, name_.y + name_.h + 2, name_.w * columns, rows * kRow + 8};
    }
    int rowAt(float x, float y, int count) const {
        const Box m = menuBox(count);
        if (!m.contains(x, y)) return -1;
        const int row = static_cast<int>((y - m.y - 4) / kRow), column = static_cast<int>((x - m.x) / name_.w);
        if (row < 0 || row >= kMaxRows) return -1;
        const int index = column * kMaxRows + row;
        return index < 0 || index >= count ? -1 : index;
    }

    Box prev_, name_, next_;
    bool open_ = false;
    int hover_ = -1;
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
