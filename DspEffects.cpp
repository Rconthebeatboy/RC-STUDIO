#include "DspEffects.h"

namespace rcstudio {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

inline float gainToDb(float gain) {
    return 20.0f * std::log10(std::max(gain, 1e-6f));
}
} // namespace

void BiquadFilter::setParams(Type type, float freqHz, float q, float gainDb, float sampleRate) {
    freqHz = std::clamp(freqHz, 20.0f, sampleRate * 0.45f);
    q = std::max(q, 0.1f);

    const float w0 = kTwoPi * freqHz / sampleRate;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * q);
    const float A = std::pow(10.0f, gainDb / 40.0f);

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (type) {
        case Type::LowPass:
            b0 = (1.0f - cosW0) * 0.5f;
            b1 = 1.0f - cosW0;
            b2 = (1.0f - cosW0) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
        case Type::HighPass:
            b0 = (1.0f + cosW0) * 0.5f;
            b1 = -(1.0f + cosW0);
            b2 = (1.0f + cosW0) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
        case Type::BandPass:
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha;
            break;
        case Type::LowShelf: {
            const float sqrtA = std::sqrt(A);
            const float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAAlpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAAlpha);
            a0 = (A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAAlpha;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0);
            a2 = (A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAAlpha;
            break;
        }
        case Type::HighShelf: {
            const float sqrtA = std::sqrt(A);
            const float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 + twoSqrtAAlpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 - twoSqrtAAlpha);
            a0 = (A + 1.0f) - (A - 1.0f) * cosW0 + twoSqrtAAlpha;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0);
            a2 = (A + 1.0f) - (A - 1.0f) * cosW0 - twoSqrtAAlpha;
            break;
        }
        case Type::Peak:
            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cosW0;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cosW0;
            a2 = 1.0f - alpha / A;
            break;
    }

    const float invA0 = 1.0f / a0;
    b0_ = b0 * invA0;
    b1_ = b1 * invA0;
    b2_ = b2 * invA0;
    a1_ = a1 * invA0;
    a2_ = a2 * invA0;
}

void BiquadFilter::reset() {
    x1L_ = x2L_ = y1L_ = y2L_ = 0.0f;
    x1R_ = x2R_ = y1R_ = y2R_ = 0.0f;
}

void ParametricEQ3Band::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    low_.reset();
    mid_.reset();
    high_.reset();
    updateCoefficients();
}

void ParametricEQ3Band::updateCoefficients() {
    low_.setParams(BiquadFilter::Type::LowShelf, 120.0f, 0.707f, lowGainDb, sampleRate_);
    mid_.setParams(BiquadFilter::Type::Peak, midFreqHz, 1.2f, midGainDb, sampleRate_);
    high_.setParams(BiquadFilter::Type::HighShelf, 6500.0f, 0.707f, highGainDb, sampleRate_);
}

void ParametricEQ3Band::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    low_.processStereo(l, r);
    mid_.processStereo(l, r);
    high_.processStereo(l, r);
}

void Compressor::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    env_ = 0.0f;
}

void Compressor::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    const float peak = std::max(std::abs(l), std::abs(r));
    const float atkCoeff = std::exp(-1.0f / (std::max(attackMs, 0.5f) * 0.001f * sampleRate_));
    const float relCoeff = std::exp(-1.0f / (std::max(releaseMs, 5.0f) * 0.001f * sampleRate_));

    if (peak > env_) {
        env_ = atkCoeff * env_ + (1.0f - atkCoeff) * peak;
    } else {
        env_ = relCoeff * env_ + (1.0f - relCoeff) * peak;
    }

    const float envDb = gainToDb(env_);
    float gainReductionDb = 0.0f;
    if (envDb > thresholdDb && ratio > 1.0f) {
        gainReductionDb = (envDb - thresholdDb) * (1.0f - 1.0f / ratio);
    }

    const float totalGain = dbToGain(makeupDb - gainReductionDb);
    l *= totalGain;
    r *= totalGain;
}

void Limiter::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    env_ = 0.0f;
}

