// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The look of a plugin window. "Classic" is the flat look every plugin was made with and
// stays the default; the others are built in beside it and dress the same layout as a piece
// of hi-fi equipment: a brushed front plate, display windows, turned knobs and keys. The
// choice is the user's, holds for every HNL plugin and is kept in the settings file
// (common/UserSettings.hpp), not in the song: it changes nothing about the sound.
//
// A plugin draws its window through the parts below (faceplate, panel, knob, keys, ...),
// each of which draws the flat or the solid version, with the colours of a Skin. Where
// things stand is the same in every look, so clicks, help areas and tests do not depend on it.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "../UserSettings.hpp"
#include "Widgets.hpp"

namespace hnl {
namespace ui {

enum { kSkinClassic, kSkinHifiBlack, kSkinHifiSilver, kSkinCount };

/** What the settings file holds, and what the list shows. */
inline const char* skinName(int skin) {
    static const char* const kNames[kSkinCount] = {"classic", "hifi-black", "hifi-silver"};
    return kNames[skin];
}
inline const char* skinLabel(int skin) {
    static const char* const kLabels[kSkinCount] = {"Classic", "Hi-Fi Black", "Hi-Fi Silver"};
    return kLabels[skin];
}

/**
 * The colours of a look. `solid` is false for the flat look, where the front plate and the
 * displays are one dark surface and the plate colours equal the display colours.
 */
struct Skin {
    bool solid;
    // The front plate and what is printed on it.
    uint32_t bg, bgLow, plateText, plateDim, groove;
    // Panels and displays: light on dark.
    uint32_t panel, panelEdge, track, well, text, dim, accent, popup, popupEdge;
    // Small buttons (the preset arrows, help), knobs and keys.
    uint32_t button, knobHi, knobLo, knobEdge, pointer, keyHi, keyLo, keyText;
    // The printed meter card, and the level colours.
    uint32_t face, faceEdge, ink, faceRed, green, yellow, red;
    // Around a knob: the colour of the arc that shows its value, and whether a scale is printed there.
    uint32_t ring;
    bool scale;

