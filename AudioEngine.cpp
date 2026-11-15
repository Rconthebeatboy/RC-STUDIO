#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RCStudioAudio", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RCStudioAudio", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
#endif

namespace rcstudio {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

AudioEngine::AudioEngine() {
    masterLimiter_.prepare(static_cast<float>(sampleRate_));
    masterLimiter_.enabled = true;
    masterLimiter_.ceilingDb = -0.3f;

    scratchL_.resize(2048, 0.0f);
    scratchR_.resize(2048, 0.0f);
    mixL_.resize(2048, 0.0f);
    mixR_.resize(2048, 0.0f);

    // Default 6 drum lanes: Kick(36), Snare(38), Clap(39), ClosedHat(42), OpenHat(46), Perc(56)
    const int defaultNotes[6] = {36, 38, 39, 42, 46, 56};
    for (int i = 0; i < 6; ++i) {
        seqLanes_[i].midiNote = defaultNotes[i];
        seqLanes_[i].velocities.fill(0.0f);
    }
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start(int sampleRate) {
    sampleRate_ = sampleRate;
    masterLimiter_.prepare(static_cast<float>(sampleRate_));
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& t : tracks_) {
            t->synth.prepare(static_cast<float>(sampleRate_));
            t->fx.prepare(static_cast<float>(sampleRate_));
        }
    }

#ifdef __ANDROID__
    oboe::AudioStreamBuilder outBuilder;
    outBuilder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setSampleRate(sampleRate_)
        ->setDataCallback(this)
        ->setErrorCallback(this);

    oboe::Result res = outBuilder.openStream(outStream_);
    if (res != oboe::Result::OK) {
        LOGE("Failed to open Oboe output stream: %s", oboe::convertToText(res));
        return false;
    }
    sampleRate_ = outStream_->getSampleRate();
    outStream_->setBufferSizeInFrames(outStream_->getFramesPerBurst() * 2);
    res = outStream_->requestStart();
    if (res != oboe::Result::OK) {
        LOGE("Failed to start Oboe output stream: %s", oboe::convertToText(res));
        return false;
    }
    LOGI("Oboe output stream started at %d Hz", sampleRate_);
#endif
    return true;
}

void AudioEngine::stop() {
#ifdef __ANDROID__
    if (inStream_) {
        inStream_->stop();
        inStream_->close();
        inStream_.reset();
    }
    if (outStream_) {
        outStream_->stop();
        outStream_->close();
        outStream_.reset();
    }
#endif
}

void AudioEngine::setPlaying(bool playing) {
    cmdQueue_.push({EngineCommand::Type::SetPlayState, 0, playing ? 1 : 0, 0.0f, 0});
}

void AudioEngine::setRecording(bool recording) {
    if (recording && !recording_.load()) {
        std::lock_guard<std::mutex> lock(recMutex_);
        recordedStereo_.clear();
    }
    recording_.store(recording, std::memory_order_release);

#ifdef __ANDROID__
    if (recording && !inStream_) {
        oboe::AudioStreamBuilder inBuilder;
        inBuilder.setDirection(oboe::Direction::Input)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Mono)
            ->setSampleRate(sampleRate_)
            ->setInputPreset(oboe::InputPreset::Unprocessed);
        if (inBuilder.openStream(inStream_) == oboe::Result::OK) {
            inStream_->requestStart();
        }
    } else if (!recording && inStream_) {
        inStream_->stop();
        inStream_->close();
        inStream_.reset();
    }
#endif
}

void AudioEngine::setBpm(float bpm) {
    bpm = std::clamp(bpm, 40.0f, 240.0f);
    cmdQueue_.push({EngineCommand::Type::SetBpm, 0, 0, bpm, 0});
}

void AudioEngine::setLoopRange(bool enabled, int64_t startFrame, int64_t endFrame) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    loopEnabled_ = enabled;
    loopStartFrame_ = std::max<int64_t>(0, startFrame);
    loopEndFrame_ = std::max(loopStartFrame_ + sampleRate_ / 4, endFrame);
}

