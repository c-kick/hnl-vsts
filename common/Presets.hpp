// Factory presets, shared by all plugins. A preset is the default value of
// every parameter plus a list of overrides, so adding a parameter never
// needs a change to existing presets.
#pragma once

#include <cstdint>

namespace hnl {

struct PresetValue {
    uint32_t id;
    float value;
};

struct Preset {
    const char* name;
    const PresetValue* values;
    uint32_t count;
};

/** The value a preset gives to a parameter whose default is `def`. */
inline float presetValue(const Preset& p, uint32_t id, float def) {
    for (uint32_t i = 0; i < p.count; ++i)
        if (p.values[i].id == id) return p.values[i].value;
    return def;
}

} // namespace hnl

#define HNL_PRESET(name, values) {name, values, static_cast<uint32_t>(sizeof(values) / sizeof(values[0]))}