void Limiter::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    const float ceiling = dbToGain(ceilingDb);
    const float peak = std::max(std::abs(l), std::abs(r));
    const float relCoeff = std::exp(-1.0f / (0.05f * sampleRate_));
    env_ = std::max(peak, env_ * relCoeff);
    if (env_ > ceiling) {
        const float gain = ceiling / env_;
        l *= gain;
        r *= gain;
    }
    l = std::clamp(l, -ceiling, ceiling);
    r = std::clamp(r, -ceiling, ceiling);
}

void WaveshaperDistortion::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    const float norm = 1.0f / std::tanh(std::max(drive, 0.1f));
    const float wetL = std::tanh(l * drive) * norm;
    const float wetR = std::tanh(r * drive) * norm;
    l = l * (1.0f - mix) + wetL * mix;
    r = r * (1.0f - mix) + wetR * mix;
}

void StereoDelay::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    const size_t maxSamples = static_cast<size_t>(sampleRate * 2.0f) + 1;
    bufL_.assign(maxSamples, 0.0f);
    bufR_.assign(maxSamples, 0.0f);
    writeIdx_ = 0;
    lpStateL_ = lpStateR_ = 0.0f;
}

void StereoDelay::processStereo(float& l, float& r) noexcept {
    if (!enabled || bufL_.empty()) return;
    const size_t delaySamples = std::clamp(
        static_cast<size_t>(delayMs * 0.001f * sampleRate_),
        size_t{1},
        bufL_.size() - 1
    );
    const size_t readIdx = (writeIdx_ + bufL_.size() - delaySamples) % bufL_.size();

    const float delayedL = bufL_[readIdx];
    const float delayedR = bufR_[readIdx];

    // Gentle high-frequency damping in feedback path
    lpStateL_ = 0.65f * delayedL + 0.35f * lpStateL_;
    lpStateR_ = 0.65f * delayedR + 0.35f * lpStateR_;

    const float fb = std::clamp(feedback, 0.0f, 0.92f);
    // Ping-pong cross feedback
    bufL_[writeIdx_] = l + lpStateR_ * fb;
    bufR_[writeIdx_] = r + lpStateL_ * fb;

    writeIdx_ = (writeIdx_ + 1) % bufL_.size();

    l = l * (1.0f - mix) + delayedL * mix;
    r = r * (1.0f - mix) + delayedR * mix;
}

float FreeverbReverb::Comb::process(float input, float feedback, float damp) noexcept {
    if (buf.empty()) return input;
    const float output = buf[idx];
    filterStore = output * (1.0f - damp) + filterStore * damp;
    buf[idx] = input + filterStore * feedback;
    idx = (idx + 1) % buf.size();
    return output;
}

float FreeverbReverb::Allpass::process(float input) noexcept {
    if (buf.empty()) return input;
    const float bufOut = buf[idx];
    const float output = -input + bufOut;
    buf[idx] = input + bufOut * 0.5f;
    idx = (idx + 1) % buf.size();
    return output;
}

void FreeverbReverb::prepare(float sampleRate) {
    const float scale = sampleRate / 44100.0f;
    constexpr int combLengths[kNumCombs] = {1116, 1188, 1277, 1356};
    constexpr int allpassLengths[kNumAllpass] = {556, 441};

    for (int i = 0; i < kNumCombs; ++i) {
        size_t lenL = static_cast<size_t>(combLengths[i] * scale);
        size_t lenR = static_cast<size_t>((combLengths[i] + 23) * scale);
        combsL_[i].buf.assign(std::max<size_t>(lenL, 64), 0.0f);
        combsL_[i].idx = 0;
        combsL_[i].filterStore = 0.0f;
        combsR_[i].buf.assign(std::max<size_t>(lenR, 64), 0.0f);
        combsR_[i].idx = 0;
        combsR_[i].filterStore = 0.0f;
    }

    for (int i = 0; i < kNumAllpass; ++i) {
        size_t lenL = static_cast<size_t>(allpassLengths[i] * scale);
        size_t lenR = static_cast<size_t>((allpassLengths[i] + 23) * scale);
        allpassL_[i].buf.assign(std::max<size_t>(lenL, 32), 0.0f);
        allpassL_[i].idx = 0;
        allpassR_[i].buf.assign(std::max<size_t>(lenR, 32), 0.0f);
        allpassR_[i].idx = 0;
    }
}

