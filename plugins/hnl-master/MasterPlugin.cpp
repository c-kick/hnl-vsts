// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Master: plugin side. Wraps hnl::MasterChain for DPF.
#include <atomic>
#include <cstring>
#include <memory>

#include "DistrhoPlugin.hpp"

#include "../../common/Link.hpp"
#include "dsp/MasterChain.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

static_assert(kSpectrumBands == hnl::Spectrum::kBands, "the spectrum has a parameter for each band");

class MasterPlugin : public Plugin {
public:
    MasterPlugin() : Plugin(kParamCount, 0, 3) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = kParams[i].def;
        prepare(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HNLMaster"; }
    const char* getDescription() const override {
        return "Mastering chain: 3-band compressor, equaliser, exciter, widener, reverb and true-peak limiter, with a spectrum, a loudness-matched bypass and dither.";
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
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[3];
            for (int i = 0; i < 3; ++i) {
                values[i].value = static_cast<float>(i);
                values[i].label = choiceNames(index)[i];
            }
            parameter.enumValues.count = 3;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            break;
        }
        case kMeter: parameter.hints = kParameterIsOutput; break;
        }
    }

    /** Saved with the song for the UI: the name of the preset last chosen, which stages are folded
        (a 1 or 0 for each stage with a switch, in signal order), and whether the compressor's bands are linked. */
    void initState(uint32_t index, State& state) override {
        if (index == 0) {
            state.key = "preset";
            state.label = "Preset";
            state.defaultValue = "Default";
        } else if (index == 1) {
            state.key = "folded";
            state.label = "Folded stages";
            state.defaultValue = "";
        } else {
            state.key = "link";
            state.label = "Linked bands";
            state.defaultValue = "0";
        }
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
        dirty_.store(true, std::memory_order_release);
    }

    /** The UI sends "reset" to clear integrated loudness and the true-peak hold.
        "preset", "folded" and "link" are only stored by the host; the UI acts on them itself. */
    void setState(const char* key, const char*) override {
        if (std::strcmp(key, "reset") == 0) resetMeters_.store(true, std::memory_order_relaxed);
    }

    void sampleRateChanged(double newSampleRate) override { prepare(newSampleRate); }

    void activate() override {
        pushParams();
        chain_.reset();
        // From here on HNL Analyzer finds this instance and reads its meters (common/Link.hpp). Only an instance that
        // the host prepares for audio enters: a VST3 host gets a second one, for the knobs alone, that never does.
        if (link_ == nullptr && !isDummyInstance()) {
            link_.reset(new hnl::link::Publisher(hnl::link::kMaster, d_version(HNL_VERSION_MAJOR, HNL_VERSION_MINOR, HNL_VERSION_PATCH)));
            publish(0);
        }
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        if (outputs[0] != inputs[0]) std::memcpy(outputs[0], inputs[0], sizeof(float) * frames);
        if (outputs[1] != inputs[1]) std::memcpy(outputs[1], inputs[1], sizeof(float) * frames);
        // Samples that are not numbers would stay in the filters for good: they count as silence.
        hnl::cleanInput(outputs[0], frames);
        hnl::cleanInput(outputs[1], frames);

        if (dirty_.exchange(false, std::memory_order_acquire)) pushParams();
        if (resetMeters_.exchange(false, std::memory_order_relaxed)) {
            chain_.resetIntegratedLoudness();
            chain_.meters.truePeakMax.store(0.0f, std::memory_order_relaxed);
        }

        chain_.process(outputs[0], outputs[1], static_cast<int>(frames));
        // Should the engine still have produced something that is not a number, this block is
        // silent and the engine starts again clean, instead of staying broken.
        if (!hnl::isClean(outputs[0], frames) || !hnl::isClean(outputs[1], frames)) {
            std::memset(outputs[0], 0, sizeof(float) * frames);
            std::memset(outputs[1], 0, sizeof(float) * frames);
            hnl::guardTrips().fetch_add(1u, std::memory_order_relaxed);
            chain_.reset();
        }
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

        hnl::Params p;
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
        p.eqFirst = v[kEqFirst] > 0.5f;
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
        p.limiterSoft = v[kLimSoft] > 0.5f;
        p.limiterAttack = v[kLimAttack];
        p.eqLowCut = lowCutIsOff(v[kEqLowCut]) ? 0.0 : v[kEqLowCut];
        p.eqMode = static_cast<int>(std::lround(v[kEqMode]));
        for (int b = 0; b < 3; ++b) {
            p.bandSolo[b] = v[kLowSolo + 2 * b] > 0.5f;
            p.bandMute[b] = v[kLowMute + 2 * b] > 0.5f;
        }
        static const int kBits[3] = {0, 16, 24};
        p.dither = kBits[std::clamp(static_cast<int>(std::lround(v[kDither])), 0, 2)];
        p.bypass = v[kBypass] > 0.5f;
        p.match = v[kMatch] > 0.5f;
        chain_.setParams(p);
    }

    /** Peak meters fall 20 dB/s and the spectrum 40; gain-reduction meters recover in about 150 ms. */
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
        out[kMeterMatch - kInputParamCount] = m.matchDb.load(std::memory_order_relaxed);
        out[kMeterCorr - kInputParamCount] = m.correlation.load(std::memory_order_relaxed);
        for (int b = 0; b < kSpectrumBands; ++b) {
            float& held = out[kSpectrum - kInputParamCount + b];
            held = std::max(m.spectrum[b].exchange(-100.0f, std::memory_order_relaxed), std::max(held - 2.0f * peakFall, -100.0f));
        }
        publish(frames);
    }

    /** The meters and a few of the knobs, for another HNL plugin to read (common/Link.hpp). Only stores numbers. */
    void publish(uint32_t frames) {
        if (link_ == nullptr) return;
        using namespace hnl::link;
        static_assert(kMasterInR == kMasterInL + 1 && kMasterOutL == kMasterInL + 2 && kMasterOutR == kMasterInL + 3 && kMasterTruePeak == kMasterInL + 4 &&
                          kMasterGrLimiter == kMasterGrLow + 3 && kMasterGrLow == kMasterInL + 5 && kMeterGrLim == kMeterInL + 8,
                      "peaks, true peak and gain reductions stand in the same order in both lists");
        for (int i = 0; i < 9; ++i) link_->set(kMasterInL + i, meters_[i]);
        link_->set(kMasterLufsM, meters_[kMeterLufsM - kInputParamCount]);
        link_->set(kMasterLufsS, meters_[kMeterLufsS - kInputParamCount]);
        link_->set(kMasterLufsI, meters_[kMeterLufsI - kInputParamCount]);
        link_->set(kMasterMatchGain, meters_[kMeterMatch - kInputParamCount]);
        link_->set(kMasterInputGain, values_[kInputGain].load(std::memory_order_relaxed));
        link_->set(kMasterLimiterGain, values_[kLimGain].load(std::memory_order_relaxed));
        link_->set(kMasterLimiterCeiling, values_[kLimCeiling].load(std::memory_order_relaxed));
        link_->set(kMasterLimiterOn, values_[kLimOn].load(std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f);
        link_->set(kMasterBypass, values_[kBypass].load(std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f);
        link_->set(kMasterMatch, values_[kMatch].load(std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f);
        link_->set(kMasterSampleRate, static_cast<float>(sampleRate_));
        link_->advance(frames);
    }

    static float toDb(float lin) { return lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f; }

    hnl::MasterChain chain_;
    double sampleRate_ = 48000.0;
    std::atomic<float> values_[kInputParamCount];
    std::atomic<bool> dirty_{true}, resetMeters_{false};
    float meters_[kParamCount - kInputParamCount] = {};
    std::unique_ptr<hnl::link::Publisher> link_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterPlugin)
};

Plugin* createPlugin() { return new MasterPlugin(); }

END_NAMESPACE_DISTRHO
