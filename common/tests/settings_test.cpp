// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The settings file shared by all plugins (common/UserSettings.hpp): values come back, other
// keys are left alone, and a missing, damaged or unwritable file never does worse than "no value".
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "../UserSettings.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "hnl-settings-test";
    fs::remove_all(dir);
    setenv("HNL_SETTINGS_DIR", dir.c_str(), 1);

    check(hnl::settings::get("skin").empty(), "no folder yet: no value");
    check(hnl::settings::set("skin", "hifi-black") && hnl::settings::get("skin") == "hifi-black", "a value comes back, and the folder was made");
    check(hnl::settings::set("other", "1") && hnl::settings::get("skin") == "hifi-black" && hnl::settings::get("other") == "1", "a second key leaves the first alone");
    check(hnl::settings::set("skin", "classic") && hnl::settings::get("skin") == "classic" && hnl::settings::get("other") == "1", "a key is replaced, not added again");
    check(!fs::exists(dir / "settings.txt.new"), "nothing is left beside the file");

    { std::ofstream(dir / "settings.txt") << "# by hand\r\n  skin=hifi-silver  \r\nnonsense\n=x\nlater = thing\n"; }
    check(hnl::settings::get("skin") == "hifi-silver" && hnl::settings::get("later") == "thing", "a file edited by hand, with Windows line ends, is read");
    check(hnl::settings::get("nonsense").empty() && hnl::settings::get("").empty(), "a line without a key or a value is passed over");
    check(hnl::settings::set("skin", "classic") && hnl::settings::get("later") == "thing", "and writing keeps its other keys");

    { std::ofstream out(dir / "settings.txt", std::ios::binary); for (int i = 0; i < 300000; ++i) out.put(static_cast<char>(i * 7)); }
    check(hnl::settings::get("skin").empty(), "a large file of something else gives no value");

    fs::remove_all(dir);
    { std::ofstream(dir.string()) << "a file where the folder should be"; }
    check(!hnl::settings::set("skin", "hifi-black") && hnl::settings::get("skin").empty(), "a folder that cannot be made: not stored, no value, no crash");
    fs::remove(dir);

    unsetenv("HNL_SETTINGS_DIR");
    setenv("XDG_CONFIG_HOME", "/somewhere", 1);
    check(hnl::settings::folder() == fs::path("/somewhere/HNL"), "without the override the file is in the HNL folder, beside the plugins' presets");

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
