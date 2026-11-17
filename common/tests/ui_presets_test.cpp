// Saving, loading and deleting a preset through a plugin's real UI class, driven by clicks and
// keys as a host window would send them. Built once per plugin by its tests/run.sh against the
// drawing stand-in in tools/ui-preview, with -DPRESET_X=<the plugin's kPresetX>.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "DistrhoUI.hpp"
#include "Params.hpp"
#include "Presets.hpp"
#include "../../common/ui/Widgets.hpp"

namespace fs = std::filesystem;
typedef hnl::ui::PresetBar Bar;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static const Bar kBar(PRESET_X, 14); // the same place as the UI's own, for where to click

static void click(DISTRHO::UI* ui, const hnl::ui::Box& box, float along = 0.4f) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = box.x + box.w * along;
    ev.pos.y = box.y + box.h * 0.5f;
    ui->onMouse(ev);
    ev.press = false;
    ui->onMouse(ev);
}
static void type(DISTRHO::UI* ui, const char* text) {
    for (; *text; ++text) {
        DISTRHO::KeyboardEvent key;
        key.key = static_cast<unsigned char>(*text);
        ui->onKeyboard(key);
        DISTRHO::CharacterInputEvent ch;
        ch.string[0] = *text;
        ui->onCharacterInput(ch);
    }
}
static bool key(DISTRHO::UI* ui, unsigned code) {
    DISTRHO::KeyboardEvent ev;
    ev.key = code;
    const bool taken = ui->onKeyboard(ev);
    DISTRHO::CharacterInputEvent ch; // Windows sends these keys as text too
    ch.string[0] = static_cast<char>(code);
    ui->onCharacterInput(ch);
    return taken;
}
static std::string drawn(DISTRHO::UI* ui) {
    ui->svg.clear();
    ui->onNanoDisplay();
    return ui->svg;
}
static bool shows(DISTRHO::UI* ui, const std::string& text) { return drawn(ui).find(">" + text + "<") != std::string::npos; }

