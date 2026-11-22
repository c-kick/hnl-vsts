// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
//
// Five controls for the whole plugin, then two scenes of 31 parameters each: A and B hold the same
// controls, and Morph goes from one to the other. The order is permanent: hosts store automation by
// index. Types are stored as numbers, so a module's types are only ever added at the end of its list.
#pragma once

#include <cmath>
#include <cstdint>

/** The six modules, in the order the sound passes them. */
enum Module { kDrive, kMotion, kVinyl, kNoise, kFilter, kSpace, kModuleCount };

/** A scene's parameters, counted from the scene's first one. */
enum SceneParam : uint32_t {
    sDriveOn, sDriveType, sDriveAmount, sDriveReact, sDriveTone,
    sMotionOn, sMotionType, sMotionAmount, sMotionReact, sMotionSpeed,
    sVinylOn, sVinylType, sVinylAmount, sVinylReact, sVinylWear,
    sNoiseOn, sNoiseType, sNoiseAmount, sNoiseReact, sNoiseColour, sNoiseMode,
    sFilterOn, sFilterType, sFilterAmount, sFilterReact, sFilterRes,
    sSpaceOn, sSpaceType, sSpaceAmount, sSpaceReact, sSpaceDecay,
    kSceneSize
};

/** Where a module's five controls start within a scene: on, type, amount, react, third knob. */
static const uint32_t kModuleBase[kModuleCount] = {sDriveOn, sMotionOn, sVinylOn, sNoiseOn, sFilterOn, sSpaceOn};
enum ModuleParam : uint32_t { mOn, mType, mAmount, mReact, mThird };

enum ParamId : uint32_t {
    kMorph, kMix, kOutput, kMains, kIdle,
    kSceneA,
    kSceneB = kSceneA + kSceneSize,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount = kSceneB + kSceneSize,

    // What the engine shows the window (plugin -> UI/host).
    kMeterLevel = kInputParamCount, kMeterHits, kMeterGaps,
    kMeterReact, // six of them, one per module
    kMeterAngle = kMeterReact + kModuleCount, kMeterCents, kMeterOutL, kMeterOutR,
    kParamCount
};

/** The parameter of a scene (0 = A, 1 = B). */
constexpr uint32_t sceneParam(int scene, uint32_t s) { return (scene == 0 ? kSceneA : kSceneB) + s; }
/** One of a module's five controls in a scene. */
inline uint32_t moduleParam(int scene, int module, uint32_t which) { return sceneParam(scene, kModuleBase[module] + which); }

enum ParamKind { kLinear, kToggle, kChoice, kMeter };

static const char* const kMainsNames[] = {"50 Hz", "60 Hz"};
static const char* const kIdleNames[] = {"Stop", "Keep"};
static const char* const kDriveTypes[] = {"Tube", "Transistor", "Diode", "Fuzz"};
static const char* const kMotionTypes[] = {"Off-centre", "Warped", "Slipping Belt", "Tired Motor"};
static const char* const kSpeedNames[] = {"33\xE2\x85\x93", "45", "78"};
static const char* const kVinylTypes[] = {"Dusty", "Scratched", "Worn Groove", "Shellac"};
static const char* const kNoiseTypes[] = {"Hum", "Buzz", "Static", "Room Tone"};
static const char* const kNoiseModes[] = {"Follow", "Duck"};
static const char* const kFilterTypes[] = {"Tilt", "Band", "Ladder"};
static const char* const kSpaceTypes[] = {"Room", "Chamber", "Spring", "Early Digital"};

static const char* const kModuleNames[kModuleCount] = {"Drive", "Motion", "Vinyl", "Noise", "Filter", "Space"};
static const char* const* const kModuleTypes[kModuleCount] = {kDriveTypes, kMotionTypes, kVinylTypes, kNoiseTypes, kFilterTypes, kSpaceTypes};
static const int kModuleTypeCount[kModuleCount] = {4, 4, 4, 4, 3, 4};
static const char* const kThirdNames[kModuleCount] = {"Tone", "Speed", "Wear", "Colour", "Resonance", "Decay"};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

