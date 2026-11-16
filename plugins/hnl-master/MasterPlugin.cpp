// HNL Master: plugin side. Wraps hm::MasterChain for DPF.
#include <atomic>
#include <cstring>

#include "DistrhoPlugin.hpp"

#include "dsp/MasterChain.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class MasterPlugin : public Plugin {
public:
    MasterPlugin() : Plugin(kParamCount, 0, 1) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = kParams[i].def;
        prepare(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HNLMaster"; }
    const char* getDescription() const override {
        return "Mastering chain: 3-band compressor, equaliser, exciter, widener, reverb and true-peak limiter.";
    }
    const char* getMaker() const override { return "HNL Design"; }
    const char* getHomePage() const override { return "https://hnldesign.nl"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }

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
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[3];
            for (int i = 0; i < 3; ++i) {
                values[i].value = static_cast<float>(i);
                values[i].label = kExciterModes[i];
            }
            parameter.enumValues.count = 3;
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

    float getParameterValue(uint32_t index) const override {
        if (index >= kInputParamCount) return meters_[index - kInputParamCount];
        return values_[index].load(std::memory_order_relaxed);
    }

    void setParameterValue(uint32_t index, float value) override {
        if (index >= kInputParamCount) return;
        values_[index].store(value, std::memory_order_relaxed);
        dirty_.store(true, std::memory_order_release);
    }

    /** The UI sends "reset" to clear integrated loudness and the true-peak hold.
        "preset" is only stored by the host; the UI sets the parameters itself. */
    void setState(const char* key, const char*) override {
        if (std::strcmp(key, "reset") == 0) resetMeters_.store(true, std::memory_order_relaxed);
    }

    void sampleRateChanged(double newSampleRate) override { prepare(newSampleRate); }

    void activate() override {
        pushParams();
        chain_.reset();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);

        if (dirty_.exchange(false, std::memory_order_acquire)) pushParams();
        if (resetMeters_.exchange(false, std::memory_order_relaxed)) {
            chain_.resetIntegratedLoudness();
            chain_.meters.truePeakMax.store(0.0f, std::memory_order_relaxed);
        }

        chain_.process(outputs[0], outputs[1], static_cast<int>(frames));
        updateMeters(frames);
    }

private:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        chain_.prepare(sampleRate_);
        pushParams();
        setLatency(static_cast<uint32_t>(chain_.latency()));
    }

    void pushParams() {
        float v[kInputParamCount];
        for (uint32_t i = 0; i < kInputParamCount; ++i) v[i] = values_[i].load(std::memory_order_relaxed);

        hm::Params p;
        p.inputGain = v[kInputGain];
        p.compOn = v[kCompOn] > 0.5f;
        p.xoverLow = v[kXoverLow];
        p.xoverHigh = v[kXoverHigh];
        for (int b = 0; b < 3; ++b) {
            const float* band = v + kLowThreshold + 5 * b;
            p.band[b] = {band[0], band[1], band[2], band[3], band[4]};
        }
        p.exciterOn = v[kExcOn] > 0.5f;
        p.exciterFreq = v[kExcFreq];
        p.exciterDrive = v[kExcDrive];
        p.exciterColour = 0.01 * v[kExcColour];
        p.exciterMix = v[kExcMix];
        p.exciterMode = static_cast<int>(std::lround(v[kExcMode]));
        p.eqOn = v[kEqOn] > 0.5f;
        p.eqLowFreq = v[kEqLowFreq];
        p.eqLowGain = v[kEqLowGain];
        p.eqMidFreq = v[kEqMidFreq];
        p.eqMidGain = v[kEqMidGain];
        p.eqMidQ = v[kEqMidQ];
        p.eqHighFreq = v[kEqHighFreq];
        p.eqHighGain = v[kEqHighGain];
        p.widenerOn = v[kWideOn] > 0.5f;
        p.width = v[kWideWidth];
        p.widenerFreq = v[kWideFreq];
        p.reverbOn = v[kRevOn] > 0.5f;
        p.reverbDecay = v[kRevDecay];
        p.reverbDamping = v[kRevDamping];
        p.reverbMix = v[kRevMix];
        p.limiterOn = v[kLimOn] > 0.5f;
        p.limiterGain = v[kLimGain];
        p.limiterCeiling = v[kLimCeiling];
        p.limiterRelease = v[kLimRelease];
        chain_.setParams(p);
    }

    /** Peak meters fall 20 dB/s; gain-reduction meters recover in about 150 ms. */
    void updateMeters(uint32_t frames) {
        const double dt = frames / sampleRate_;
        const float peakFall = static_cast<float>(20.0 * dt);
        const float grKeep = static_cast<float>(std::exp(-dt / 0.15));
        auto& m = chain_.meters;
        float* out = meters_;

        std::atomic<float>* peaks[4] = {&m.inPeak[0], &m.inPeak[1], &m.outPeak[0], &m.outPeak[1]};
        for (int i = 0; i < 4; ++i) {
            const float db = toDb(peaks[i]->exchange(0.0f, std::memory_order_relaxed));
            out[i] = std::max(db, std::max(out[i] - peakFall, -100.0f));
        }
        out[kMeterTruePeak - kInputParamCount] = toDb(m.truePeakMax.load(std::memory_order_relaxed));

        std::atomic<float>* grs[4] = {&m.compGr[0], &m.compGr[1], &m.compGr[2], &m.limGr};
        for (int i = 0; i < 4; ++i) {
            float& held = out[kMeterGrLow - kInputParamCount + i];
            held = std::max(-60.0f, std::min(grs[i]->exchange(0.0f, std::memory_order_relaxed), held * grKeep));
        }
        out[kMeterLufsM - kInputParamCount] = m.lufsMomentary.load(std::memory_order_relaxed);
        out[kMeterLufsS - kInputParamCount] = m.lufsShort.load(std::memory_order_relaxed);
        out[kMeterLufsI - kInputParamCount] = m.lufsIntegrated.load(std::memory_order_relaxed);
    }

    static float toDb(float lin) { return lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f; }

    hm::MasterChain chain_;
    double sampleRate_ = 48000.0;
    std::atomic<float> values_[kInputParamCount];
    std::atomic<bool> dirty_{true}, resetMeters_{false};
    float meters_[kParamCount - kInputParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterPlugin)
};

Plugin* createPlugin() { return new MasterPlugin(); }

END_NAMESPACE_DISTRHO
