#include "SynthEngine.h"
#include <algorithm>
#include <cmath>

namespace rcstudio {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

inline float midiToFreq(int midiNote) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}
} // namespace

void PolySynth::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    lfoPhase_ = 0.0f;
    for (auto& v : voices_) {
        v.active = false;
        v.stage = EnvStage::Off;
        v.filter.reset();
        v.ksBuffer.assign(static_cast<size_t>(sampleRate / 40.0f) + 8, 0.0f);
    }
}

void PolySynth::setInstrument(InstrumentType type) {
    instrument_ = type;
    switch (type) {
        case InstrumentType::GrandPiano:
            params_ = {Waveform::Triangle, Waveform::Sine, 2.0f, 0.35f, 0.1f,
                       0.005f, 0.65f, 0.15f, 0.45f, 5200.0f, 0.8f, 0.0f, 0.0f, 1.0f};
            break;
        case InstrumentType::ElectricPiano:
            params_ = {Waveform::Sine, Waveform::Triangle, 4.0f, 0.3f, 0.0f,
                       0.008f, 0.8f, 0.25f, 0.5f, 3800.0f, 0.9f, 3.5f, 0.12f, 1.1f};
            break;
        case InstrumentType::Bass808:
            params_ = {Waveform::Sine, Waveform::Triangle, 0.0f, 0.2f, 0.5f,
                       0.002f, 0.55f, 0.35f, 0.32f, 950.0f, 1.1f, 0.0f, 0.0f, 2.8f};
            break;
        case InstrumentType::SynthBass:
            params_ = {Waveform::Saw, Waveform::Square, 5.0f, 0.5f, 0.45f,
                       0.004f, 0.22f, 0.65f, 0.18f, 1600.0f, 2.2f, 0.0f, 0.0f, 1.8f};
            break;
        case InstrumentType::Strings:
        case InstrumentType::Violin:
            params_ = {Waveform::Saw, Waveform::Saw, 12.0f, 0.5f, 0.1f,
                       0.18f, 0.4f, 0.85f, 0.55f, 4500.0f, 0.9f, 4.8f, 0.08f, 1.0f};
            break;
        case InstrumentType::Flute:
            params_ = {Waveform::Sine, Waveform::Triangle, 0.0f, 0.15f, 0.0f,
                       0.06f, 0.2f, 0.85f, 0.22f, 3200.0f, 0.7f, 5.2f, 0.06f, 1.0f};
            break;
        case InstrumentType::AtmosphericPad:
            params_ = {Waveform::Saw, Waveform::Triangle, 14.0f, 0.55f, 0.25f,
                       0.45f, 0.8f, 0.8f, 1.1f, 2400.0f, 1.4f, 0.6f, 0.2f, 1.0f};
            break;
        default:
            break;
    }
}

void PolySynth::setParams(const SynthParams& params) {
    params_ = params;
}

float PolySynth::nextNoise(uint32_t& state) noexcept {
    state = state * 1664525u + 1013904223u;
    return (static_cast<float>(state >> 8) / 8388608.0f) - 1.0f;
}

void PolySynth::noteOn(int midiNote, float velocity) {
    Voice* chosen = nullptr;
    for (auto& v : voices_) {
        if (!v.active) {
            chosen = &v;
            break;
        }
    }
    if (!chosen) {
        chosen = &voices_[0];
    }

    chosen->active = true;
    chosen->midiNote = midiNote;
    chosen->velocity = std::clamp(velocity, 0.05f, 1.0f);
    chosen->freqHz = midiToFreq(midiNote);
    chosen->phase1 = 0.0f;
    chosen->phase2 = 0.0f;
    chosen->subPhase = 0.0f;
    chosen->pitchEnv = 1.0f;
    chosen->envLevel = 0.0f;
    chosen->stage = EnvStage::Attack;
    chosen->ageSamples = 0;
    chosen->filter.reset();
    chosen->filter.setParams(
        BiquadFilter::Type::LowPass,
        params_.filterCutoffHz,
        params_.filterResonance,
        0.0f,
        sampleRate_
    );

    if (instrument_ == InstrumentType::Sitar || instrument_ == InstrumentType::Guitar) {
        size_t delayLen = static_cast<size_t>(std::clamp(sampleRate_ / chosen->freqHz, 8.0f, static_cast<float>(chosen->ksBuffer.size() - 1)));
        if (chosen->ksBuffer.size() < delayLen) chosen->ksBuffer.resize(delayLen);
        for (size_t i = 0; i < delayLen; ++i) {
            chosen->ksBuffer[i] = nextNoise(chosen->rngState) * chosen->velocity;
        }
        chosen->ksIdx = 0;
        chosen->ksPrev = 0.0f;
    }
}