void AudioEngine::seekFrame(int64_t frame) {
    cmdQueue_.push({EngineCommand::Type::SeekFrame, 0, 0, 0.0f, std::max<int64_t>(0, frame)});
}

int AudioEngine::addTrack(const std::string& name, InstrumentType instrument) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto t = std::make_unique<TrackState>();
    t->id = nextTrackId_++;
    t->name = name;
    t->instrument = instrument;
    t->synth.prepare(static_cast<float>(sampleRate_));
    t->synth.setInstrument(instrument);
    t->fx.prepare(static_cast<float>(sampleRate_));
    int id = t->id;
    tracks_.push_back(std::move(t));
    return id;
}

void AudioEngine::removeTrack(int trackId) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
                       [trackId](const std::unique_ptr<TrackState>& t) { return t->id == trackId; }),
        tracks_.end()
    );
}

void AudioEngine::setTrackMix(int trackId, float volume, float pan, bool mute, bool solo, bool arm) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->volume = std::clamp(volume, 0.0f, 1.5f);
            t->pan = std::clamp(pan, -1.0f, 1.0f);
            t->muted = mute;
            t->solo = solo;
            t->armed = arm;
            break;
        }
    }
}

void AudioEngine::setTrackInstrument(int trackId, InstrumentType instrument) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->instrument = instrument;
            t->synth.setInstrument(instrument);
            break;
        }
    }
}

void AudioEngine::setTrackSynthParams(int trackId, const SynthParams& params) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->synth.setParams(params);
            break;
        }
    }
}

void AudioEngine::updateTrackFx(int trackId, const TrackEffectChain& fxTemplate) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->fx.eq.enabled = fxTemplate.eq.enabled;
            t->fx.eq.lowGainDb = fxTemplate.eq.lowGainDb;
            t->fx.eq.midFreqHz = fxTemplate.eq.midFreqHz;
            t->fx.eq.midGainDb = fxTemplate.eq.midGainDb;
            t->fx.eq.highGainDb = fxTemplate.eq.highGainDb;
            t->fx.eq.updateCoefficients();

            t->fx.comp.enabled = fxTemplate.comp.enabled;
            t->fx.comp.thresholdDb = fxTemplate.comp.thresholdDb;
            t->fx.comp.ratio = fxTemplate.comp.ratio;
            t->fx.comp.attackMs = fxTemplate.comp.attackMs;
            t->fx.comp.releaseMs = fxTemplate.comp.releaseMs;
            t->fx.comp.makeupDb = fxTemplate.comp.makeupDb;

            t->fx.distortion = fxTemplate.distortion;

            t->fx.chorus.enabled = fxTemplate.chorus.enabled;
            t->fx.chorus.rateHz = fxTemplate.chorus.rateHz;
            t->fx.chorus.depthMs = fxTemplate.chorus.depthMs;
            t->fx.chorus.mix = fxTemplate.chorus.mix;
            t->fx.chorus.flangerMode = fxTemplate.chorus.flangerMode;

            t->fx.delay.enabled = fxTemplate.delay.enabled;
            t->fx.delay.delayMs = fxTemplate.delay.delayMs;
            t->fx.delay.feedback = fxTemplate.delay.feedback;
            t->fx.delay.mix = fxTemplate.delay.mix;

            t->fx.reverb.enabled = fxTemplate.reverb.enabled;
            t->fx.reverb.roomSize = fxTemplate.reverb.roomSize;
            t->fx.reverb.damping = fxTemplate.reverb.damping;
            t->fx.reverb.mix = fxTemplate.reverb.mix;

            t->fx.widener = fxTemplate.widener;
            t->fx.autoPan.enabled = fxTemplate.autoPan.enabled;
            t->fx.autoPan.rateHz = fxTemplate.autoPan.rateHz;
            t->fx.autoPan.depth = fxTemplate.autoPan.depth;
            break;
        }
    }
}

void AudioEngine::addAudioClip(int trackId, const AudioClipData& clip) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->audioClips.push_back(clip);
            break;
        }
    }
}

void AudioEngine::clearAudioClips(int trackId) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->audioClips.clear();
            break;
        }
    }
}