void FreeverbReverb::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    const float monoIn = (l + r) * 0.015f;
    const float fb = 0.70f + std::clamp(roomSize, 0.0f, 1.0f) * 0.27f;
    const float damp = std::clamp(damping, 0.0f, 0.9f) * 0.45f;

    float outL = 0.0f;
    float outR = 0.0f;
    for (int i = 0; i < kNumCombs; ++i) {
        outL += combsL_[i].process(monoIn, fb, damp);
        outR += combsR_[i].process(monoIn, fb, damp);
    }
    for (int i = 0; i < kNumAllpass; ++i) {
        outL = allpassL_[i].process(outL);
        outR = allpassR_[i].process(outR);
    }

    l = l * (1.0f - mix) + outL * mix;
    r = r * (1.0f - mix) + outR * mix;
}

void ChorusFlanger::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    const size_t maxBuf = static_cast<size_t>(sampleRate * 0.05f) + 8;
    bufL_.assign(maxBuf, 0.0f);
    bufR_.assign(maxBuf, 0.0f);
    writeIdx_ = 0;
    lfoPhase_ = 0.0f;
}

void ChorusFlanger::processStereo(float& l, float& r) noexcept {
    if (!enabled || bufL_.empty()) return;
    bufL_[writeIdx_] = l;
    bufR_[writeIdx_] = r;

    const float lfoL = 0.5f * (1.0f + std::sin(kTwoPi * lfoPhase_));
    const float lfoR = 0.5f * (1.0f + std::sin(kTwoPi * (lfoPhase_ + 0.25f)));
    const float baseDelayMs = flangerMode ? 1.5f : 12.0f;

    const float dL = (baseDelayMs + lfoL * depthMs) * 0.001f * sampleRate_;
    const float dR = (baseDelayMs + lfoR * depthMs) * 0.001f * sampleRate_;

    auto readFrac = [&](const std::vector<float>& buf, float delaySamples) -> float {
        const size_t len = buf.size();
        const float readPos = static_cast<float>(writeIdx_ + len) - delaySamples;
        const size_t i0 = static_cast<size_t>(readPos) % len;
        const size_t i1 = (i0 + 1) % len;
        const float frac = readPos - std::floor(readPos);
        return buf[i0] * (1.0f - frac) + buf[i1] * frac;
    };

    const float wetL = readFrac(bufL_, dL);
    const float wetR = readFrac(bufR_, dR);

    l = l * (1.0f - mix) + wetL * mix;
    r = r * (1.0f - mix) + wetR * mix;

    writeIdx_ = (writeIdx_ + 1) % bufL_.size();
    lfoPhase_ += rateHz / sampleRate_;
    if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
}

void StereoWidener::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    const float mid = (l + r) * 0.5f;
    const float side = (l - r) * 0.5f * width;
    l = mid + side;
    r = mid - side;
}

void AutoPan::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    phase_ = 0.0f;
}

void AutoPan::processStereo(float& l, float& r) noexcept {
    if (!enabled) return;
    const float pan = std::sin(kTwoPi * phase_) * depth; // -1..1
    const float gainL = std::sqrt(0.5f * (1.0f - pan));
    const float gainR = std::sqrt(0.5f * (1.0f + pan));
    l *= gainL;
    r *= gainR;
    phase_ += rateHz / sampleRate_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
}

void TrackEffectChain::prepare(float sampleRate) {
    eq.prepare(sampleRate);
    comp.prepare(sampleRate);
    chorus.prepare(sampleRate);
    delay.prepare(sampleRate);
    reverb.prepare(sampleRate);
    autoPan.prepare(sampleRate);
    limiter.prepare(sampleRate);
}

void TrackEffectChain::processStereo(float& l, float& r) noexcept {
    eq.processStereo(l, r);
    comp.processStereo(l, r);
    distortion.processStereo(l, r);
    chorus.processStereo(l, r);
    delay.processStereo(l, r);
    reverb.processStereo(l, r);
    widener.processStereo(l, r);
    autoPan.processStereo(l, r);
    limiter.processStereo(l, r);
}

} // namespace rcstudio
