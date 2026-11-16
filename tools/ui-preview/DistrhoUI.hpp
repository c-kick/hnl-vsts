// Stand-in for DPF's DistrhoUI.hpp that records the UI's NanoVG drawing as
// SVG, so the layout can be checked on a machine without OpenGL or Windows.
// Only the calls the plugins use are implemented; add more as needed.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "DistrhoPluginInfo.h"

#define START_NAMESPACE_DISTRHO namespace DISTRHO {
#define END_NAMESPACE_DISTRHO }
#define DGL_NAMESPACE DGL
#define NANOVG_DEJAVU_SANS_TTF "sans"
#define DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(C) C(const C&) = delete;

typedef unsigned int uint;

namespace DGL {
struct Color {
    float r, g, b, a;
    Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    std::string css() const {
        char s[64];
        std::snprintf(s, sizeof(s), "rgba(%d,%d,%d,%.3f)", int(r * 255 + .5f), int(g * 255 + .5f), int(b * 255 + .5f), a);
        return s;
    }
};
}

namespace DISTRHO {

struct Pos { double x = 0, y = 0; double getX() const { return x; } double getY() const { return y; } };
struct MouseEvent { uint mod = 0, time = 0, button = 1; bool press = true; Pos pos; };
struct MotionEvent { uint mod = 0, time = 0; Pos pos; };
struct ScrollEvent { uint mod = 0, time = 0; Pos pos, delta; };
enum { kModifierShift = 1, kModifierControl = 2 };

class UI {
public:
    enum { ALIGN_LEFT = 1, ALIGN_CENTER = 2, ALIGN_RIGHT = 4, ALIGN_MIDDLE = 16 };
    enum Winding { CCW = 1, CW = 2 };
    enum LineCap { BUTT, ROUND };

    UI(uint w, uint h, bool) : w_(w), h_(h) {}
    virtual ~UI() {}

    virtual void parameterChanged(uint32_t, float) {}
    virtual void stateChanged(const char*, const char*) {}
    virtual void onNanoDisplay() {}
    virtual bool onMouse(const MouseEvent&) { return false; }
    virtual bool onMotion(const MotionEvent&) { return false; }
    virtual bool onScroll(const ScrollEvent&) { return false; }

    std::string svg;

    bool loadSharedResources() { return true; }
    double getScaleFactor() const { return 1.0; }
    void repaint() {}
    uint getWidth() const { return w_; }
    uint getHeight() const { return h_; }
    void setSize(uint w, uint h) { w_ = w; h_ = h; }
    void editParameter(uint32_t, bool) {}
    void setParameterValue(uint32_t, float) {}
    void setState(const char*, const char*) {}

    void scale(float, float) {}
    void fontFace(const char*) {}
    void beginPath() { d_.clear(); }
    void moveTo(float x, float y) { add("M%.2f %.2f ", x, y); }
    void lineTo(float x, float y) { add("L%.2f %.2f ", x, y); }
    void closePath() { d_ += "Z "; }
    void rect(float x, float y, float w, float h) { add("M%.2f %.2f h%.2f v%.2f h%.2f Z ", x, y, w, h, -w); }
    void roundedRect(float x, float y, float w, float h, float r) {
        add("M%.2f %.2f h%.2f a%.2f %.2f 0 0 1 %.2f %.2f ", x + r, y, w - 2 * r, r, r, r, r);
        add("v%.2f a%.2f %.2f 0 0 1 %.2f %.2f ", h - 2 * r, r, r, -r, r);
        add("h%.2f a%.2f %.2f 0 0 1 %.2f %.2f ", -(w - 2 * r), r, r, -r, -r);
        add("v%.2f a%.2f %.2f 0 0 1 %.2f %.2f Z ", -(h - 2 * r), r, r, r, -r);
    }
    void circle(float cx, float cy, float r) {
        add("M%.2f %.2f a%.2f %.2f 0 1 0 %.2f 0 ", cx - r, cy, r, r, 2 * r);
        add("a%.2f %.2f 0 1 0 %.2f 0 Z ", r, r, -2 * r);
    }
    void arc(float cx, float cy, float r, float a0, float a1, Winding) {
        add("M%.2f %.2f ", cx + std::cos(a0) * r, cy + std::sin(a0) * r);
        add("A%.2f %.2f 0 %d 1 ", r, r, std::fabs(a1 - a0) > 3.14159265f ? 1 : 0);
        add("%.2f %.2f ", cx + std::cos(a1) * r, cy + std::sin(a1) * r);
    }
    void fillColor(const DGL::Color& c) { fill_ = c.css(); }
    void strokeColor(const DGL::Color& c) { stroke_ = c.css(); }
    void strokeWidth(float w) { width_ = w; }
    void lineCap(LineCap c) { round_ = c == ROUND; }
    void fill() { svg += "<path d=\"" + d_ + "\" fill=\"" + fill_ + "\"/>\n"; }
    void stroke() {
        char w[32];
        std::snprintf(w, sizeof(w), "%.2f", width_);
        svg += "<path d=\"" + d_ + "\" fill=\"none\" stroke=\"" + stroke_ + "\" stroke-width=\"" + w +
               "\" stroke-linecap=\"" + (round_ ? "round" : "butt") + "\"/>\n";
    }
    void fontSize(float s) { size_ = s; }
    void textAlign(int a) { align_ = a; }
    float text(float x, float y, const char* s, const char*) {
        char head[160];
        std::snprintf(head, sizeof(head), "<text x=\"%.2f\" y=\"%.2f\" font-size=\"%.2f\" text-anchor=\"%s\" ", x, y, size_,
                      (align_ & ALIGN_CENTER) ? "middle" : (align_ & ALIGN_RIGHT) ? "end" : "start");
        svg += std::string(head) + "dominant-baseline=\"central\" fill=\"" + fill_ + "\">" + escape(s) + "</text>\n";
        return x;
    }

private:
    static std::string escape(const char* s) {
        std::string out;
        for (; *s; ++s) out += *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : std::string(1, *s);
        return out;
    }
    uint w_, h_;
    template <typename... A> void add(const char* fmt, A... a) {
        char s[200];
        std::snprintf(s, sizeof(s), fmt, a...);
        d_ += s;
    }
    std::string d_, fill_ = "black", stroke_ = "black";
    float width_ = 1, size_ = 12;
    int align_ = ALIGN_LEFT;
    bool round_ = false;
};

UI* createUI();

} // namespace DISTRHO