void AudioEngine::setTrackMidiNotes(int trackId, const std::vector<MidiNoteEvent>& notes) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            t->midiNotes = notes;
            break;
        }
    }
}

void AudioEngine::configureStepSequencer(int steps, float swing, const std::array<StepSequencerTrack, 6>& lanes) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    seqSteps_ = (steps == 32) ? 32 : 16;
    seqSwing_ = std::clamp(swing, 0.0f, 0.5f);
    seqLanes_ = lanes;
}

void AudioEngine::sendLiveNoteOn(int trackId, int midiNote, float velocity) {
    cmdQueue_.push({EngineCommand::Type::NoteOn, trackId, midiNote, velocity, 0});
}

void AudioEngine::sendLiveNoteOff(int trackId, int midiNote) {
    cmdQueue_.push({EngineCommand::Type::NoteOff, trackId, midiNote, 0.0f, 0});
}

void AudioEngine::getTrackPeaks(int trackId, float& outL, float& outR) {
    std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        outL = outR = 0.0f;
        return;
    }
    for (auto& t : tracks_) {
        if (t->id == trackId) {
            outL = t->peakL.load(std::memory_order_relaxed);
            outR = t->peakR.load(std::memory_order_relaxed);
            return;
        }
    }
    outL = outR = 0.0f;
}

void AudioEngine::getSpectrum(float* outBands32) {
    for (int i = 0; i < kSpectrumBands; ++i) {
        outBands32[i] = spectrumBands_[i].load(std::memory_order_relaxed);
    }
}

std::vector<float> AudioEngine::consumeRecordedAudio() {
    std::lock_guard<std::mutex> lock(recMutex_);
    std::vector<float> copy = std::move(recordedStereo_);
    recordedStereo_.clear();
    return copy;
}

void AudioEngine::processCommands() {
    EngineCommand cmd;
    while (cmdQueue_.pop(cmd)) {
        switch (cmd.type) {
            case EngineCommand::Type::SetPlayState:
                playing_.store(cmd.arg1 != 0, std::memory_order_release);
                if (cmd.arg1 == 0) {
                    lastSeqStep_ = -1;
                    lastBeatIndex_ = -1;
                    for (auto& t : tracks_) t->synth.allNotesOff();
                }
                break;
            case EngineCommand::Type::SetBpm:
                bpm_.store(cmd.argF, std::memory_order_release);
                break;
            case EngineCommand::Type::SeekFrame:
                playheadFrame_.store(cmd.arg64, std::memory_order_release);
                lastSeqStep_ = -1;
                lastBeatIndex_ = -1;
                break;
            case EngineCommand::Type::NoteOn:
                for (auto& t : tracks_) {
                    if (t->id == cmd.trackId) {
                        t->synth.noteOn(cmd.arg1, cmd.argF);
                        break;
                    }
                }
                break;
            case EngineCommand::Type::NoteOff:
                for (auto& t : tracks_) {
                    if (t->id == cmd.trackId) {
                        t->synth.noteOff(cmd.arg1);
                        break;
                    }
                }
                break;
        }
    }
}

