// HNL Digital Piano: plugin side. Wraps hnl::Piano for DPF and feeds it MIDI.
#include <atomic>
#include <cstring>

#include "DistrhoPlugin.hpp"

#include "dsp/Piano.hpp"
#include "Params.hpp"

START_NAMESPACE_DISTRHO

class PianoPlugin : public Plugin {
public:
    PianoPlugin() : Plugin(kParamCount, 0, 1) {
        for (uint32_t i = 0; i < kInputParamCount; ++i) values_[i].store(kParams[i].def);
        for (uint32_t i = kInputParamCount; i < kParamCount; ++i) meters_[i - kInputParamCount] = kParams[i].def;
        prepare(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "HNLDigitalPiano"; }
    const char* getDescription() const override {
        return "Digital pianos in stereo: workstation, FM electric, 1980s stage, 1990s dance, and a concert grand.";
    }
    const char* getMaker() const override { return "HNL Design"; }
    const char* getHomePage() const override { return "https://hnldesign.nl"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(0, 8, 1); }

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
        values_[index].store(value, std::memory_order_relaxed);
        dirty_.store(true, std::memory_order_release);
    }

    void sampleRateChanged(double newSampleRate) override { prepare(newSampleRate); }

    void activate() override {
        pushParams();
        piano_.reset();
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* events, uint32_t eventCount) override {
        if (dirty_.exchange(false, std::memory_order_acquire)) pushParams();

        // Render up to each event, then apply it, so notes start on their exact sample.
        uint32_t done = 0;
        for (uint32_t e = 0; e < eventCount; ++e) {
            const MidiEvent& ev = events[e];
            const uint32_t at = ev.frame < frames ? ev.frame : frames;
            if (at > done) {
                piano_.process(outputs[0] + done, outputs[1] + done, static_cast<int>(at - done));
                done = at;
            }
            if (ev.size >= 1 && ev.size <= MidiEvent::kDataSize) handle(ev.data, ev.size);
        }
        if (frames > done) piano_.process(outputs[0] + done, outputs[1] + done, static_cast<int>(frames - done));

        // Output peak meters fall 20 dB/s.
        const float fall = static_cast<float>(20.0 * frames / sampleRate_);
        for (int c = 0; c < 2; ++c) {
            const float lin = piano_.meters.outPeak[c].exchange(0.0f, std::memory_order_relaxed);
            const float db = lin > 1e-5f ? 20.0f * std::log10(lin) : -100.0f;
            float& held = meters_[kMeterOutL - kInputParamCount + c];
            held = std::max(db, std::max(held - fall, -100.0f));
        }
        meters_[kMeterVoices - kInputParamCount] = piano_.meters.voices.load(std::memory_order_relaxed);
    }

private:
    void handle(const uint8_t* data, uint32_t size) {
        const uint8_t status = data[0] & 0xf0;
        if (status == 0x90 && size >= 3) piano_.noteOn(data[1] & 127, data[2] & 127);
        else if (status == 0x80 && size >= 3) piano_.noteOff(data[1] & 127);
        else if (status == 0xb0 && size >= 3) piano_.controller(data[1] & 127, data[2] & 127);
        else if (status == 0xe0 && size >= 3) piano_.pitchBend((((data[2] & 127) << 7 | (data[1] & 127)) - 8192) / 8192.0);
    }

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        piano_.prepare(sampleRate_);
        pushParams();
        piano_.reset();
    }

    void pushParams() {
        float v[kInputParamCount];
        for (uint32_t i = 0; i < kInputParamCount; ++i) v[i] = values_[i].load(std::memory_order_relaxed);
        hnl::PianoParams p;
        p.volume = v[kVolume];
        p.tone = v[kTone];
        p.velocity = v[kVelocity];
        p.decay = v[kDecay];
        p.release = v[kRelease];
        p.detune = v[kDetune];
        p.width = v[kWidth];
        p.reverb = v[kReverb];
        p.tune = v[kTune];
        p.octave = static_cast<int>(std::lround(v[kOctave])) - 2;
        p.voice = static_cast<int>(std::lround(v[kVoice]));
        piano_.setParams(p);
    }

    hnl::Piano piano_;
    double sampleRate_ = 48000.0;
    std::atomic<float> values_[kInputParamCount];
    std::atomic<bool> dirty_{true};
    float meters_[kParamCount - kInputParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoPlugin)
};

Plugin* createPlugin() { return new PianoPlugin(); }

END_NAMESPACE_DISTRHO
