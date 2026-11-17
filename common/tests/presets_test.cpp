// Tests of common/UserPresets.hpp (presets saved as files) and of the preset bar in
// common/ui/Widgets.hpp (the list, typing a name, deleting). No plugin and no window needed.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace DGL {
struct Color {
    float r, g, b, a;
    Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
};
}
#define DGL_NAMESPACE DGL

#include "../UserPresets.hpp"
#include "../ui/Widgets.hpp"

namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// A plugin of three parameters, the last of which stays out of presets.
enum { kGain, kMode, kTrim, kCount };
static const hnl::PresetParam kInfo[kCount] = {{"gain", -12.f, 12.f, 0.f}, {"mode", 0.f, 3.f, 1.f}, {"trim", -6.f, 6.f, 0.f}};
static hnl::PresetParam info(uint32_t id) { return kInfo[id]; }
static bool inPreset(uint32_t id) { return id != kTrim; }
static const hnl::PresetValue kLoudValues[] = {{kGain, 6.f}};
static const hnl::Preset kFactory[] = {{"Default", nullptr, 0}, HNL_PRESET("Loud", kLoudValues)};

static hnl::PresetList makeList() { return hnl::PresetList("Test Plugin", kFactory, 2, kCount, info, inPreset); }

static void write(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}
static std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void testNames() {
    using hnl::presetfile::cleanName;
    check(cleanName("  My: pre/set?  ") == "My preset", "name: characters a file name cannot hold are dropped, ends trimmed");
    check(cleanName("") == "" && cleanName(" .. ") == "" && cleanName("\\/:*") == "", "name: nothing left gives an empty name");
    check(cleanName("..\\..\\evil") == "evil", "name: cannot climb out of the folder");
    check(cleanName("CON") == "CON_" && cleanName("nul.x") == "nul.x_" && cleanName("Console") == "Console", "name: Windows device names are avoided");
    check(cleanName(std::string(60, 'a')).size() == 20, "name: cut to 20 characters");
    const std::string accents = cleanName("Caf\xC3\xA9 \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9");
    check(hnl::presetfile::length(accents) == 20 && hnl::presetfile::validUtf8(accents), "name: cut between characters, not inside one");
    check(cleanName("bad\xC3") == "" && cleanName("\xFF\xFE") == "", "name: broken UTF-8 is refused");
    check(cleanName("line\nbreak\ttab") == "linebreaktab", "name: control characters are dropped");
}

static void testNumbers() {
    using hnl::presetfile::parseNumber;
    float v = 99.f;
    check(parseNumber(" -24.5 ", v) && v == -24.5f, "number: plain");
    check(parseNumber("0,25", v) && v == 0.25f, "number: a comma counts as a point");
    check(parseNumber("1.5e2", v) && v == 150.f && parseNumber("2E-1", v) && std::fabs(v - 0.2f) < 1e-7f, "number: exponent");
    v = 7.f;
    check(!parseNumber("", v) && !parseNumber("abc", v) && !parseNumber("1.2.3", v) && !parseNumber("nan", v) && !parseNumber("inf", v) &&
          !parseNumber("1e", v) && !parseNumber("-", v) && !parseNumber("1e999", v) && v == 7.f, "number: what is not a number is refused, the value untouched");
    bool same = true;
    for (float f : {0.f, -0.1f, 123.456f, 1e-7f, -3.4e20f, 100.f, 0.333333343f}) {
        float back = 0.f;
        same = same && parseNumber(hnl::presetfile::formatNumber(f), back) && back == f;
    }
    check(same, "number: written and read back, bit for bit");
}

