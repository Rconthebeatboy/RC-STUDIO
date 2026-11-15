#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace rcstudio {

class BiquadFilter {
public:
    enum class Type {
        LowPass,
        HighPass,
        BandPass,
        LowShelf,
        Peak,
        HighShelf
    };

    void setParams(Type type, float freqHz, float q, float gainDb, float sampleRate);
    void reset();
    inline void processStereo(float& l, float& r) noexcept {
        float outL = b0_ * l + b1_ * x1L_ + b2_ * x2L_ - a1_ * y1L_ - a2_ * y2L_;
        x2L_ = x1L_; x1L_ = l; y2L_ = y1L_; y1L_ = outL;
        float outR = b0_ * r + b1_ * x1R_ + b2_ * x2R_ - a1_ * y1R_ - a2_ * y2R_;
        x2R_ = x1R_; x1R_ = r; y2R_ = y1R_; y1R_ = outR;
        l = outL;
        r = outR;
    }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1L_ = 0.0f, x2L_ = 0.0f, y1L_ = 0.0f, y2L_ = 0.0f;
    float x1R_ = 0.0f, x2R_ = 0.0f, y1R_ = 0.0f, y2R_ = 0.0f;
};

class ParametricEQ3Band {
public:
    bool enabled = false;
    float lowGainDb = 0.0f;
    float midFreqHz = 1000.0f;
    float midGainDb = 0.0f;
    float highGainDb = 0.0f;

    void prepare(float sampleRate);
    void updateCoefficients();
    void processStereo(float& l, float& r) noexcept;

private:
    float sampleRate_ = 48000.0f;
    BiquadFilter low_;
    BiquadFilter mid_;
    BiquadFilter high_;
};

class Compressor {
public:
    bool enabled = false;
    float thresholdDb = -16.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float makeupDb = 0.0f;

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;

private:
    float sampleRate_ = 48000.0f;
    float env_ = 0.0f;
};

class Limiter {
public:
    bool enabled = true;
    float ceilingDb = -0.3f;

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;

private:
    float sampleRate_ = 48000.0f;
    float env_ = 0.0f;
};

class WaveshaperDistortion {
public:
    bool enabled = false;
    float drive = 1.0f; // 1.0 to 20.0
    float mix = 0.5f;   // 0.0 to 1.0

    void processStereo(float& l, float& r) noexcept;
};

class StereoDelay {
public:
    bool enabled = false;
    float delayMs = 375.0f;
    float feedback = 0.35f;
    float mix = 0.25f;

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;

private:
    float sampleRate_ = 48000.0f;
    std::vector<float> bufL_;
    std::vector<float> bufR_;
    size_t writeIdx_ = 0;
    float lpStateL_ = 0.0f;
    float lpStateR_ = 0.0f;
};

class FreeverbReverb {
public:
    bool enabled = false;
    float roomSize = 0.6f; // 0.0..1.0
    float damping = 0.4f;  // 0.0..1.0
    float mix = 0.22f;     // 0.0..1.0

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;

private:
    static constexpr int kNumCombs = 4;
    static constexpr int kNumAllpass = 2;

    struct Comb {
        std::vector<float> buf;
        size_t idx = 0;
        float filterStore = 0.0f;
        float process(float input, float feedback, float damp) noexcept;
    };

    struct Allpass {
        std::vector<float> buf;
        size_t idx = 0;
        float process(float input) noexcept;
    };

    std::array<Comb, kNumCombs> combsL_;
    std::array<Comb, kNumCombs> combsR_;
    std::array<Allpass, kNumAllpass> allpassL_;
    std::array<Allpass, kNumAllpass> allpassR_;
};

class ChorusFlanger {
public:
    bool enabled = false;
    float rateHz = 0.8f;
    float depthMs = 3.5f;
    float mix = 0.3f;
    bool flangerMode = false;

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;

private:
    float sampleRate_ = 48000.0f;
    std::vector<float> bufL_;
    std::vector<float> bufR_;
    size_t writeIdx_ = 0;
    float lfoPhase_ = 0.0f;
};

class StereoWidener {
public:
    bool enabled = false;
    float width = 1.2f; // 0.0 (mono) .. 2.0 (wide)

    void processStereo(float& l, float& r) noexcept;
};

class AutoPan {
public:
    bool enabled = false;
    float rateHz = 0.5f;
    float depth = 0.6f;

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;

private:
    float sampleRate_ = 48000.0f;
    float phase_ = 0.0f;
};

struct TrackEffectChain {
    ParametricEQ3Band eq;
    Compressor comp;
    WaveshaperDistortion distortion;
    ChorusFlanger chorus;
    StereoDelay delay;
    FreeverbReverb reverb;
    StereoWidener widener;
    AutoPan autoPan;
    Limiter limiter;

    void prepare(float sampleRate);
    void processStereo(float& l, float& r) noexcept;
};

} // namespace rcstudio
