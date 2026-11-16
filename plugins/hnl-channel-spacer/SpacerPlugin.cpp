// HNL Channel Spacer: plugin side. Wraps hnl::Spacer for DPF.
#include <atomic>
#include <cstdio>
#include <cstring>

#include "DistrhoPlugin.hpp"

#include "dsp/Spacer.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class SpacerPlugin : public Plugin {
public:
    SpacerPlugin() : Plugin(kParamCount, 0, 1) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(paramInfo(i).def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = paramInfo(i).def;
        prepare(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HNLChannelSpacer"; }
    const char* getDescription() const override {
        return "Makes room in one channel for another: turns down only the frequency ranges where the sidechain signal is playing.";
    }
    const char* getMaker() const override { return "HNL Design"; }
    const char* getHomePage() const override { return "https://hnldesign.nl"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override {
        if (input && index >= 2) {
            port.hints = kAudioPortIsSidechain;
            port.name = index == 2 ? "Sidechain Left" : "Sidechain Right";
            port.symbol = index == 2 ? "sidechain_l" : "sidechain_r";
            port.groupId = kPortGroupNone;
            return;
        }
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override {
        const ParamInfo& p = paramInfo(index);
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
        case kBandMeter: {
            // One per band, for the display only: kept out of the host's parameter lists.
            const uint32_t band = (index - kMeterCut) % kBands + 1;
            char text[32];
            parameter.hints = kParameterIsOutput | kParameterIsHidden;
            std::snprintf(text, sizeof(text), "%s Band %u", p.name, band);
            parameter.name = text;
            std::snprintf(text, sizeof(text), "%s_%u", p.symbol, band);
            parameter.symbol = text;
            break;
        }
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
        values_[index].store(value, std::memory_order_relaxed);
        dirty_.store(true, std::memory_order_release);
    }

    void sampleRateChanged(double newSampleRate) override { prepare(newSampleRate); }

    void activate() override {
        pushParams();
        spacer_.reset();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        if (dirty_.exchange(false, std::memory_order_acquire)) pushParams();
        spacer_.process(outputs[0], outputs[1], inputs[2], inputs[3], static_cast<int>(frames));

        // Output peak meters fall 20 dB/s; the band meters follow the engine.
        const float fall = static_cast<float>(20.0 * frames / sampleRate_);
        for (int c = 0; c < 2; ++c) {
            const float lin = spacer_.meters.outPeak[c].exchange(0.0f, std::memory_order_relaxed);
            const float db = lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f;
            float& held = meters_[kMeterOutL - kInputParamCount + c];
            held = std::max(db, std::max(held - fall, -100.0f));
        }
        for (int k = 0; k < kBands; ++k) {
            meters_[kMeterCut - kInputParamCount + k] = spacer_.meters.cut[k].load(std::memory_order_relaxed);
            meters_[kMeterLevel - kInputParamCount + k] = spacer_.meters.level[k].load(std::memory_order_relaxed);
        }
    }

private:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        spacer_.prepare(sampleRate_);
        pushParams();
        spacer_.reset();
    }

    void pushParams() {
        float v[kInputParamCount];
        for (uint32_t i = 0; i < kInputParamCount; ++i) v[i] = values_[i].load(std::memory_order_relaxed);
        hnl::SpacerParams p;
        p.sensitivity = v[kSensitivity];
        p.depth = v[kDepth];
        p.attack = v[kAttack];
        p.release = v[kRelease];
        for (int i = 0; i < 4; ++i) p.range[i] = v[kLow + i];
        p.link = v[kLink];
        p.self = v[kSource] > 0.5f;
        p.listen = static_cast<int>(std::lround(v[kListen]));
        spacer_.setParams(p);
    }

    hnl::Spacer spacer_;
    double sampleRate_ = 48000.0;
    std::atomic<float> values_[kInputParamCount];
    std::atomic<bool> dirty_{true};
    float meters_[kParamCount - kInputParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpacerPlugin)
};

Plugin* createPlugin() { return new SpacerPlugin(); }

END_NAMESPACE_DISTRHO
