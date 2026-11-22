// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Sampler: plugin side. Wraps hnl::sampler::Sampler for DPF: feeds it MIDI, tempo and the
// host's parameters, and keeps the instrument as state.
#include <atomic>
#include <cstring>

#include "DistrhoPlugin.hpp"

#include "dsp/Sampler.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class SamplerPlugin : public Plugin {
public:
    SamplerPlugin() : Plugin(kParamCount, 0, 1) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = kParams[i].def;
        hnl::sampler::Sampler::of(static_cast<Plugin*>(this), &sampler_); // so that the UI finds it
        prepare(getSampleRate());
    }
    ~SamplerPlugin() override { hnl::sampler::Sampler::of(static_cast<Plugin*>(this), nullptr, true); }

protected:
    const char* getLabel() const override { return "HNLSampler"; }
    const char* getDescription() const override {
        return "Sampler: multisamples over keys and velocities in eight parts, with loops, unison, an oscillator, filter, envelopes, LFOs and a modulation matrix.";
    }
    const char* getMaker() const override { return "HNL Design"; }
    const char* getHomePage() const override { return "https://hnldesign.nl"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(HNL_VERSION_MAJOR, HNL_VERSION_MINOR, HNL_VERSION_PATCH); }

    /** Eight stereo outputs, each a group of its own, so that a host shows them as pairs. */
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override {
        Plugin::initAudioPort(input, index, port);
        const uint32_t pair = index / 2;
        char text[32];
        std::snprintf(text, sizeof(text), "Out %u %s", pair + 1, index % 2 == 0 ? "L" : "R");
        port.name = text;
        std::snprintf(text, sizeof(text), "out_%u_%s", pair + 1, index % 2 == 0 ? "l" : "r");
        port.symbol = text;
        port.groupId = pair;
    }
    void initPortGroup(uint32_t groupId, PortGroup& group) override {
        char text[32];
        std::snprintf(text, sizeof(text), "Out %u-%u", 2 * groupId + 1, 2 * groupId + 2);
        group.name = text;
        std::snprintf(text, sizeof(text), "out_%u", groupId + 1);
        group.symbol = text;
    }

    void initParameter(uint32_t index, Parameter& parameter) override {
        const ParamInfo& p = kParams[index];
        parameter.name = p.name;
        parameter.symbol = p.symbol;
        parameter.unit = p.unit;
        parameter.ranges.min = p.min;
        parameter.ranges.max = p.max;
        parameter.ranges.def = p.def;
        parameter.hints = p.meter ? kParameterIsOutput : kParameterIsAutomatable;
    }

    /** The instrument, as the text of dsp/Model.hpp. Sounds are named by their path, not stored. */
    void initState(uint32_t, State& state) override {
        state.key = "instrument";
        state.label = "Instrument";
        state.defaultValue = "";
    }
    String getState(const char* key) const override {
        if (std::strcmp(key, "instrument") != 0) return String();
        return String(sampler_.text().c_str());
    }
    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, "instrument") != 0 || value == nullptr || value[0] == '\0') return;
        sampler_.setText(value); // text that is not an instrument changes nothing
    }

    float getParameterValue(uint32_t index) const override {
        if (index >= kInputParamCount) return meters_[index - kInputParamCount];
        return values_[index].load(std::memory_order_relaxed);
    }

    void setParameterValue(uint32_t index, float value) override {
        if (index >= kInputParamCount) return;
        // Within the parameter's range, whatever the host sends; the default when it is not a number.
        const ParamInfo& p = kParams[index];
        values_[index].store(hnl::cleanParam(value, p.min, p.max, p.def), std::memory_order_relaxed);
    }

    void sampleRateChanged(double newSampleRate) override { prepare(newSampleRate); }

    void activate() override { sampler_.reset(); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* events, uint32_t eventCount) override {
        hnl::sampler::Engine& engine = sampler_.engine;
        sampler_.begin(); // a new instrument is there before the first note of the block
        engine.setMaster(values_[kVolume].load(std::memory_order_relaxed));
        for (int m = 0; m < hnl::sampler::kMacros; ++m) engine.setMacro(m, 0.01f * values_[kMacro1 + m].load(std::memory_order_relaxed));
        const TimePosition& time = getTimePosition();
        engine.setTempo(time.bbt.valid ? time.bbt.beatsPerMinute : 120.0);

        // Render up to each event, then apply it, so notes start on their exact sample.
        float* out[DISTRHO_PLUGIN_NUM_OUTPUTS];
        uint32_t done = 0;
        for (uint32_t e = 0; e < eventCount; ++e) {
            const MidiEvent& ev = events[e];
            const uint32_t at = ev.frame < frames ? ev.frame : frames;
            if (at > done) {
                for (int c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c) out[c] = outputs[c] + done;
                sampler_.process(out, static_cast<int>(at - done));
                done = at;
            }
            if (ev.size >= 1 && ev.size <= MidiEvent::kDataSize) handle(ev.data, ev.size);
        }
        for (int c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c) out[c] = outputs[c] + done;
        sampler_.process(out, static_cast<int>(frames - done));
        // Should the engine still have produced something that is not a number, this block is
        // silent and the engine starts again clean, instead of staying broken.
        bool clean = true;
        for (int c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c) clean = clean && hnl::isClean(outputs[c], frames);
        if (!clean) {
            for (int c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c) std::memset(outputs[c], 0, sizeof(float) * frames);
            hnl::guardTrips().fetch_add(1u, std::memory_order_relaxed);
            sampler_.reset();
        }

        // Output peak meters fall 20 dB/s.
        const float fall = static_cast<float>(20.0 * frames / sampleRate_);
        for (int c = 0; c < 2; ++c) {
            const float lin = engine.meters.outPeak[c].exchange(0.0f, std::memory_order_relaxed);
            const float db = lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f;
            float& held = meters_[kMeterOutL - kInputParamCount + c];
            held = std::max(std::min(db, 24.0f), std::max(held - fall, -100.0f));
        }
        meters_[kMeterVoices - kInputParamCount] = static_cast<float>(engine.meters.voices.load(std::memory_order_relaxed));
    }

private:
    void handle(const uint8_t* data, uint32_t size) {
        hnl::sampler::Engine& engine = sampler_.engine;
        const uint8_t status = data[0] & 0xf0;
        const int channel = data[0] & 15;
        if (status == 0x90 && size >= 3) engine.noteOn(channel, data[1] & 127, data[2] & 127);
        else if (status == 0x80 && size >= 3) engine.noteOff(channel, data[1] & 127, data[2] & 127);
        else if (status == 0xa0 && size >= 3) engine.polyPressure(channel, data[1] & 127, data[2] & 127);
        else if (status == 0xb0 && size >= 3) engine.controller(channel, data[1] & 127, data[2] & 127);
        else if (status == 0xd0 && size >= 2) engine.channelPressure(channel, data[1] & 127);
        else if (status == 0xe0 && size >= 3) engine.pitchBend(channel, (((data[2] & 127) << 7 | (data[1] & 127)) - 8192) / 8192.0);
    }

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        sampler_.prepare(sampleRate_);
    }

    hnl::sampler::Sampler sampler_;
    double sampleRate_ = 48000.0;
    std::atomic<float> values_[kInputParamCount];
    float meters_[kParamCount - kInputParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerPlugin)
};

Plugin* createPlugin() { return new SamplerPlugin(); }

END_NAMESPACE_DISTRHO