    Theme theme() const { return {solid ? well : bg, panel, panelEdge, button, popup, popupEdge, text, dim, accent}; }
    MeterColours meter() const { return {face, faceEdge, ink, faceRed, well, panelEdge, track, text, dim, accent, yellow, red}; }
};

/** The flat look in a plugin's own colours. */
inline Skin classicSkin(uint32_t bg, uint32_t panel, uint32_t panelEdge, uint32_t track, uint32_t well, uint32_t text, uint32_t dim,
                        uint32_t accent, uint32_t popup, uint32_t popupEdge) {
    return {false, bg, bg, text, dim, panelEdge,
            panel, panelEdge, track, well, text, dim, accent, popup, popupEdge,
            track, 0x2a2e35, 0x2a2e35, 0x2a2e35, text, track, track, text,
            0xe6e2d6, 0xb3ad9c, 0x22252a, 0xb83a2b, 0x4cd27a, 0xe8d44d, 0xef5b5b, accent, false};
}

/** The built-in looks other than Classic: the same for every plugin, as a line of equipment is. */
inline const Skin& solidSkin(int skin) {
    // The displays are the same in both: cool white figures, amber for what is on, red for too much.
    static const Skin kBlack = {true, 0x222224, 0x141415, 0xd8d8d4, 0x8f8f8b, 0x000000,
                                0x0a0c0d, 0x000000, 0x2a2118, 0x030405, 0xe2eef0, 0x7d9195, 0xffae3d, 0x0e1112, 0x33393b,
                                0x2c2c2f, 0x77777c, 0x0c0c0e, 0x000000, 0xe9e9e6, 0x3d3d41, 0x1f1f22, 0xd8d8d4,
                                0xf5cd80, 0x8f6a2c, 0x1e1810, 0xa52a14, 0xa4e9f0, 0xffb84a, 0xff4a3a, 0xffae3d, false};
    // Satin aluminium with dark print, and knobs of turned metal: nothing on the plate lights up.
    static const Skin kSilver = {true, 0xd6d5d2, 0xbcbbb8, 0x26282b, 0x5a5d60, 0x2c2e31,
                                 0x0a0c0d, 0x000000, 0x2a2118, 0x030405, 0xe2eef0, 0x7d9195, 0xffae3d, 0x0e1112, 0x33393b,
                                 0x2c2c2f, 0xfbfbfa, 0x96979a, 0x6c6e71, 0x26282b, 0xeeeeec, 0xb8b9bb, 0x26282b,
                                 0xf5cd80, 0x8f6a2c, 0x1e1810, 0xa52a14, 0xa4e9f0, 0xffb84a, 0xff4a3a, 0x26282b, true};
    return skin == kSkinHifiSilver ? kSilver : kBlack;
}

/**
 * A plugin's or a module's own colour as it stands around a knob: as it is where the arc is
 * a ring of light, darker where it is printed on bare metal.
 */
inline uint32_t printed(const Skin& k, uint32_t colour) {
    if (!k.scale) return colour;
    const uint32_t r = ((colour >> 16) & 255) * 5 / 8, g = ((colour >> 8) & 255) * 5 / 8, b = (colour & 255) * 5 / 8;
    return (r << 16) | (g << 8) | b;
}

/** A built-in look with the plugin's own colour for what is on or lit, so that a plugin is still told by its colour. */
inline Skin solidSkin(int skin, uint32_t accent) {
    Skin k = solidSkin(skin);
    k.accent = accent;
    k.ring = printed(k, accent);
    return k;
}

// ------------------------------------------------------------------ parts

/** The whole window: one colour, or a brushed plate that is lighter at the top. */
template <class C> void faceplate(C& c, const Skin& k, float w, float h) {
    c.beginPath();
    c.rect(0, 0, w, h);
    if (!k.solid) {
        c.fillColor(rgb(k.bg));
        c.fill();
        return;
    }
    c.fillPaint(c.linearGradient(0, 0, 0, h, rgb(k.bg), rgb(k.bgLow)));
    c.fill();
    // The brushing: fine lines across, some darker and some lighter than the plate, in a fixed irregular order.
    for (int pass = 0; pass < 2; ++pass) {
        c.beginPath();
        uint32_t seed = 12345u + static_cast<uint32_t>(pass) * 977u;
        for (float y = 0.5f; y < h; y += 1.0f) {
            seed = seed * 1664525u + 1013904223u;
            if ((seed >> 24) % 3 != 0) continue;
            c.moveTo(0, y);
            c.lineTo(w, y);
        }
        const bool bright = ((k.bg >> 8) & 255) > 128; // bare metal shows its brushing more than a black finish
        c.strokeColor(pass == 0 ? rgb(0x000000, bright ? 0.03f : 0.06f) : rgb(0xffffff, bright ? 0.07f : 0.02f));
        c.strokeWidth(1);
        c.stroke();
    }
    if (((k.bg >> 8) & 255) > 128) { // bare metal: a soft band of light across the plate
        c.beginPath();
        c.rect(0, 0, w, h);
        c.fillPaint(c.linearGradient(0, h * 0.05f, 0, h * 0.5f, rgb(0xffffff, 0.12f), rgb(0xffffff, 0.0f)));
        c.fill();
    }
    c.beginPath(); // the light catching the upper edge
    c.moveTo(0, 0.5f);
    c.lineTo(w, 0.5f);
    c.strokeColor(rgb(0xffffff, 0.22f));
    c.strokeWidth(1);
    c.stroke();
    c.beginPath();
    c.moveTo(0, h - 0.5f);
    c.lineTo(w, h - 0.5f);
    c.strokeColor(rgb(0x000000, 0.4f));
    c.stroke();
}

/** A line engraved in the plate: dark, with the light catching its lower side. */
template <class C> void groove(C& c, const Skin& k, float x0, float y0, float x1, float y1) {
    if (k.solid) {
        const bool upright = x0 == x1;
        c.beginPath();
        c.moveTo(x0 + (upright ? 1 : 0), y0 + (upright ? 0 : 1));
        c.lineTo(x1 + (upright ? 1 : 0), y1 + (upright ? 0 : 1));
        c.strokeColor(rgb(0xffffff, 0.14f));
        c.strokeWidth(1);
        c.stroke();
    }
    c.beginPath();
    c.moveTo(x0, y0);
    c.lineTo(x1, y1);
    c.strokeColor(rgb(k.groove, k.solid ? 0.45f : 1.0f));
    c.strokeWidth(1);
    c.stroke();
}

/** A group of controls on the plate. Flat: a panel. Solid: only an engraved outline. */
template <class C> void platePanel(C& c, const Skin& k, const Box& p) {
    if (!k.solid) {
        c.beginPath();
        c.roundedRect(p.x, p.y, p.w, p.h, 6);
        c.fillColor(rgb(k.panel));
        c.fill();
        c.strokeColor(rgb(k.panelEdge));
        c.strokeWidth(1);
        c.stroke();
        return;
    }
    c.beginPath();
    c.roundedRect(p.x + 0.5f, p.y + 1.5f, p.w - 1, p.h - 1, 6);
    c.strokeColor(rgb(0xffffff, 0.14f));
    c.strokeWidth(1);
    c.stroke();
    c.beginPath();
    c.roundedRect(p.x + 0.5f, p.y + 0.5f, p.w - 1, p.h - 1, 6);
    c.strokeColor(rgb(k.groove, 0.45f));
    c.stroke();
}

/** Where meters and figures are shown. Flat: a panel. Solid: a window of dark glass set into the plate. */
template <class C> void displayPanel(C& c, const Skin& k, const Box& p) {
    if (!k.solid) {
        platePanel(c, k, p);
        return;
    }
    const bool bright = ((k.bg >> 8) & 255) > 128; // on a black plate the edge of the cut-out hardly catches the light
    const float edge = bright ? 1.5f : 1.0f;
    c.beginPath(); // where the plate is cut out for the window: dark above, light below
    c.roundedRect(p.x - edge, p.y - edge, p.w + 2 * edge, p.h + 2 * edge, 5 + edge);
    c.fillPaint(c.linearGradient(0, p.y - 2, 0, p.y + p.h + 2, rgb(0x000000, 0.55f), rgb(0xffffff, bright ? 0.3f : 0.11f)));
    c.fill();
    c.beginPath();
    c.roundedRect(p.x, p.y, p.w, p.h, 5);
    c.fillPaint(c.linearGradient(0, p.y, 0, p.y + p.h, rgb(k.well), rgb(k.panel)));
    c.fill();
    c.beginPath(); // the glass: a faint reflection over the upper part
    c.roundedRect(p.x + 1, p.y + 1, p.w - 2, p.h * 0.45f, 4);
    c.fillPaint(c.linearGradient(0, p.y, 0, p.y + p.h * 0.45f, rgb(0xffffff, 0.07f), rgb(0xffffff, 0.0f)));
    c.fill();
}

/**
 * A knob of radius `r` at `norm` 0..1, with its value shown as an arc around it that starts
 * at `fromNorm` (0, or the neutral point of a value that goes both ways). Flat: a disc with a
 * line from `mark` inside the arc, and the arc in `arcColour`. Solid: a knob of turned metal
 * with a sharp edge, bare or black, that stands on the plate, with a ring of light or a
 * printed scale around it; pass the look's `ring` there.
 */
template <class C> void knob(C& c, const Skin& k, float x, float y, float r, float norm, float fromNorm, uint32_t arcColour, float mark = 14) {
    const float pi = 3.14159265f, a0 = 0.75f * pi, a1 = 2.25f * pi;
    const float angle = a0 + (norm < 0 ? 0 : (norm > 1 ? 1 : norm)) * (a1 - a0);
    const float start = a0 + fromNorm * (a1 - a0);
    const float ring = k.solid ? 2.4f : 3.5f;

    if (k.scale) { // printed: a mark for every tenth, longer at the ends and in the middle
        c.beginPath();
        for (int i = 0; i <= 10; ++i) {
            const float a = a0 + i * (a1 - a0) / 10, to = r + (i % 5 == 0 ? 6.0f : 4.5f);
            c.moveTo(x + std::cos(a) * (r + 2.5f), y + std::sin(a) * (r + 2.5f));
            c.lineTo(x + std::cos(a) * to, y + std::sin(a) * to);
        }
        c.strokeColor(rgb(k.plateDim));
        c.strokeWidth(1);
        c.stroke();
    }
    c.beginPath();
    c.arc(x, y, r, a0, a1, C::CW);
    c.strokeColor(rgb(k.solid ? k.groove : k.track, k.scale ? 0.22f : (k.solid ? 0.5f : 1.0f)));
    c.strokeWidth(k.scale ? 1.2f : ring);
    c.lineCap(C::ROUND);
    c.stroke();
    if (std::fabs(angle - start) > 0.01f) {
        c.beginPath();
        c.arc(x, y, r, start < angle ? start : angle, start < angle ? angle : start, C::CW);
        c.strokeColor(rgb(arcColour));
        c.strokeWidth(k.solid ? ring - 0.8f : ring);
        c.stroke();
    }

    const float body = k.solid ? r - 4.5f : r - 6;
    if (!k.solid) {
        c.beginPath();
        c.circle(x, y, body);
        c.fillColor(rgb(k.knobLo));
        c.fill();
    } else {
        c.beginPath(); // its shadow on the plate, thrown down
        c.circle(x, y + 3, body + 5);
        c.fillPaint(c.radialGradient(x, y + 3, body - 3, body + 5, rgb(0x000000, 0.38f), rgb(0x000000, 0.0f)));
        c.fill();
        c.beginPath(); // the bevel around the top, lit from above
        c.circle(x, y, body);
        c.fillPaint(c.linearGradient(0, y - body, 0, y + body, rgb(k.knobHi), rgb(k.knobLo)));
        c.fill();
        c.strokeColor(rgb(k.knobEdge));
        c.strokeWidth(0.8f);
        c.stroke();
        const float top = body - 2;
        const bool black = ((k.knobHi >> 8) & 255) < 160; // black metal throws back less light and shows its dark fans more
        c.beginPath(); // the top, turned on a lathe: it throws the light back in two fans across the middle
        c.circle(x, y, top);
        c.fillPaint(c.linearGradient(0, y - top, 0, y + top, rgb(k.keyHi), rgb(k.keyLo)));
        c.fill();
        for (int fan = 0; fan < 4; ++fan) {
            const float mid = -2.2f + fan * (pi / 2); // light, dark, light, dark
            for (float half : {0.6f, 0.38f, 0.18f}) {
                c.beginPath();
                c.moveTo(x, y);
                for (int i = 0; i <= 6; ++i) {
                    const float a = mid - half + i * (2 * half / 6);
                    c.lineTo(x + std::cos(a) * top, y + std::sin(a) * top);
                }
                c.closePath();
                c.fillColor(fan % 2 == 0 ? rgb(0xffffff, black ? 0.055f : 0.3f) : rgb(0x000000, black ? 0.3f : 0.075f));
                c.fill();
            }
        }
        c.beginPath();
        c.circle(x, y, top);
        c.strokeColor(rgb(k.knobEdge, 0.45f));
        c.strokeWidth(0.7f);
        c.stroke();
    }
    c.beginPath();
    const float tip = k.solid ? body - 1.5f : r - 7;
    const float from = k.solid ? 2.5f : r - mark; // turned metal: a line from the middle to the edge
    c.moveTo(x + std::cos(angle) * from, y + std::sin(angle) * from);
    c.lineTo(x + std::cos(angle) * tip, y + std::sin(angle) * tip);
    c.strokeColor(rgb(k.pointer));
    c.strokeWidth(k.solid ? 1.5f : 2);
    c.lineCap(C::ROUND);
    c.stroke();
}

/** The same, for a value that grows from the lowest setting, in the look's own colour. */
template <class C> void knob(C& c, const Skin& k, float x, float y, float r, float norm) {
    knob(c, k, x, y, r, norm, 0.0f, k.ring);
}

/**
 * A row of `count` choices in the box `b`, of which `active` is on; label(i) gives the text
 * of choice i. Flat: a strip with the choice filled in with `accent`, the others in
 * `flatIdle`. Solid: keys, with a light of that colour in the one that is down.
 */
template <class C, class LabelFn> void keys(C& c, const Skin& k, const Box& b, int count, int active, LabelFn text, uint32_t accent, uint32_t flatIdle) {
    const float w = b.w / count, my = b.y + b.h * 0.5f;
    if (!k.solid) {
        c.beginPath();
        c.roundedRect(b.x, b.y, b.w, b.h, 4);
        c.fillColor(rgb(k.track));
        c.fill();
        for (int i = 0; i < count; ++i) {
            if (i == active) {
                c.beginPath();
                c.roundedRect(b.x + i * w + 2, b.y + 2, w - 4, b.h - 4, 3);
                c.fillColor(rgb(accent));
                c.fill();
            }
            label(c, text(i), b.x + (i + 0.5f) * w, my, 9, rgb(i == active ? k.bg : flatIdle), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
        }
        return;
    }
    c.beginPath(); // the slot the keys stand in
    c.roundedRect(b.x - 1, b.y - 1, b.w + 2, b.h + 2.5f, 4);
    c.fillColor(rgb(0x000000, 0.6f));
    c.fill();
    for (int i = 0; i < count; ++i) {
        const bool down = i == active;
        const float x = b.x + i * w + 1, kw = w - 2;
        c.beginPath();
        c.roundedRect(x, b.y + (down ? 1 : 0), kw, b.h - 1, 3);
        c.fillPaint(c.linearGradient(0, b.y, 0, b.y + b.h, rgb(down ? k.keyLo : k.keyHi), rgb(down ? k.keyHi : k.keyLo)));
        c.fill();
        if (!down) {
            c.beginPath(); // the light on its upper edge
            c.moveTo(x + 3, b.y + 1);
            c.lineTo(x + kw - 3, b.y + 1);
            c.strokeColor(rgb(0xffffff, 0.3f));
            c.strokeWidth(1);
            c.stroke();
        }
        const float lx = x + kw * 0.5f - 7, ly = b.y + (b.h < 24 ? 2.5f : 4.0f) + (down ? 1 : 0); // a low key has its light at the very top
        if (down) {
            c.beginPath(); // the glow of the light
            c.roundedRect(lx - 3, ly - 3, 20, 8.5f, 4);
            c.fillColor(rgb(accent, 0.22f));
            c.fill();
        }
        c.beginPath(); // the window of the light, and the light in it
        c.roundedRect(lx - 1, ly - 1, 16, 4.5f, 2);
        c.fillColor(rgb(0x000000, 0.55f));
        c.fill();
        if (down) {
            c.beginPath();
            c.roundedRect(lx, ly, 14, 2.5f, 1);
            c.fillColor(rgb(accent));
            c.fill();
        }
        label(c, text(i), x + kw * 0.5f, my + (b.h < 24 ? 3.5f : 3.0f) + (down ? 1 : 0), 9, rgb(k.keyText), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
    }
}

template <class C, class LabelFn> void keys(C& c, const Skin& k, const Box& b, int count, int active, LabelFn text) {
    keys(c, k, b, count, active, text, k.accent, k.text);
}

/** A key that does something when pressed, with `text` on it. */
template <class C> void button(C& c, const Skin& k, const Box& b, const char* text) {
    c.beginPath();
    c.roundedRect(b.x, b.y, b.w, b.h, 4);
    if (!k.solid) {
        c.fillColor(rgb(k.track));
        c.fill();
        label(c, text, b.x + b.w * 0.5f, b.y + b.h * 0.5f, 10, rgb(k.text), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
        return;
    }
    c.fillColor(rgb(0x000000, 0.6f)); // the slot it stands in
    c.fill();
    c.beginPath();
    c.roundedRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2.5f, 3);
    c.fillPaint(c.linearGradient(0, b.y, 0, b.y + b.h, rgb(k.keyHi), rgb(k.keyLo)));
    c.fill();
    c.beginPath();
    c.moveTo(b.x + 4, b.y + 2);
    c.lineTo(b.x + b.w - 4, b.y + 2);
    c.strokeColor(rgb(0xffffff, 0.3f));
    c.strokeWidth(1);
    c.stroke();
    label(c, text, b.x + b.w * 0.5f, b.y + b.h * 0.5f, 10, rgb(k.keyText), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
}

/**
 * An on/off switch, 28 x 14 around (x, y). Flat: a pill in `accent` when on. Solid: a
 * slide switch, with a light of that colour behind the slot when on.
 */
template <class C> void toggle(C& c, const Skin& k, float x, float y, bool on, uint32_t accent) {
    c.beginPath();
    c.roundedRect(x - 14, y - 7, 28, 14, 7);
    if (!k.solid) {
        c.fillColor(on ? rgb(accent) : rgb(k.track));
        c.fill();
        c.beginPath();
        c.circle(on ? x + 7 : x - 7, y, 5);
        c.fillColor(rgb(on ? k.bg : k.dim));
        c.fill();
        return;
    }
    c.fillColor(rgb(0x000000, 0.75f));
    c.fill();
    if (on) {
        c.beginPath();
        c.roundedRect(x - 11, y - 4, 12, 8, 4);
        c.fillColor(rgb(accent));
        c.fill();
    }
    const float hx = on ? x + 7 : x - 7;
    c.beginPath();
    c.circle(hx, y, 5.5f);
    c.fillPaint(c.linearGradient(0, y - 5.5f, 0, y + 5.5f, rgb(k.knobHi), rgb(k.knobLo)));
    c.fill();
    c.strokeColor(rgb(k.knobEdge));
    c.strokeWidth(0.8f);
    c.stroke();
}

/** Over a bar of a level display: solid looks cut it, along its length, into the segments of a display tube. */
template <class C> void segments(C& c, const Skin& k, float x, float y, float w, float h) {
    if (!k.solid) return;
    c.beginPath();
    if (h > w) {
        for (float gy = y + 4; gy < y + h - 1; gy += 5) {
            c.moveTo(x - 0.5f, gy + 0.5f);
            c.lineTo(x + w + 0.5f, gy + 0.5f);
        }
    }
    for (float gx = x + 4; h <= w && gx < x + w - 1; gx += 5) {
        c.moveTo(gx + 0.5f, y - 0.5f);
        c.lineTo(gx + 0.5f, y + h + 0.5f);
    }
    c.strokeColor(rgb(k.well));
    c.strokeWidth(1.4f);
    c.stroke();
}

/** Over a printed meter card (the classic meter design): solid looks light it from behind, brightest in the middle. */
template <class C> void lamp(C& c, const Skin& k, const Box& f) {
    if (!k.solid) return;
    c.beginPath();
    c.roundedRect(f.x, f.y, f.w, f.h, 5);
    c.fillPaint(c.radialGradient(f.x + f.w * 0.5f, f.y + f.h * 0.62f, f.h * 0.35f, f.w * 0.62f, rgb(0xfff2c8, 0.16f), rgb(0x5a2a00, 0.42f)));
    c.fill();
}

// ------------------------------------------------------- the size of a window

/** A window is kept between these sizes, as a percentage of the size it was designed at. */
constexpr int kSizeLeast = 75, kSizeMost = 300;
inline int cleanSize(int percent) { return percent < kSizeLeast ? kSizeLeast : (percent > kSizeMost ? kSizeMost : percent); }

/** The size the window of `plugin` (a short name, as "sampler") was left at; 100 when none was kept or it cannot be read. */
inline int storedSize(const char* plugin) {
    const std::string text = settings::get((std::string("size.") + plugin).c_str());
    if (text.empty() || text.size() > 3) return 100;
    int percent = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return 100;
        percent = percent * 10 + (c - '0');
    }
    return percent < kSizeLeast || percent > kSizeMost ? 100 : percent;
}
/** Keeps that size for the next time the window is opened. Every plugin has its own. */
inline bool storeSize(const char* plugin, int percent) { return settings::set((std::string("size.") + plugin).c_str(), std::to_string(cleanSize(percent))); }

// ------------------------------------------------------- the choice of look

/**
 * A cog button and, under it, the list of looks. Clicking a look, or an arrow at the top of
 * the list, changes the window at once and leaves the list open, so that the looks can be
 * tried one after the other; a click beside the list closes it. The choice goes to the
 * settings file, and poll() lets a window that is already open follow a choice made in
 * another one.
 */
class SkinMenu {
public:
    static constexpr float kWidth = 168, kRow = 22, kHead = 26;
    enum { kNotMine, kHandled, kChanged, kSized };
    static constexpr int kSizeCount = 5;
    static constexpr int kSizes[kSizeCount] = {75, 100, 125, 150, 200};

    SkinMenu() { load(); }

    int skin() const { return skin_; }
    bool isOpen() const { return open_; }
    void setButton(float cx, float cy) { bx_ = cx; by_ = cy; }
    Box buttonBox() const { return {bx_ - 12, by_ - 12, 24, 24}; }
    Box menuBox() const { return {bx_ + 12 - kWidth, by_ + 16, kWidth, kHead + kSkinCount * kRow + 6 + (sizes_ ? kHead + kRow + 4 : 0)}; }
    /**
     * A plugin whose window can be made larger and smaller offers the sizes under the looks: `percent` is
     * the size the window has now. Without this call the list holds the looks alone.
     */
    void offerSizes(int percent) { sizes_ = true; size_ = percent; }
    /** The size last chosen from the list, or told with offerSizes(): a percentage of the design size. */
    int size() const { return size_; }
    Box sizeBox(int i) const {
        const Box m = menuBox();
        const float w = (m.w - 16 - (kSizeCount - 1) * 3) / kSizeCount;
        return {m.x + 8 + i * (w + 3), m.y + kHead + kSkinCount * kRow + kHead, w, kRow - 4};
    }
    static const char* helpWithSizes() {
        return "Look and size: opens the list of looks for the plugin windows, and under them the sizes of this window. The look holds for every HNL plugin and every song; the size is kept for this plugin. "
               "The window can also be dragged by its edge where the host allows that. Neither changes anything about the sound.";
    }
    Box rowBox(int row) const {
        const Box m = menuBox();
        return {m.x, m.y + kHead + row * kRow, m.w, kRow};
    }
    Box prevBox() const { return {menuBox().x + kWidth - 46, menuBox().y + 7, 17, 14}; }
    Box nextBox() const { return {menuBox().x + kWidth - 25, menuBox().y + 7, 17, 14}; }
    static const char* help() {
        return "Look: opens the list of looks for the plugin windows. Classic is the look the plugins have always had. The choice holds for every HNL plugin and every song, and changes nothing about the sound.";
    }

    /** kChanged when another look was chosen, kHandled when the click was for the button or the list. */
    int mouseDown(float x, float y) {
        if (std::hypot(x - bx_, y - by_) <= 13) {
            open_ = !open_;
            hover_ = -1;
            return kHandled;
        }
        if (!open_) return kNotMine;
        if (!menuBox().contains(x, y)) {
            open_ = false;
            return kHandled;
        }
        for (int i = 0; sizes_ && i < kSizeCount; ++i)
            if (sizeBox(i).contains(x, y)) {
                if (kSizes[i] == size_) return kHandled;
                size_ = kSizes[i];
                return kSized;
            }
        int next = skin_;
        if (prevBox().contains(x, y)) next = (skin_ + kSkinCount - 1) % kSkinCount;
        else if (nextBox().contains(x, y)) next = (skin_ + 1) % kSkinCount;
        else if (rowAt(x, y) >= 0) next = rowAt(x, y);
        if (next == skin_) return kHandled;
        skin_ = next;
        settings::set("skin", skinName(skin_)); // when it cannot be written, the look still holds for this window
        return kChanged;
    }

    /** Tracks the row under the mouse while the list is open; true when it changed. */
    bool motion(float x, float y) {
        const int row = open_ ? rowAt(x, y) : -1;
        if (row == hover_) return false;
        hover_ = row;
        return true;
    }

    /** Call from the UI's idle time. Reads the settings again about once a second; true when the look changed. */
    bool poll() {
        if (++ticks_ < kPollEvery) return false;
        ticks_ = 0;
        const int before = skin_;
        load();
        return skin_ != before;
    }
    static constexpr int kPollEvery = 30;

    template <class C> void drawButton(C& c, const Theme& t) const {
        c.beginPath();
        c.circle(bx_, by_, 11);
        c.fillColor(rgb(open_ ? t.accent : t.track));
        c.fill();
        const DGL_NAMESPACE::Color ink = rgb(open_ ? t.bg : t.text);
        c.beginPath(); // a cog: a ring with eight teeth
        for (int i = 0; i < 8; ++i) {
            const float a = i * 0.785398163f;
            c.moveTo(bx_ + std::cos(a) * 4.6f, by_ + std::sin(a) * 4.6f);
            c.lineTo(bx_ + std::cos(a) * 6.6f, by_ + std::sin(a) * 6.6f);
        }
        c.strokeColor(ink);
        c.strokeWidth(2.3f);
        c.stroke();
        c.beginPath();
        c.circle(bx_, by_, 3.6f);
        c.strokeColor(ink);
        c.strokeWidth(2.2f);
        c.stroke();
    }

    /** Draw last, so the list lies over everything else. */
    template <class C> void drawMenu(C& c, const Theme& t) const {
        if (!open_) return;
        const Box m = menuBox();
        c.beginPath();
        c.roundedRect(m.x, m.y, m.w, m.h, 4);
        c.fillColor(rgb(t.popup));
        c.fill();
        c.strokeColor(rgb(t.popupEdge));
        c.strokeWidth(1);
        c.stroke();
        label(c, "LOOK", m.x + 12, m.y + 14, 9, rgb(t.dim), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
        const Box prev = prevBox(), next = nextBox();
        for (const Box* b : {&prev, &next}) {
            c.beginPath();
            c.roundedRect(b->x, b->y, b->w, b->h, 3);
            c.fillColor(rgb(t.track));
            c.fill();
            const float cx = b->x + b->w * 0.5f, cy = b->y + b->h * 0.5f, d = b == &prev ? -1.0f : 1.0f;
            triangle(c, cx + d * 3, cy, cx - d * 2.5f, cy - 3.5f, cx - d * 2.5f, cy + 3.5f, rgb(t.text));
        }
        for (int i = 0; i < kSkinCount; ++i) {
            const Box r = rowBox(i);
            if (i == hover_) {
                c.beginPath();
                c.roundedRect(r.x + 4, r.y, r.w - 8, kRow, 3);
                c.fillColor(rgb(t.track));
                c.fill();
            }
            const float my = r.y + kRow * 0.5f;
            if (i == skin_) {
                c.beginPath();
                c.circle(r.x + 15, my, 2.5f);
                c.fillColor(rgb(t.accent));
                c.fill();
            }
            label(c, skinLabel(i), r.x + 26, my, 11, rgb(i == skin_ ? t.accent : t.text), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
        }
        if (!sizes_) return;
        label(c, "SIZE", m.x + 12, sizeBox(0).y - 11, 9, rgb(t.dim), C::ALIGN_LEFT | C::ALIGN_MIDDLE);
        bool listed = false;
        for (int i = 0; i < kSizeCount; ++i) {
            const Box b = sizeBox(i);
            const bool on = kSizes[i] == size_;
            listed = listed || on;
            c.beginPath();
            c.roundedRect(b.x, b.y, b.w, b.h, 3);
            c.fillColor(rgb(on ? t.accent : t.track));
            c.fill();
            label(c, std::to_string(kSizes[i]).c_str(), b.x + b.w * 0.5f, b.y + b.h * 0.5f, 10, rgb(on ? t.bg : t.text), C::ALIGN_CENTER | C::ALIGN_MIDDLE);
        }
        if (!listed) label(c, (std::to_string(size_) + " %").c_str(), m.x + m.w - 12, sizeBox(0).y - 11, 9, rgb(t.accent), C::ALIGN_RIGHT | C::ALIGN_MIDDLE); // dragged to a size between
    }

private:
    /** An unknown name (a look of a later version, or a damaged file) and a missing file give Classic. */
    void load() {
        const std::string name = settings::get("skin");
        skin_ = kSkinClassic;
        for (int i = 0; i < kSkinCount; ++i)
            if (name == skinName(i)) skin_ = i;
    }
    int rowAt(float x, float y) const {
        for (int i = 0; i < kSkinCount; ++i)
            if (rowBox(i).contains(x, y)) return i;
        return -1;
    }

    float bx_ = 0, by_ = 0;
    int skin_ = kSkinClassic, hover_ = -1, ticks_ = 0, size_ = 100;
    bool open_ = false, sizes_ = false;
};

} // namespace ui
} // namespace hnl
