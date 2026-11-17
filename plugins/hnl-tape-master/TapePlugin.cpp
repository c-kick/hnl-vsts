// HNL Tape Master: plugin side. Wraps hnl::TapeMachine for DPF.
#include <atomic>
#include <cstring>

#include "DistrhoPlugin.hpp"

#include "dsp/TapeMachine.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class TapePlugin : public Plugin {
public:
    TapePlugin() : Plugin(kParamCount, 0, 2) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = kParams[i].def;
        prepare(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HNLTapeMaster"; }
    const char* getDescription() const override {
        return "Two-track mastering tape machine: saturation, tape compression, head response, wow, flutter and hiss.";
    }
    const char* getMaker() const override { return "HNL Design"; }
    const char* getHomePage() const override { return "https://hnldesign.nl"; }
    const char* getLicense() const override { return "ISC"; }
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

    /** What the UI keeps with the song: the name of the preset last chosen and the meter design. */
    void initState(uint32_t index, State& state) override {
        if (index == 0) {
            state.key = "preset";
            state.label = "Preset";
            state.defaultValue = "Default";
        } else {
            state.key = "meter";
            state.label = "Meter";
            state.defaultValue = "classic";
        }
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
        machine_.reset();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        // Samples that are not numbers would stay in the filters for good: they count as silence.
        hnl::cleanInput(outputs[0], frames);
        hnl::cleanInput(outputs[1], frames);
        if (dirty_.exchange(false, std::memory_order_acquire)) pushParams();
        machine_.process(outputs[0], outputs[1], static_cast<int>(frames));
        // Should the engine still have produced something that is not a number, this block is
        // silent and the engine starts again clean, instead of staying broken.
        if (!hnl::isClean(outputs[0], frames) || !hnl::isClean(outputs[1], frames)) {
            std::memset(outputs[0], 0, sizeof(float) * frames);
            std::memset(outputs[1], 0, sizeof(float) * frames);
            hnl::guardTrips().fetch_add(1u, std::memory_order_relaxed);
            machine_.reset();
        }

        // Output peak meters fall 20 dB/s; the VU ballistics are in the machine.
        const float fall = static_cast<float>(20.0 * frames / sampleRate_);
        for (int c = 0; c < 2; ++c) {
            meters_[kMeterVuL - kInputParamCount + c] = machine_.meters.vu[c].load(std::memory_order_relaxed);
            const float lin = machine_.meters.outPeak[c].exchange(0.0f, std::memory_order_relaxed);
            const float db = lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f;
            float& held = meters_[kMeterOutL - kInputParamCount + c];
            held = std::max(db, std::max(held - fall, -100.0f));
        }
    }

private:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        machine_.prepare(sampleRate_);
        pushParams();
        setLatency(static_cast<uint32_t>(machine_.latency()));
    }

    void pushParams() {
        float v[kInputParamCount];
        for (uint32_t i = 0; i < kInputParamCount; ++i) v[i] = values_[i].load(std::memory_order_relaxed);
        hnl::TapeParams p;
        p.record = v[kRecord];
        p.reproduce = v[kReproduce];
        p.link = v[kLink] > 0.5f;
        p.speed = static_cast<int>(std::lround(v[kSpeed]));
        p.formula = static_cast<int>(std::lround(v[kFormula]));
        p.head = static_cast<int>(std::lround(v[kHead]));
        p.path = static_cast<int>(std::lround(v[kPath]));
        p.bias = v[kBias];
        p.wow = v[kWow];
        p.flutter = v[kFlutter];
        p.noise = v[kNoise];
        p.calibration = v[kCalibration];
        machine_.setParams(p);
    }

    hnl::TapeMachine machine_;
    double sampleRate_ = 48000.0;
    std::atomic<float> values_[kInputParamCount];
    std::atomic<bool> dirty_{true};
    float meters_[kParamCount - kInputParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapePlugin)
};

Plugin* createPlugin() { return new TapePlugin(); }

END_NAMESPACE_DISTRHO
