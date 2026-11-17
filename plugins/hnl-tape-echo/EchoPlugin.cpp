// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Tape Echo: plugin side. Wraps hnl::TapeEcho for DPF.
#include <atomic>
#include <cstring>

#include "DistrhoPlugin.hpp"

#include "dsp/TapeEcho.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class EchoPlugin : public Plugin {
public:
    EchoPlugin() : Plugin(kParamCount, 0, 1) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = kParams[i].def;
        prepare(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HNLTapeEcho"; }
    const char* getDescription() const override {
        return "Tape echo with three playback heads and a spring reverb: saturating repeats, wow and flutter, self-oscillation.";
    }
    const char* getMaker() const override { return "HNL Design"; }
    const char* getHomePage() const override { return "https://hnldesign.nl"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(HNL_VERSION_MAJOR, HNL_VERSION_MINOR, HNL_VERSION_PATCH); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override {
        const ParamInfo& p = kParams[index];
        parameter.name = p.name;
        parameter.symbol = p.symbol;
        parameter.unit = p.unit;
        parameter.ranges.min = p.min;
        parameter.ranges.max = p.max;
        parameter.ranges.def = p.def;
        switch (p.kind) {
        case kLinear: parameter.hints = kParameterIsAutomatable; break;
        case kLog: parameter.hints = kParameterIsAutomatable | kParameterIsLogarithmic; break;
        case kToggle: parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger; break;
        case kChoice: {
            const int count = static_cast<int>(p.max) + 1;
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];
            for (int i = 0; i < count; ++i) {
                values[i].value = static_cast<float>(i);
                values[i].label = p.choices[i];
            }
            parameter.enumValues.count = static_cast<uint8_t>(count);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            break;
        }
        case kMeter: parameter.hints = kParameterIsOutput; break;
        }
    }

    /** The name of the preset last chosen in the UI, saved with the song. */
    void initState(uint32_t, State& state) override {
        state.key = "preset";
        state.label = "Preset";
        state.defaultValue = "Default";
    }
    void setState(const char*, const char*) override {} // only stored by the host

    float getParameterValue(uint32_t index) const override {
        if (index >= kInputParamCount) return meters_[index - kInputParamCount];
        return values_[index].load(std::memory_order_relaxed);
    }

    void setParameterValue(uint32_t index, float value) override {
        if (index >= kInputParamCount) return;
        // Within the parameter's range, whatever the host sends; the default when it is not a number.
        const ParamInfo& p = kParams[index];
        values_[index].store(hnl::cleanParam(value, p.min, p.max, p.def), std::memory_order_relaxed);
        dirty_.store(true, std::memory_order_release);
    }

    void sampleRateChanged(double newSampleRate) override { prepare(newSampleRate); }

    void activate() override {
        pushParams();
        echo_.reset();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        // Samples that are not numbers would stay in the filters for good: they count as silence.
        hnl::cleanInput(outputs[0], frames);
        hnl::cleanInput(outputs[1], frames);

        // With sync on, the echo time follows the host's tempo.
        const TimePosition& time = getTimePosition();
        const double bpm = time.bbt.valid ? time.bbt.beatsPerMinute : 120.0;
        if (dirty_.exchange(false, std::memory_order_acquire) || bpm != bpm_) {
            bpm_ = bpm;
            pushParams();
        }
        echo_.process(outputs[0], outputs[1], static_cast<int>(frames));
        // Should the engine still have produced something that is not a number, this block is
        // silent and the engine starts again clean, instead of staying broken.
        if (!hnl::isClean(outputs[0], frames) || !hnl::isClean(outputs[1], frames)) {
            std::memset(outputs[0], 0, sizeof(float) * frames);
            std::memset(outputs[1], 0, sizeof(float) * frames);
            hnl::guardTrips().fetch_add(1u, std::memory_order_relaxed);
            echo_.reset();
        }

        // Output peak meters fall 20 dB/s; the tape meter's ballistics are in the engine.
        const float fall = static_cast<float>(20.0 * frames / sampleRate_);
        meters_[kMeterTape - kInputParamCount] = echo_.meters.tape.load(std::memory_order_relaxed);
        meters_[kMeterTime - kInputParamCount] = echo_.meters.headTime.load(std::memory_order_relaxed);
        for (int c = 0; c < 2; ++c) {
            const float lin = echo_.meters.outPeak[c].exchange(0.0f, std::memory_order_relaxed);
            const float db = lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f;
            float& held = meters_[kMeterOutL - kInputParamCount + c];
            held = std::max(db, std::max(held - fall, -100.0f));
        }
    }

private:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        echo_.prepare(sampleRate_);
        pushParams();
        echo_.reset();
    }

    void pushParams() {
        float v[kInputParamCount];
        for (uint32_t i = 0; i < kInputParamCount; ++i) v[i] = values_[i].load(std::memory_order_relaxed);
        hnl::EchoParams p;
        p.input = v[kInput];
        p.time = v[kSync] > 0.5f ? syncedTime(static_cast<int>(std::lround(v[kDivision])), bpm_) : 0.001 * v[kTime];
        for (int k = 0; k < hnl::kHeads; ++k) p.head[k] = v[kHead1 + k] > 0.5f;
        p.intensity = v[kIntensity];
        p.echo = v[kEcho];
        p.reverb = v[kReverb];
        p.bass = v[kBass];
        p.treble = v[kTreble];
        p.wobble = v[kWobble];
        p.noise = v[kNoise];
        p.width = v[kWidth];
        p.dry = v[kDry] > 0.5f;
        echo_.setParams(p);
    }

    hnl::TapeEcho echo_;
    double sampleRate_ = 48000.0, bpm_ = 120.0;
    std::atomic<float> values_[kInputParamCount];
    std::atomic<bool> dirty_{true};
    float meters_[kParamCount - kInputParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EchoPlugin)
};

Plugin* createPlugin() { return new EchoPlugin(); }

END_NAMESPACE_DISTRHO