static void testList(const fs::path& dir) {
    hnl::PresetList list = makeList();
    check(list.count() == 2 && list.canSave() && !list.isUser(1) && list.value(1, kGain) == 6.f && list.value(1, kMode) == 1.f, "list: factory presets only at first");
    check(list.suggestName(0) == "My preset", "list: first suggestion");

    float values[kCount] = {-3.5f, 2.f, 4.f};
    const int a = list.save("Warm", values);
    check(a == 2 && list.count() == 3 && list.isUser(a) && std::string(list.name(a)) == "Warm", "save: the preset joins the list after the factory ones");
    check(fs::exists(dir / "Warm.hnlpreset") && !fs::exists(dir / "Warm.tmp"), "save: one file, no leftovers");
    check(list.value(a, kGain) == -3.5f && list.value(a, kMode) == 2.f, "save: values come back");
    check(list.value(a, kTrim) == 0.f && slurp(dir / "Warm.hnlpreset").find("trim") == std::string::npos, "save: a parameter outside presets is not stored");

    hnl::PresetList second = makeList(); // as another plugin window, or the next song
    check(second.count() == 3 && second.find("Warm") == 2 && second.value(2, kGain) == -3.5f, "another list finds the preset on disk");

    values[kGain] = 9.f;
    check(list.save("warm", values) == 2 && list.count() == 3 && list.value(2, kGain) == 9.f, "save: the same name in other capitals replaces, no twin");
    check(std::string(list.name(2)) == "warm" && !fs::exists(dir / "Warm.tmp"), "save: the new spelling is kept");

    const int mine = list.save("Loud", values);
    check(mine >= 2 && std::string(list.name(mine)) == "Loud (mine)" && list.value(1, kGain) == 6.f, "save: a factory name is never taken over");
    check(list.save("   ", values) == -1 && list.save("???", values) == -1 && list.count() == 4, "save: an empty name is refused");
    check(list.suggestName(mine) == "Loud (mine)" && list.suggestName(0) == "My preset", "suggestion: the user's own preset keeps its name");
    list.save("My preset", values);
    check(list.suggestName(0) == "My preset 2", "suggestion: a free name");

    float wild[kCount] = {1e9f, -5.f, 0.f};
    const int w = list.save("Wild", wild);
    check(list.value(w, kGain) == 12.f && list.value(w, kMode) == 0.f, "save: values are kept within their range");

    // Files that did not come from this code.
    write(dir / "Comma.hnlpreset", "HNL preset\r\nplugin: x\r\ngain = 1,5\r\nmode=3\r\nunknown = 4\r\nrubbish\r\n");
    write(dir / "Far out.hnlpreset", "HNL preset\ngain = 500\nmode = nan\n");
    write(dir / "Not ours.hnlpreset", "something else\ngain = 3\n");
    write(dir / "Empty.hnlpreset", "");
    write(dir / "Huge.hnlpreset", "HNL preset\n" + std::string(100000, 'x'));
    write(dir / "notes.txt", "HNL preset\ngain = 3\n");
    write(dir / "Default.hnlpreset", "HNL preset\ngain = 3\n");
    fs::create_directory(dir / "Folder.hnlpreset");
    list.rescan();
    const int comma = list.find("Comma"), far = list.find("Far out");
    check(comma >= 0 && list.value(comma, kGain) == 1.5f && list.value(comma, kMode) == 3.f, "file by hand: commas, CRLF and unknown lines are coped with");
    check(far >= 0 && list.value(far, kGain) == 12.f && list.value(far, kMode) == 1.f, "file by hand: out of range is clamped, not a number gives the default");
    check(list.find("Not ours") < 0 && list.find("Empty") < 0 && list.find("Huge") < 0 && list.find("notes") < 0 && list.find("Folder") < 0,
          "other files in the folder are left out");
    check(list.find("Default") == 0 && list.value(0, kGain) == 0.f, "a file with a factory name does not shadow the factory preset");

    bool sorted = true;
    for (int i = list.factoryCount() + 1; i < list.count(); ++i) {
        std::string x = list.name(i - 1), y = list.name(i);
        for (char& c : x) c = hnl::presetfile::lower(c);
        for (char& c : y) c = hnl::presetfile::lower(c);
        sorted = sorted && x <= y;
    }
    check(sorted, "saved presets are listed by name");

    const int before = list.count();
    check(!list.remove(0) && !list.remove(99) && !list.remove(-1) && list.count() == before, "delete: a factory preset cannot be deleted");
    check(list.remove(list.find("warm")) && list.find("warm") < 0 && list.count() == before - 1 && !fs::exists(dir / "warm.hnlpreset"), "delete: the file is gone");

    for (int i = 0; i < 60; ++i) list.save("Fill " + std::to_string(i), values);
    check(list.count() == hnl::PresetList::kMaxShown && list.full(), "the list stops at what the UI has room for");
    check(list.save("One more", values) == -1 && !fs::exists(dir / "One more.hnlpreset"), "save: refused when the list is full");
    check(list.save("Fill 3", wild) >= 0, "save: replacing still works when the list is full");
    check(list.value(-1, kGain) == 0.f && list.value(999, kMode) == 1.f && std::string(list.name(999)).empty(), "an index outside the list is harmless");
}

static void testNoFolder() {
    setenv("HNL_PRESET_DIR", "/proc/hnl-no-such-place/presets", 1);
    hnl::PresetList list = makeList();
    float values[kCount] = {1.f, 1.f, 1.f};
    check(list.count() == 2 && list.save("Nowhere", values) == -1 && list.count() == 2, "a folder that cannot be made: saving fails, nothing else does");
}