void AudioEngine::processSequencerAndMidi(int64_t blockStart, int numFrames) {
    const float bpm = bpm_.load(std::memory_order_relaxed);
    const double framesPerBeat = (60.0 / static_cast<double>(bpm)) * static_cast<double>(sampleRate_);
    const double framesPerStep = framesPerBeat / 4.0; // 16th notes
    const int64_t blockEnd = blockStart + numFrames;

    // Trigger MIDI piano roll notes
    for (auto& t : tracks_) {
        for (const auto& note : t->midiNotes) {
            if (note.startFrame >= blockStart && note.startFrame < blockEnd) {
                t->synth.noteOn(note.midiNote, note.velocity);
            }
            const int64_t noteEnd = note.startFrame + note.durationFrames;
            if (noteEnd >= blockStart && noteEnd < blockEnd) {
                t->synth.noteOff(note.midiNote);
            }
        }
    }

    // Trigger 16/32-step drum sequencer on DrumKit tracks
    if (stepSeqEnabled_.load(std::memory_order_relaxed)) {
        const int64_t currentStepTotal = static_cast<int64_t>(static_cast<double>(blockStart) / framesPerStep);
        const int stepIdx = static_cast<int>(currentStepTotal % seqSteps_);
        if (stepIdx != lastSeqStep_) {
            lastSeqStep_ = stepIdx;
            for (auto& t : tracks_) {
                if (t->instrument == InstrumentType::DrumKit || t->instrument == InstrumentType::AcousticDrums) {
                    for (const auto& lane : seqLanes_) {
                        float vel = lane.velocities[stepIdx];
                        if (vel > 0.01f) {
                            t->synth.noteOn(lane.midiNote, vel);
                        }
                    }
                }
            }
        }
    }

    // Metronome click
    if (metronomeEnabled_.load(std::memory_order_relaxed)) {
        const int64_t beatIdx = static_cast<int64_t>(static_cast<double>(blockStart) / framesPerBeat);
        if (beatIdx != lastBeatIndex_) {
            lastBeatIndex_ = beatIdx;
            metroClickRemain_ = static_cast<int>(0.025f * sampleRate_);
            metroPhase_ = 0.0f;
        }
    }
}

void AudioEngine::updateSpectrum(const float* monoSamples, int numFrames) {
    // Lightweight Goertzel-like 32-band logarithmic energy estimator
    const int n = std::min(numFrames, 256);
    if (n <= 0) return;

    for (int b = 0; b < kSpectrumBands; ++b) {
        const float freq = 40.0f * std::pow(2.0f, static_cast<float>(b) * 0.28f);
        if (freq >= sampleRate_ * 0.48f) {
            spectrumBands_[b].store(0.0f, std::memory_order_relaxed);
            continue;
        }
        const float w = kTwoPi * freq / static_cast<float>(sampleRate_);
        const float coeff = 2.0f * std::cos(w);
        float q1 = 0.0f, q2 = 0.0f;
        for (int i = 0; i < n; ++i) {
            float q0 = coeff * q1 - q2 + monoSamples[i];
            q2 = q1;
            q1 = q0;
        }
        const float mag = std::sqrt(std::max(0.0f, q1 * q1 + q2 * q2 - q1 * q2 * coeff)) / static_cast<float>(n);
        const float prev = spectrumBands_[b].load(std::memory_order_relaxed);
        const float smoothed = prev * 0.65f + std::clamp(mag * 4.0f, 0.0f, 1.0f) * 0.35f;
        spectrumBands_[b].store(smoothed, std::memory_order_relaxed);
    }
}