/** The value the UI last sent to the host for a parameter, or `none`. */
static float sent(DISTRHO::UI* ui, uint32_t id, float none) {
    float v = none;
    for (const auto& p : ui->sentParameters)
        if (p.first == id) v = p.second;
    return v;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("hnl-ui-presets-" + std::to_string(static_cast<long>(::getpid())));
    fs::remove_all(dir);
    setenv("HNL_PRESET_DIR", dir.c_str(), 1);

    const hnl::PresetList reference = presets::list();
    const int factory = presets::kCount;
    // Two controls that presets hold, set away from every factory value.
    uint32_t a = kInputParamCount, b = kInputParamCount, outside = kInputParamCount;
    for (uint32_t id = 0; id < kInputParamCount; ++id) {
        if (!presets::inPreset(id)) outside = id;
        else if (a == kInputParamCount) a = id;
        else if (b == kInputParamCount) b = id;
    }
    auto info = [](uint32_t id) {
#ifdef HNL_PARAM_INFO_FN
        const ParamInfo& p = paramInfo(id);
#else
        const ParamInfo& p = kParams[id];
#endif
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    };
    const float aSaved = info(a).min + 0.3125f * (info(a).max - info(a).min), bSaved = info(b).min + 0.6875f * (info(b).max - info(b).min);

    DISTRHO::UI* ui = DISTRHO::createUI();
    ui->parameterChanged(a, aSaved);
    ui->parameterChanged(b, bSaved);
    if (outside < kInputParamCount) ui->parameterChanged(outside, info(outside).max);

    // Save as..., type a name, Enter.
    click(ui, kBar.nameBox());
    check(shows(ui, "Save as..."), "the list ends with Save as...");
    click(ui, kBar.rowBox(factory));
    check(shows(ui, "My preset|") && ui->window.focused, "Save as... opens a name box with a suggestion and takes the keyboard");
    for (int i = 0; i < 9; ++i) key(ui, 0x08);
    type(ui, "Mix/ A");
    check(shows(ui, "Mix A|"), "typing replaces the suggestion; a slash is not accepted");
    check(key(ui, 0x0D), "Enter is taken");
    check(fs::exists(dir / "Mix A.hnlpreset"), "the preset is a file in the preset folder");
    check(ui->sentState == "preset=Mix A" && shows(ui, "Mix A"), "the new preset is the current one, by name, and the host is told");

    // Moving a control marks it; choosing the preset again puts the control back.
    ui->parameterChanged(a, info(a).def);
    check(shows(ui, "Mix A *"), "a moved control puts a * after the user's preset too");
    ui->sentParameters.clear();
    click(ui, kBar.nameBox());
    click(ui, kBar.rowBox(factory));
    check(sent(ui, a, -1e9f) == aSaved && shows(ui, "Mix A"), "choosing the saved preset sends the saved value");

    // A factory preset, then the user's one through the arrows.
    click(ui, kBar.nameBox());
    click(ui, kBar.rowBox(0));
    check(ui->sentState == std::string("preset=") + presets::kAll[0].name, "factory presets still load");
    ui->sentParameters.clear();
    click(ui, kBar.cancelBox()); // the previous arrow: from the first to the last, the saved one
    check(ui->sentState == "preset=Mix A" && sent(ui, a, -1e9f) == aSaved && sent(ui, b, -1e9f) == bSaved, "the arrows step into the saved presets");
    if (outside < kInputParamCount)
        check(sent(ui, outside, -1e9f) == -1e9f, "a control that presets leave alone is left alone");

    // A second window (or the song opened again) knows the preset by its name.
    DISTRHO::UI* other = DISTRHO::createUI();
    other->stateChanged("preset", "Mix A");
    other->parameterChanged(a, aSaved);
    other->parameterChanged(b, bSaved);
    check(shows(other, "Mix A"), "a second window shows the saved preset without a *");
    other->stateChanged("preset", "Gone Missing");
    check(shows(other, "Gone Missing"), "a preset whose file is gone: the name stays, nothing breaks");

    // Without a keyboard: the tick saves under the suggested name.
    click(other, kBar.nameBox());
    click(other, kBar.rowBox(factory + 1));
    click(other, kBar.saveBox());
    check(fs::exists(dir / "My preset.hnlpreset") && other->sentState == "preset=My preset", "the tick saves without a single key");

    // The first window sees it when its list is opened, and can delete it.
    click(ui, kBar.nameBox());
    check(shows(ui, "My preset"), "the first window lists what the second saved");
    click(ui, kBar.deleteBox(factory + 1), 0.5f);
    check(fs::exists(dir / "My preset.hnlpreset") && shows(ui, "Delete? Click x again"), "the cross asks before it deletes");
    click(ui, kBar.deleteBox(factory + 1), 0.5f);
    check(!fs::exists(dir / "My preset.hnlpreset") && fs::exists(dir / "Mix A.hnlpreset"), "the cross again deletes that preset and no other");

    // Escape and a click elsewhere leave the name box without saving.
    click(ui, kBar.nameBox());
    click(ui, kBar.rowBox(factory + 1));
    type(ui, "zzz");
    check(key(ui, 0x1B) && !fs::exists(dir / "Mix Azzz.hnlpreset") && shows(ui, "Mix A"), "Escape saves nothing");
    check(!key(ui, 0x1B) && !key(ui, 'a'), "keys go back to the host when no name is being typed");

    // Saving over the user's own preset keeps one file.
    ui->parameterChanged(a, info(a).max);
    click(ui, kBar.nameBox());
    click(ui, kBar.rowBox(factory + 1));
    check(shows(ui, "Mix A|"), "Save as... on the user's preset suggests its own name");
    key(ui, 0x0D);
    int files = 0;
    for (const auto& entry : fs::directory_iterator(dir)) files += entry.path().extension() == ".hnlpreset" ? 1 : 0;
    check(files == 1 && shows(ui, "Mix A") && presets::list().value(factory, a) == info(a).max, "saving under the same name replaces the preset");

    delete ui;
    delete other;
    fs::remove_all(dir);
    (void)reference;
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
