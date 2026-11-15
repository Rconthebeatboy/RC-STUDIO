#pragma once

#include "DspEffects.h"
#include <array>
#include <cstdint>
#include <vector>

namespace rcstudio {

enum class Waveform : int {
    Sine = 0,
    Triangle = 1,
    Saw = 2,
    Square = 3
};

enum class InstrumentType : int {
    GrandPiano = 0,
    ElectricPiano = 1,
    Bass808 = 2,
    SynthBass = 3,
    AnalogSynth = 4,
    DrumKit = 5,
    AcousticDrums = 6,
    Guitar = 7,
    Strings = 8,
    Violin = 9,
    Flute = 10,
    Tabla = 11,
    Sitar = 12,
    AtmosphericPad = 13
};

struct SynthParams {
    Waveform osc1Wave = Waveform::Saw;
    Waveform osc2Wave = Waveform::Square;
    float osc2DetuneCents = 7.0f;
    float oscMix = 0.5f;
    float subOscMix = 0.25f;

    float attackSec = 0.01f;
    float decaySec = 0.25f;
    float sustainLevel = 0.7f;
    float releaseSec = 0.3f;

    float filterCutoffHz = 6000.0f;
    float filterResonance = 1.2f;
    float lfoRateHz = 2.5f;
    float lfoDepth = 0.0f;
    float drive = 1.0f;
};

class PolySynth {
public:
    static constexpr int kMaxVoices = 16;

    void prepare(float sampleRate);
    void setInstrument(InstrumentType type);
    void setParams(const SynthParams& params);
    const SynthParams& getParams() const { return params_; }

    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);
    void allNotesOff();

    void renderStereo(float* outL, float* outR, int numFrames) noexcept;

private:
    enum class EnvStage { Off, Attack, Decay, Sustain, Release };

    struct Voice {
        bool active = false;
        int midiNote = -1;
        float velocity = 0.0f;
        float freqHz = 440.0f;
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float subPhase = 0.0f;
        float pitchEnv = 0.0f;
        float envLevel = 0.0f;
        EnvStage stage = EnvStage::Off;
        BiquadFilter filter;

        // Karplus-Strong buffer for Sitar / Guitar
        std::vector<float> ksBuffer;
        size_t ksIdx = 0;
        float ksPrev = 0.0f;

        // Drum / Tabla transient state
        uint32_t ageSamples = 0;
        uint32_t rngState = 123456789u;
    };

    float sampleRate_ = 48000.0f;
    InstrumentType instrument_ = InstrumentType::AnalogSynth;
    SynthParams params_{};
    float lfoPhase_ = 0.0f;
    std::array<Voice, kMaxVoices> voices_{};

    float evalOsc(Waveform w, float phase) noexcept;
    float nextNoise(uint32_t& state) noexcept;
    void renderDrumNote(Voice& v, float& l, float& r) noexcept;
    void renderTablaNote(Voice& v, float& l, float& r) noexcept;
    void renderPluckNote(Voice& v, float& l, float& r, bool sitarBuzz) noexcept;
};

} // namespace rcstudio