void AudioEngine::renderBlock(float* outInterleavedStereo, int numFrames, bool offlineMode) {
    if (static_cast<int>(mixL_.size()) < numFrames) {
        if (!offlineMode) return;
        mixL_.resize(numFrames);
        mixR_.resize(numFrames);
        scratchL_.resize(numFrames);
        scratchR_.resize(numFrames);
    }

    std::fill(mixL_.begin(), mixL_.begin() + numFrames, 0.0f);
    std::fill(mixR_.begin(), mixR_.begin() + numFrames, 0.0f);

    std::unique_lock<std::mutex> lock(stateMutex_, std::defer_lock);
    if (offlineMode) {
        lock.lock();
    } else {
        if (!lock.try_lock()) {
            std::memset(outInterleavedStereo, 0, sizeof(float) * numFrames * 2);
            return;
        }
    }

    processCommands();

    const bool isPlayingNow = playing_.load(std::memory_order_relaxed) || offlineMode;
    int64_t currentFrame = playheadFrame_.load(std::memory_order_relaxed);

    if (isPlayingNow) {
        processSequencerAndMidi(currentFrame, numFrames);
    }

    bool anySolo = false;
    for (const auto& t : tracks_) {
        if (t->solo) {
            anySolo = true;
            break;
        }
    }

    for (auto& t : tracks_) {
        if (t->muted || (anySolo && !t->solo)) {
            t->peakL.store(0.0f, std::memory_order_relaxed);
            t->peakR.store(0.0f, std::memory_order_relaxed);
            continue;
        }

        std::fill(scratchL_.begin(), scratchL_.begin() + numFrames, 0.0f);
        std::fill(scratchR_.begin(), scratchR_.begin() + numFrames, 0.0f);

        // Render synth voices (always active for live keyboard/pad audition + MIDI playback)
        t->synth.renderStereo(scratchL_.data(), scratchR_.data(), numFrames);

        // Render audio clips when playing
        if (isPlayingNow) {
            for (const auto& clip : t->audioClips) {
                if (!clip.stereoPcm || clip.stereoPcm->empty()) continue;
                const int64_t clipEnd = clip.startFrame + clip.lengthFrames;
                if (currentFrame + numFrames <= clip.startFrame || currentFrame >= clipEnd) continue;

                const float pitchRate = std::pow(2.0f, clip.pitchSemitones / 12.0f);
                const size_t totalSrcFrames = clip.stereoPcm->size() / 2;

                for (int i = 0; i < numFrames; ++i) {
                    const int64_t timelineFrame = currentFrame + i;
                    if (timelineFrame < clip.startFrame || timelineFrame >= clipEnd) continue;

                    const double srcPos = static_cast<double>(clip.offsetFrame) +
                                          static_cast<double>(timelineFrame - clip.startFrame) * pitchRate;
                    const size_t idx0 = static_cast<size_t>(srcPos);
                    if (idx0 + 1 < totalSrcFrames) {
                        const float frac = static_cast<float>(srcPos - static_cast<double>(idx0));
                        const float l0 = (*clip.stereoPcm)[idx0 * 2];
                        const float r0 = (*clip.stereoPcm)[idx0 * 2 + 1];
                        const float l1 = (*clip.stereoPcm)[(idx0 + 1) * 2];
                        const float r1 = (*clip.stereoPcm)[(idx0 + 1) * 2 + 1];
                        scratchL_[i] += (l0 + (l1 - l0) * frac) * clip.gain;
                        scratchR_[i] += (r0 + (r1 - r0) * frac) * clip.gain;
                    }
                }
            }
        }

        // Apply per-track DSP FX chain & pan/volume
        const float pan = t->pan;
        const float gainL = t->volume * std::sqrt(0.5f * (1.0f - pan));
        const float gainR = t->volume * std::sqrt(0.5f * (1.0f + pan));
        float trPeakL = 0.0f;
        float trPeakR = 0.0f;

        for (int i = 0; i < numFrames; ++i) {
            float l = scratchL_[i];
            float r = scratchR_[i];
            t->fx.processStereo(l, r);
            l *= gainL;
            r *= gainR;
            trPeakL = std::max(trPeakL, std::abs(l));
            trPeakR = std::max(trPeakR, std::abs(r));
            mixL_[i] += l;
            mixR_[i] += r;
        }
        t->peakL.store(trPeakL, std::memory_order_relaxed);
        t->peakR.store(trPeakR, std::memory_order_relaxed);
    }

    // Metronome click mix (not included in offline bounce)
    if (!offlineMode && isPlayingNow && metroClickRemain_ > 0) {
        const float clickFreq = (lastBeatIndex_ % 4 == 0) ? 1320.0f : 880.0f;
        for (int i = 0; i < numFrames && metroClickRemain_ > 0; ++i) {
            const float click = std::sin(kTwoPi * metroPhase_) * 0.28f;
            metroPhase_ += clickFreq / static_cast<float>(sampleRate_);
            if (metroPhase_ >= 1.0f) metroPhase_ -= 1.0f;
            mixL_[i] += click;
            mixR_[i] += click;
            metroClickRemain_--;
        }
    }

    // Master volume + Limiter + Metering
    const float mVol = masterVolume_.load(std::memory_order_relaxed);
    float mPeakL = 0.0f;
    float mPeakR = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        float l = mixL_[i] * mVol;
        float r = mixR_[i] * mVol;
        masterLimiter_.processStereo(l, r);
        outInterleavedStereo[i * 2] = l;
        outInterleavedStereo[i * 2 + 1] = r;
        scratchL_[i] = 0.5f * (l + r);
        mPeakL = std::max(mPeakL, std::abs(l));
        mPeakR = std::max(mPeakR, std::abs(r));
    }

    masterPeakL_.store(mPeakL, std::memory_order_relaxed);
    masterPeakR_.store(mPeakR, std::memory_order_relaxed);

    if (!offlineMode) {
        updateSpectrum(scratchL_.data(), numFrames);
    }

    if (isPlayingNow) {
        int64_t nextFrame = currentFrame + numFrames;
        if (!offlineMode && loopEnabled_ && nextFrame >= loopEndFrame_) {
            nextFrame = loopStartFrame_;
            lastSeqStep_ = -1;
            lastBeatIndex_ = -1;
        }
        playheadFrame_.store(nextFrame, std::memory_order_relaxed);
    }
}