void PolySynth::noteOff(int midiNote) {
    for (auto& v : voices_) {
        if (v.active && v.midiNote == midiNote && v.stage != EnvStage::Release) {
            v.stage = EnvStage::Release;
        }
    }
}

void PolySynth::allNotesOff() {
    for (auto& v : voices_) {
        v.active = false;
        v.stage = EnvStage::Off;
    }
}

float PolySynth::evalOsc(Waveform w, float phase) noexcept {
    switch (w) {
        case Waveform::Sine:
            return std::sin(kTwoPi * phase);
        case Waveform::Triangle:
            return 1.0f - 4.0f * std::abs(std::round(phase - 0.25f) - (phase - 0.25f));
        case Waveform::Saw:
            return 2.0f * (phase - std::floor(phase + 0.5f));
        case Waveform::Square:
            return phase < 0.5f ? 1.0f : -1.0f;
    }
    return 0.0f;
}

void PolySynth::renderDrumNote(Voice& v, float& l, float& r) noexcept {
    const float t = static_cast<float>(v.ageSamples) / sampleRate_;
    const int note = v.midiNote % 12; // Map GM/pad notes cleanly
    float sample = 0.0f;

    if (v.midiNote == 36 || note == 0) {
        // Punchy Trap/Phonk Kick
        const float freq = 48.0f + 130.0f * std::exp(-t * 38.0f);
        v.phase1 += freq / sampleRate_;
        if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
        const float amp = std::exp(-t * 9.5f);
        sample = std::tanh(std::sin(kTwoPi * v.phase1) * 1.8f) * amp;
        if (amp < 0.001f) v.active = false;
    } else if (v.midiNote == 38 || v.midiNote == 40 || note == 2) {
        // Snare: body + snappy noise
        v.phase1 += 185.0f / sampleRate_;
        if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
        const float body = std::sin(kTwoPi * v.phase1) * std::exp(-t * 22.0f);
        const float noise = nextNoise(v.rngState) * std::exp(-t * 15.0f);
        sample = body * 0.45f + noise * 0.65f;
        if (t > 0.35f) v.active = false;
    } else if (v.midiNote == 42 || note == 6) {
        // Closed Hi-Hat
        sample = nextNoise(v.rngState) * std::exp(-t * 55.0f) * 0.5f;
        if (t > 0.12f) v.active = false;
    } else if (v.midiNote == 46 || note == 10) {
        // Open Hi-Hat
        sample = nextNoise(v.rngState) * std::exp(-t * 9.0f) * 0.55f;
        if (t > 0.5f) v.active = false;
    } else if (v.midiNote == 39 || note == 3) {
        // Trap Clap (multi-burst noise)
        const float burst = (t < 0.03f) ? std::abs(std::sin(t * 400.0f)) : 1.0f;
        sample = nextNoise(v.rngState) * burst * std::exp(-t * 14.0f) * 0.7f;
        if (t > 0.35f) v.active = false;
    } else {
        // Phonk Cowbell / Perc (540 Hz + 800 Hz metallic)
        v.phase1 += 540.0f / sampleRate_;
        v.phase2 += 800.0f / sampleRate_;
        if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
        if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
        const float raw = evalOsc(Waveform::Square, v.phase1) + evalOsc(Waveform::Square, v.phase2);
        sample = std::tanh(raw * 0.6f) * std::exp(-t * 12.0f) * 0.5f;
        if (t > 0.35f) v.active = false;
    }

    sample *= v.velocity;
    l += sample;
    r += sample;
}

void PolySynth::renderTablaNote(Voice& v, float& l, float& r) noexcept {
    const float t = static_cast<float>(v.ageSamples) / sampleRate_;
    const bool isBayan = (v.midiNote < 60);
    const float f0 = isBayan ? 95.0f + 55.0f * std::exp(-t * 18.0f) : v.freqHz;
    v.phase1 += f0 / sampleRate_;
    v.phase2 += (f0 * (isBayan ? 1.5f : 2.0f)) / sampleRate_;
    if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

    const float env = std::exp(-t * (isBayan ? 5.5f : 8.5f));
    const float transient = nextNoise(v.rngState) * std::exp(-t * 90.0f) * 0.35f;
    const float body = std::sin(kTwoPi * v.phase1) * 0.7f + std::sin(kTwoPi * v.phase2) * 0.25f;
    const float sample = (body * env + transient) * v.velocity;
    if (env < 0.001f) v.active = false;
    l += sample;
    r += sample;
}