// One scene. S is its name in words ("A"), P the start of its symbols ("a"); the six numbers after
// them are the Amounts of the modules, the only defaults in which the two scenes differ: A is mild,
// B is heavy.
#define HNL_PATINA_SCENE(S, P, DRIVE, MOTION, MREACT, VINYL, WEAR, NOISE, FILTER, SPACE) \
    {S " Drive", P "_drive_on", "", 0.f, 1.f, 1.f, kToggle, nullptr}, \
    {S " Drive Type", P "_drive_type", "", 0.f, 3.f, 0.f, kChoice, kDriveTypes}, \
    {S " Drive Amount", P "_drive_amount", "%", 0.f, 100.f, DRIVE, kLinear, nullptr}, \
    {S " Drive React", P "_drive_react", "%", 0.f, 100.f, 40.f, kLinear, nullptr}, \
    {S " Drive Tone", P "_drive_tone", "%", -100.f, 100.f, 0.f, kLinear, nullptr}, \
    {S " Motion", P "_motion_on", "", 0.f, 1.f, 1.f, kToggle, nullptr}, \
    {S " Motion Type", P "_motion_type", "", 0.f, 3.f, 0.f, kChoice, kMotionTypes}, \
    {S " Motion Amount", P "_motion_amount", "%", 0.f, 100.f, MOTION, kLinear, nullptr}, \
    {S " Motion React", P "_motion_react", "%", 0.f, 100.f, MREACT, kLinear, nullptr}, \
    {S " Motion Speed", P "_motion_speed", "", 0.f, 2.f, 0.f, kChoice, kSpeedNames}, \
    {S " Vinyl", P "_vinyl_on", "", 0.f, 1.f, 1.f, kToggle, nullptr}, \
    {S " Vinyl Type", P "_vinyl_type", "", 0.f, 3.f, 0.f, kChoice, kVinylTypes}, \
    {S " Vinyl Amount", P "_vinyl_amount", "%", 0.f, 100.f, VINYL, kLinear, nullptr}, \
    {S " Vinyl React", P "_vinyl_react", "%", 0.f, 100.f, 60.f, kLinear, nullptr}, \
    {S " Vinyl Wear", P "_vinyl_wear", "%", 0.f, 100.f, WEAR, kLinear, nullptr}, \
    {S " Noise", P "_noise_on", "", 0.f, 1.f, 0.f, kToggle, nullptr}, \
    {S " Noise Type", P "_noise_type", "", 0.f, 3.f, 0.f, kChoice, kNoiseTypes}, \
    {S " Noise Amount", P "_noise_amount", "%", 0.f, 100.f, NOISE, kLinear, nullptr}, \
    {S " Noise React", P "_noise_react", "%", 0.f, 100.f, 50.f, kLinear, nullptr}, \
    {S " Noise Colour", P "_noise_colour", "%", 0.f, 100.f, 40.f, kLinear, nullptr}, \
    {S " Noise React Mode", P "_noise_mode", "", 0.f, 1.f, 0.f, kChoice, kNoiseModes}, \
    {S " Filter", P "_filter_on", "", 0.f, 1.f, 1.f, kToggle, nullptr}, \
    {S " Filter Type", P "_filter_type", "", 0.f, 2.f, 0.f, kChoice, kFilterTypes}, \
    {S " Filter Amount", P "_filter_amount", "%", 0.f, 100.f, FILTER, kLinear, nullptr}, \
    {S " Filter React", P "_filter_react", "%", 0.f, 100.f, 30.f, kLinear, nullptr}, \
    {S " Filter Resonance", P "_filter_res", "%", 0.f, 100.f, 0.f, kLinear, nullptr}, \
    {S " Space", P "_space_on", "", 0.f, 1.f, 1.f, kToggle, nullptr}, \
    {S " Space Type", P "_space_type", "", 0.f, 3.f, 0.f, kChoice, kSpaceTypes}, \
    {S " Space Amount", P "_space_amount", "%", 0.f, 100.f, SPACE, kLinear, nullptr}, \
    {S " Space React", P "_space_react", "%", 0.f, 100.f, 40.f, kLinear, nullptr}, \
    {S " Space Decay", P "_space_decay", "s", 0.2f, 4.f, 0.8f, kLinear, nullptr}

static const ParamInfo kParams[kParamCount] = {
    {"Morph", "morph", "%", 0.f, 100.f, 30.f, kLinear, nullptr},
    {"Mix", "mix", "%", 0.f, 100.f, 100.f, kLinear, nullptr},
    {"Output", "output", "dB", -24.f, 12.f, 0.f, kLinear, nullptr},
    {"Mains", "mains", "", 0.f, 1.f, 0.f, kChoice, kMainsNames},
    {"In Silence", "idle", "", 0.f, 1.f, 0.f, kChoice, kIdleNames},
    HNL_PATINA_SCENE("A", "a", 20.f, 20.f, 30.f, 20.f, 20.f, 20.f, 15.f, 15.f),
    HNL_PATINA_SCENE("B", "b", 55.f, 50.f, 50.f, 50.f, 50.f, 40.f, 45.f, 30.f),

    {"Listener Level", "meter_level", "dB", -12.f, 12.f, -12.f, kMeter, nullptr},
    {"Listener Hits", "meter_hits", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"Listener Gaps", "meter_gaps", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"React Drive", "meter_react_drive", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"React Motion", "meter_react_motion", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"React Vinyl", "meter_react_vinyl", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"React Noise", "meter_react_noise", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"React Filter", "meter_react_filter", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"React Space", "meter_react_space", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"Platter Angle", "meter_angle", "", 0.f, 1.f, 0.f, kMeter, nullptr},
    {"Platter Speed", "meter_cents", "cent", -100.f, 100.f, 0.f, kMeter, nullptr},
    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
};

#undef HNL_PATINA_SCENE

/** Knob position 0..1 for a parameter value. */
inline float paramToNorm(uint32_t id, float value) {
    const ParamInfo& p = kParams[id];
    return (value - p.min) / (p.max - p.min);
}

/** Parameter value for a knob position 0..1. */
inline float normToParam(uint32_t id, float norm) {
    const ParamInfo& p = kParams[id];
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    return p.min + norm * (p.max - p.min);
}