bool AudioEngine::renderOfflineToWav(const std::string& outputPath,
                                     double durationSeconds,
                                     int targetSampleRate,
                                     int channels,
                                     int bitsPerSample) {
    if (durationSeconds <= 0.0) return false;
    const int savedSampleRate = sampleRate_;
    const int64_t savedPlayhead = playheadFrame_.load();

    sampleRate_ = (targetSampleRate == 44100 || targetSampleRate == 48000) ? targetSampleRate : 48000;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        masterLimiter_.prepare(static_cast<float>(sampleRate_));
        for (auto& t : tracks_) {
            t->synth.prepare(static_cast<float>(sampleRate_));
            t->fx.prepare(static_cast<float>(sampleRate_));
            t->synth.allNotesOff();
        }
    }

    playheadFrame_.store(0);
    lastSeqStep_ = -1;
    lastBeatIndex_ = -1;

    const int64_t totalFrames = static_cast<int64_t>(durationSeconds * sampleRate_);
    constexpr int kBlockSize = 512;
    std::vector<float> blockStereo(kBlockSize * 2);
    std::vector<float> outSamples;
    outSamples.reserve(static_cast<size_t>(totalFrames * channels));

    for (int64_t done = 0; done < totalFrames; done += kBlockSize) {
        const int frames = static_cast<int>(std::min<int64_t>(kBlockSize, totalFrames - done));
        renderBlock(blockStereo.data(), frames, true);
        if (channels == 1) {
            for (int i = 0; i < frames; ++i) {
                outSamples.push_back(0.5f * (blockStereo[i * 2] + blockStereo[i * 2 + 1]));
            }
        } else {
            outSamples.insert(outSamples.end(), blockStereo.begin(), blockStereo.begin() + frames * 2);
        }
    }

    // Restore live sampleRate & playhead
    sampleRate_ = savedSampleRate;
    playheadFrame_.store(savedPlayhead);
    lastSeqStep_ = -1;
    lastBeatIndex_ = -1;

    return WavCodec::writeWav(outputPath, outSamples, (targetSampleRate == 44100 ? 44100 : 48000), channels, bitsPerSample);
}

#ifdef __ANDROID__
oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* audioStream,
                                                   void* audioData,
                                                   int32_t numFrames) {
    auto* out = static_cast<float*>(audioData);
    renderBlock(out, numFrames, false);

    // Capture microphone input if recording
    if (recording_.load(std::memory_order_relaxed) && inStream_) {
        std::vector<float> monoIn(numFrames, 0.0f);
        auto res = inStream_->read(monoIn.data(), numFrames, 0);
        if (res && res.value() > 0) {
            std::unique_lock<std::mutex> lock(recMutex_, std::try_to_lock);
            if (lock.owns_lock()) {
                for (int i = 0; i < res.value(); ++i) {
                    recordedStereo_.push_back(monoIn[i]);
                    recordedStereo_.push_back(monoIn[i]);
                }
            }
        }
    }

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* oboeStream, oboe::Result error) {
    LOGE("Oboe stream error: %s — restarting", oboe::convertToText(error));
    start(sampleRate_);
}
#endif

} // namespace rcstudio