static void testBar() {
    typedef hnl::ui::PresetBar Bar;
    Bar bar(100, 10);
    const hnl::ui::Box name = bar.nameBox();
    const float nx = name.x + 20, ny = name.y + 10;
    const int count = 5, firstUser = 3; // rows 0..4 presets, row 5 "Save as..."
    auto centre = [](const hnl::ui::Box& b, float& x, float& y) { x = b.x + b.w * 0.4f; y = b.y + b.h * 0.5f; };
    float x, y;

    check(bar.mouseDown(5, 5, 0, count, firstUser, true) == Bar::kNotMine, "bar: a click elsewhere is not its business");
    check(bar.mouseDown(nx, ny, 0, count, firstUser, true) == Bar::kHandled && bar.isOpen(), "bar: the name opens the list");
    centre(bar.rowBox(4), x, y);
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == 4 && !bar.isOpen(), "bar: a row picks its preset");

    bar.mouseDown(nx, ny, 0, count, firstUser, true);
    centre(bar.deleteBox(4), x, y);
    x = bar.deleteBox(4).x + 11;
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == Bar::kHandled && bar.isOpen(), "bar: the cross asks first");
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == Bar::kDelete && bar.target() == 4 && !bar.isOpen(), "bar: the cross again deletes");
    bar.mouseDown(nx, ny, 0, count, firstUser, true);
    bar.mouseDown(x, y, 0, count, firstUser, true);
    centre(bar.rowBox(3), x, y);
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == 3, "bar: after the question a click on another row just picks it");
    bar.mouseDown(nx, ny, 0, count, firstUser, true);
    x = bar.deleteBox(1).x + 11;
    y = bar.deleteBox(1).y + 11;
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == 1, "bar: a factory preset has no cross");

    bar.mouseDown(nx, ny, 0, count, firstUser, false);
    centre(bar.rowBox(count), x, y);
    check(bar.mouseDown(x, y, 0, count, firstUser, false) == Bar::kHandled && !bar.isEditing(), "bar: Save as does nothing when the list is full");
    bar.mouseDown(nx, ny, 0, count, firstUser, true);
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == Bar::kEdit, "bar: Save as asks for a name");
    bar.startEdit("My preset");
    check(bar.isEditing() && bar.isBusy() && bar.text() == "My preset", "bar: the name box holds the suggestion");

    check(bar.character("!") && bar.text() == "My preset!", "typing: a character is added");
    check(bar.character("/") && bar.character("\r") && bar.character("\x08") && bar.text() == "My preset!", "typing: what a name cannot hold is swallowed");
    check(bar.key(0x08) == Bar::kHandled && bar.text() == "My preset", "typing: backspace");
    bar.character("\xC3\xA9");
    check(bar.text() == "My preset\xC3\xA9" && bar.key(0x08) == Bar::kHandled && bar.text() == "My preset", "typing: backspace takes a whole accented character");
    for (int i = 0; i < 40; ++i) bar.character("x");
    check(bar.text().size() == 20, "typing: stops at 20 characters");
    check(bar.mouseDown(nx, ny, 0, count, firstUser, true) == Bar::kHandled && bar.isEditing(), "typing: a click in the box keeps it open");
    check(bar.key(0x0D) == Bar::kSave && !bar.isEditing(), "typing: Enter saves");

    bar.startEdit("A");
    check(bar.key(0x1B) == Bar::kHandled && !bar.isEditing(), "typing: Escape leaves without saving");
    bar.startEdit("A");
    bar.key(0x08);
    check(bar.key(0x0D) == Bar::kHandled && bar.isEditing(), "typing: Enter with no name does nothing");
    check(bar.character(" ") && bar.text().empty(), "typing: a name does not start with a space");
    centre(bar.saveBox(), x, y);
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == Bar::kHandled && !bar.isEditing(), "the tick with no name leaves without saving");
    bar.startEdit("B");
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == Bar::kSave && bar.text() == "B", "the tick saves, without any keyboard");
    bar.startEdit("B");
    centre(bar.cancelBox(), x, y);
    check(bar.mouseDown(x, y, 0, count, firstUser, true) == Bar::kHandled && !bar.isEditing(), "the cross leaves without saving");
    bar.startEdit("B");
    check(bar.mouseDown(5, 5, 0, count, firstUser, true) == Bar::kHandled && !bar.isEditing(), "a click elsewhere leaves without saving, and goes no further");

    check(bar.key(0x41) == Bar::kNotMine && !bar.character("a"), "keys are left alone when no name is being typed");
    bar.mouseDown(nx, ny, 0, count, firstUser, true);
    check(bar.key(0x1B) == Bar::kHandled && !bar.isOpen(), "Escape closes the list");
    check(bar.mouseDown(bar.cancelBox().x + 5, ny, 0, 0, 0, true) == Bar::kNotMine, "bar: no presets at all does not divide by zero");
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("hnl-presets-test-" + std::to_string(std::rand()) + "-" + std::to_string(static_cast<long>(::getpid())));
    fs::remove_all(dir);
    setenv("HNL_PRESET_DIR", dir.c_str(), 1);
    testNames();
    testNumbers();
    testList(dir);
    testNoFolder();
    testBar();
    fs::remove_all(dir);
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
