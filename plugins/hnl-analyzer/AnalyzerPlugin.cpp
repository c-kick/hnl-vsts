// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Analyzer: plugin side. Wraps hnl::analyzer::Engine for DPF. The sound goes out as it
// came in, sample for sample and without delay; everything here only looks at it.
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include "DistrhoPlugin.hpp"

#include "dsp/Engine.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class AnalyzerPlugin : public Plugin {
public:
    AnalyzerPlugin() : Plugin(kParamCount, 0, kStateCount) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        hnl::analyzer::Engine::of(static_cast<Plugin*>(this), &engine_); // so that the window finds it
        pushParams();
        engine_.prepare(getSampleRate());
    }
    ~AnalyzerPlugin() override { hnl::analyzer::Engine::of(static_cast<Plugin*>(this), nullptr, true); }

protected:
    const char* getLabel() const override { return "HNLAnalyzer"; }
    const char* getDescription() const override {
        return "Mastering analysis: loudness, true peak, RMS statistics, spectrum and stereo figures of a whole track, an advisor for loudness targets, and comparison with a reference.";
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
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
            int count = 0;
            const char* const* names = choiceNames(index, count);
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];
            for (int i = 0; i < count; ++i) {
                values[i].value = static_cast<float>(i);
                values[i].label = names[i];
            }
            parameter.enumValues.count = static_cast<uint8_t>(count);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            break;
        }
        case kMeter: parameter.hints = kParameterIsOutput; break;
        }
    }

    /**
     * Saved with the song: the name of the preset last chosen, the analysis that is kept (as
     * JSON, see dsp/Report.hpp), and which reference the window compares with and how.
     */
    enum { kStatePreset, kStateAnalysis, kStateReference, kStateCount };
    void initState(uint32_t index, State& state) override {
        if (index == kStatePreset) {
            state.key = "preset";
            state.label = "Preset";
            state.defaultValue = "Streaming -14";
        } else if (index == kStateAnalysis) {
            state.key = "analysis";
            state.label = "Analysis";
            state.defaultValue = "";
        } else {
            state.key = "reference";
            state.label = "Reference";
            state.defaultValue = "";
        }
    }

    String getState(const char* key) const override {
        if (std::strcmp(key, "analysis") == 0) return String(const_cast<AnalyzerPlugin*>(this)->engine_.saveState().c_str());
        std::lock_guard<std::mutex> lock(textMutex_);
        return String((std::strcmp(key, "preset") == 0 ? preset_ : reference_).c_str());
    }

    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, "analysis") == 0) {
            engine_.loadState(value); // what is not an analysis leaves the one there is
            return;
        }
        std::lock_guard<std::mutex> lock(textMutex_);
        if (std::strcmp(key, "preset") == 0) preset_ = value;
        else if (std::strcmp(key, "reference") == 0) reference_ = value;
    }

    float getParameterValue(uint32_t index) const override {
        switch (index) {
        case kMeterLufsM: return engine_.lufsM.load(std::memory_order_relaxed);
        case kMeterLufsS: return engine_.lufsS.load(std::memory_order_relaxed);
        case kMeterLufsI: return engine_.lufsI.load(std::memory_order_relaxed);
        case kMeterTruePeak: return engine_.truePeak.load(std::memory_order_relaxed);
        case kMeterPlr: return engine_.plrNow.load(std::memory_order_relaxed);
        case kMeterLra: return engine_.lra.load(std::memory_order_relaxed);
        default: return index < kInputParamCount ? values_[index].load(std::memory_order_relaxed) : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override {
        if (index >= kInputParamCount) return;
        // Within the parameter's range, whatever the host sends; the default when it is not a number.
        const ParamInfo& p = kParams[index];
        values_[index].store(hnl::cleanParam(value, p.min, p.max, p.def), std::memory_order_relaxed);
        pushParams();
    }

    void sampleRateChanged(double newSampleRate) override { engine_.prepare(newSampleRate); }
    void deactivate() override { engine_.deactivate(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        // The sound goes out as it came in. Only a sample that is not a number becomes silence, as in
        // every plugin here: it would stay in the sums of the meter, and in whatever follows, for good.
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        hnl::cleanInput(outputs[0], frames);
        hnl::cleanInput(outputs[1], frames);

        hnl::ScopedNoDenormals noDenormals;
        const TimePosition& time = getTimePosition();
        engine_.process(outputs[0], outputs[1], frames, time.playing, time.frame, true);
    }

private:
    /** The settings as the engine wants them. Only stores a few numbers, so it may run on any thread. */
    void pushParams() {
        engine_.setMode(static_cast<int>(std::lround(values_[kMode].load(std::memory_order_relaxed))));
        engine_.setAutomatic(values_[kAuto].load(std::memory_order_relaxed) > 0.5f);
        engine_.setView(static_cast<int>(std::lround(values_[kRmsWindow].load(std::memory_order_relaxed))), values_[kRmsRef].load(std::memory_order_relaxed) < 0.5f);
    }

    hnl::analyzer::Engine engine_;
    std::atomic<float> values_[kInputParamCount];
    mutable std::mutex textMutex_;
    std::string preset_ = "Streaming -14", reference_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerPlugin)
};

Plugin* createPlugin() { return new AnalyzerPlugin(); }

END_NAMESPACE_DISTRHO
