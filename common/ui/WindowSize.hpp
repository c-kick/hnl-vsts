// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The size of a plugin's window, for a UI that draws in a fixed design space: the window opens
// at the size it was left at, takes a size from the cog's list (SkinMenu in Skin.hpp) or from
// being dragged by its edge, and the drawing follows it.
//
// A UI that uses it:
//   - sets DISTRHO_UI_USER_RESIZABLE to 1
//   - calls open() in its constructor
//   - scales its drawing by zoom(), and divides the place of the mouse by it
//   - calls choose(menu.size()) when SkinMenu::mouseDown() answers kSized
//   - calls follow() from uiIdle(), and repaints when it says true
//   - calls design() when the size it was designed at changes (a window whose width depends on what is in it)
#pragma once

#include <algorithm>
#include <cmath>

#include "Skin.hpp"

namespace hnl {
namespace ui {

template <class Ui>
class WindowSize {
public:
    /** `plugin`: the short name the size is kept under ("analyzer"). `width` x `height`: the size the UI was designed at. */
    WindowSize(Ui& ui, const char* plugin, float width, float height) : ui_(ui), plugin_(plugin), width_(width), height_(height) {}

    /** The window takes the size it was left at, and never gets smaller than three quarters of its design. */
    void open(SkinMenu& menu) {
        percent_ = storedSize(plugin_);
        menu.offerSizes(percent_);
        open_ = true;
        fit();
    }

    /** The design itself has another size from now on: the window follows, at the percentage it had. */
    void design(float width, float height) {
        if (width == width_ && height == height_) return;
        width_ = width;
        height_ = height;
        if (open_) fit();
    }

    /** How many pixels of the window a unit of the design takes. The whole design stays in sight when the window has another shape. */
    float zoom() const {
        const float z = std::min(static_cast<float>(ui_.getWidth()) / width_, static_cast<float>(ui_.getHeight()) / height_);
        return z > 0.05f ? z : 1.0f;
    }

    /** A size from the list: the window follows at once, and the size is kept. */
    void choose(int percent) {
        percent_ = cleanSize(percent);
        resizeTo(percent_);
        keep(percent_);
    }

    /**
     * A window dragged by its edge: once it has kept one size for about a second, that size is the
     * one it opens at next time. Not while it moves: that would write the settings for every pixel.
     * True when the list of sizes changed and the window is to be drawn again.
     */
    bool follow(SkinMenu& menu) {
        const unsigned w = ui_.getWidth();
        if (w != seen_) {
            seen_ = w;
            still_ = 0;
            return false;
        }
        if (w == kept_ || ++still_ < SkinMenu::kPollEvery) return false;
        const int size = cleanSize(static_cast<int>(std::lround(100.0 * w / (width_ * hostScale()))));
        percent_ = size;
        menu.offerSizes(size);
        keep(size);
        return true;
    }

private:
    /** The host's own scale for windows (a display of high resolution), which the user's size comes on top of. */
    double hostScale() const {
        const double s = ui_.getScaleFactor();
        return s > 0.1 && s < 20.0 ? s : 1.0;
    }
    /** The least size the window may be dragged to, and the size it has at its percentage. */
    void fit() {
        const double s = hostScale();
        ui_.setGeometryConstraints(static_cast<unsigned>(std::lround(width_ * s * kSizeLeast / 100.0)), static_cast<unsigned>(std::lround(height_ * s * kSizeLeast / 100.0)), true, false);
        resizeTo(percent_);
        kept_ = seen_ = ui_.getWidth();
    }
    void resizeTo(int percent) {
        const double s = hostScale() * cleanSize(percent) / 100.0;
        const unsigned w = static_cast<unsigned>(std::lround(width_ * s)), h = static_cast<unsigned>(std::lround(height_ * s));
        if (w != ui_.getWidth() || h != ui_.getHeight()) ui_.setSize(w, h);
    }
    void keep(int percent) {
        storeSize(plugin_, percent); // when it cannot be written, the size still holds for this window
        kept_ = seen_ = ui_.getWidth();
    }

    Ui& ui_;
    const char* plugin_;
    float width_, height_;
    unsigned seen_ = 0, kept_ = 0; // the window's width now, and the one whose size was last kept
    int still_ = 0, percent_ = 100;
    bool open_ = false;
};

} // namespace ui
} // namespace hnl