void PolySynth::renderPluckNote(Voice& v, float& l, float& r, bool sitarBuzz) noexcept {
    const size_t delayLen = std::clamp(static_cast<size_t>(sampleRate_ / v.freqHz), size_t{8}, v.ksBuffer.size() - 1);
    const size_t idx = v.ksIdx % delayLen;
    const float current = v.ksBuffer[idx];
    const float nextVal = 0.994f * 0.5f * (current + v.ksPrev);
    v.ksBuffer[idx] = nextVal;
    v.ksPrev = current;
    v.ksIdx = (idx + 1) % delayLen;

    float sample = current;
    if (sitarBuzz) {
        // Jawari bridge harmonic shimmer
        sample = std::tanh(sample * 2.2f) * 0.65f;
    }

    if (v.stage == EnvStage::Release) {
        v.envLevel -= 1.0f / (std::max(params_.releaseSec, 0.05f) * sampleRate_);
        if (v.envLevel <= 0.0f) {
            v.active = false;
            return;
        }
    } else {
        v.envLevel = 1.0f;
    }

    sample *= v.envLevel;
    l += sample;
    r += sample;
}

void PolySynth::renderStereo(float* outL, float* outR, int numFrames) noexcept {
    const float detuneRatio = std::pow(2.0f, params_.osc2DetuneCents / 1200.0f);
    const float atkRate = 1.0f / (std::max(params_.attackSec, 0.001f) * sampleRate_);
    const float decRate = 1.0f / (std::max(params_.decaySec, 0.005f) * sampleRate_);
    const float relRate = 1.0f / (std::max(params_.releaseSec, 0.005f) * sampleRate_);

    for (int i = 0; i < numFrames; ++i) {
        float mixL = 0.0f;
        float mixR = 0.0f;

        const float lfo = std::sin(kTwoPi * lfoPhase_);
        lfoPhase_ += params_.lfoRateHz / sampleRate_;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;

        for (auto& v : voices_) {
            if (!v.active) continue;
            v.ageSamples++;

            if (instrument_ == InstrumentType::DrumKit || instrument_ == InstrumentType::AcousticDrums) {
                renderDrumNote(v, mixL, mixR);
                continue;
            }
            if (instrument_ == InstrumentType::Tabla) {
                renderTablaNote(v, mixL, mixR);
                continue;
            }
            if (instrument_ == InstrumentType::Sitar || instrument_ == InstrumentType::Guitar) {
                renderPluckNote(v, mixL, mixR, instrument_ == InstrumentType::Sitar);
                continue;
            }

            // Envelope
            switch (v.stage) {
                case EnvStage::Attack:
                    v.envLevel += atkRate;
                    if (v.envLevel >= 1.0f) {
                        v.envLevel = 1.0f;
                        v.stage = EnvStage::Decay;
                    }
                    break;
                case EnvStage::Decay:
                    v.envLevel -= decRate * (1.0f - params_.sustainLevel);
                    if (v.envLevel <= params_.sustainLevel) {
                        v.envLevel = params_.sustainLevel;
                        v.stage = EnvStage::Sustain;
                    }
                    break;
                case EnvStage::Sustain:
                    break;
                case EnvStage::Release:
                    v.envLevel -= relRate;
                    if (v.envLevel <= 0.0f) {
                        v.envLevel = 0.0f;
                        v.active = false;
                        v.stage = EnvStage::Off;
                    }
                    break;
                case EnvStage::Off:
                    v.active = false;
                    break;
            }
            if (!v.active) continue;

            float freq = v.freqHz * (1.0f + lfo * params_.lfoDepth * 0.02f);
            if (instrument_ == InstrumentType::Bass808) {
                // Classic 808 pitch drop transient
                v.pitchEnv *= std::exp(-1.0f / (0.045f * sampleRate_));
                freq *= (1.0f + 1.8f * v.pitchEnv);
            }

            v.phase1 += freq / sampleRate_;
            v.phase2 += (freq * detuneRatio) / sampleRate_;
            v.subPhase += (freq * 0.5f) / sampleRate_;
            if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
            if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
            if (v.subPhase >= 1.0f) v.subPhase -= 1.0f;

            const float osc1 = evalOsc(params_.osc1Wave, v.phase1);
            const float osc2 = evalOsc(params_.osc2Wave, v.phase2);
            const float sub = std::sin(kTwoPi * v.subPhase);
            float raw = osc1 * (1.0f - params_.oscMix) + osc2 * params_.oscMix + sub * params_.subOscMix;

            if (instrument_ == InstrumentType::Flute) {
                raw += nextNoise(v.rngState) * 0.07f;
            }

            if (params_.drive > 1.05f) {
                raw = std::tanh(raw * params_.drive);
            }

            float sL = raw * v.envLevel * v.velocity * 0.35f;
            float sR = sL;
            v.filter.processStereo(sL, sR);

            mixL += sL;
            mixR += sR;
        }

        outL[i] += mixL;
        outR[i] += mixR;
    }
}

} // namespace rcstudio
