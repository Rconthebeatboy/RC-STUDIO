#include "AudioEngine.h"

#ifdef __ANDROID__
#include <jni.h>
#include <memory>
#include <string>
#include <vector>

using namespace rcstudio;

namespace {
std::unique_ptr<AudioEngine> gEngine;

std::string jstringToString(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}
} // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeStart(JNIEnv*, jobject, jint sampleRate) {
    if (!gEngine) gEngine = std::make_unique<AudioEngine>();
    return gEngine->start(sampleRate) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeStop(JNIEnv*, jobject) {
    if (gEngine) gEngine->stop();
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetPlaying(JNIEnv*, jobject, jboolean playing) {
    if (gEngine) gEngine->setPlaying(playing == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetRecording(JNIEnv*, jobject, jboolean recording) {
    if (gEngine) gEngine->setRecording(recording == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetMetronome(JNIEnv*, jobject, jboolean enabled) {
    if (gEngine) gEngine->setMetronomeEnabled(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetBpm(JNIEnv*, jobject, jfloat bpm) {
    if (gEngine) gEngine->setBpm(bpm);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSeekFrame(JNIEnv*, jobject, jlong frame) {
    if (gEngine) gEngine->seekFrame(frame);
}

JNIEXPORT jlong JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeGetPlayheadFrame(JNIEnv*, jobject) {
    return gEngine ? static_cast<jlong>(gEngine->getPlayheadFrame()) : 0L;
}

JNIEXPORT jint JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeAddTrack(JNIEnv* env, jobject, jstring name, jint instrumentOrdinal) {
    if (!gEngine) return -1;
    return gEngine->addTrack(jstringToString(env, name), static_cast<InstrumentType>(instrumentOrdinal));
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeRemoveTrack(JNIEnv*, jobject, jint trackId) {
    if (gEngine) gEngine->removeTrack(trackId);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetTrackMix(
    JNIEnv*, jobject, jint trackId, jfloat volume, jfloat pan, jboolean mute, jboolean solo, jboolean arm) {
    if (gEngine) gEngine->setTrackMix(trackId, volume, pan, mute == JNI_TRUE, solo == JNI_TRUE, arm == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetTrackInstrument(JNIEnv*, jobject, jint trackId, jint instrumentOrdinal) {
    if (gEngine) gEngine->setTrackInstrument(trackId, static_cast<InstrumentType>(instrumentOrdinal));
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetTrackSynthParams(
    JNIEnv*, jobject, jint trackId, jint osc1, jint osc2, jfloat detune, jfloat oscMix, jfloat subMix,
    jfloat attack, jfloat decay, jfloat sustain, jfloat release, jfloat cutoff, jfloat res,
    jfloat lfoRate, jfloat lfoDepth, jfloat drive) {
    if (!gEngine) return;
    SynthParams p;
    p.osc1Wave = static_cast<Waveform>(osc1);
    p.osc2Wave = static_cast<Waveform>(osc2);
    p.osc2DetuneCents = detune;
    p.oscMix = oscMix;
    p.subOscMix = subMix;
    p.attackSec = attack;
    p.decaySec = decay;
    p.sustainLevel = sustain;
    p.releaseSec = release;
    p.filterCutoffHz = cutoff;
    p.filterResonance = res;
    p.lfoRateHz = lfoRate;
    p.lfoDepth = lfoDepth;
    p.drive = drive;
    gEngine->setTrackSynthParams(trackId, p);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeUpdateTrackFx(
    JNIEnv*, jobject, jint trackId,
    jboolean eqOn, jfloat eqLow, jfloat eqMidFreq, jfloat eqMid, jfloat eqHigh,
    jboolean compOn, jfloat compThresh, jfloat compRatio, jfloat compMakeup,
    jboolean distOn, jfloat distDrive, jfloat distMix,
    jboolean chorusOn, jfloat chorusRate, jfloat chorusMix, jboolean flangerMode,
    jboolean delayOn, jfloat delayMs, jfloat delayFb, jfloat delayMix,
    jboolean revOn, jfloat revSize, jfloat revDamp, jfloat revMix,
    jboolean wideOn, jfloat wideWidth,
    jboolean panOn, jfloat panRate, jfloat panDepth) {
    if (!gEngine) return;
    TrackEffectChain fx;
    fx.eq.enabled = (eqOn == JNI_TRUE);
    fx.eq.lowGainDb = eqLow;
    fx.eq.midFreqHz = eqMidFreq;
    fx.eq.midGainDb = eqMid;
    fx.eq.highGainDb = eqHigh;

    fx.comp.enabled = (compOn == JNI_TRUE);
    fx.comp.thresholdDb = compThresh;
    fx.comp.ratio = compRatio;
    fx.comp.makeupDb = compMakeup;

    fx.distortion.enabled = (distOn == JNI_TRUE);
    fx.distortion.drive = distDrive;
    fx.distortion.mix = distMix;

    fx.chorus.enabled = (chorusOn == JNI_TRUE);
    fx.chorus.rateHz = chorusRate;
    fx.chorus.mix = chorusMix;
    fx.chorus.flangerMode = (flangerMode == JNI_TRUE);

    fx.delay.enabled = (delayOn == JNI_TRUE);
    fx.delay.delayMs = delayMs;
    fx.delay.feedback = delayFb;
    fx.delay.mix = delayMix;

    fx.reverb.enabled = (revOn == JNI_TRUE);
    fx.reverb.roomSize = revSize;
    fx.reverb.damping = revDamp;
    fx.reverb.mix = revMix;

    fx.widener.enabled = (wideOn == JNI_TRUE);
    fx.widener.width = wideWidth;

    fx.autoPan.enabled = (panOn == JNI_TRUE);
    fx.autoPan.rateHz = panRate;
    fx.autoPan.depth = panDepth;

    gEngine->updateTrackFx(trackId, fx);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSendNoteOn(JNIEnv*, jobject, jint trackId, jint note, jfloat velocity) {
    if (gEngine) gEngine->sendLiveNoteOn(trackId, note, velocity);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSendNoteOff(JNIEnv*, jobject, jint trackId, jint note) {
    if (gEngine) gEngine->sendLiveNoteOff(trackId, note);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetTrackMidiNotes(
    JNIEnv* env, jobject, jint trackId, jlongArray starts, jlongArray durations, jintArray notes, jfloatArray velocities) {
    if (!gEngine) return;
    jsize count = env->GetArrayLength(notes);
    jlong* sPtr = env->GetLongArrayElements(starts, nullptr);
    jlong* dPtr = env->GetLongArrayElements(durations, nullptr);
    jint* nPtr = env->GetIntArrayElements(notes, nullptr);
    jfloat* vPtr = env->GetFloatArrayElements(velocities, nullptr);

    std::vector<MidiNoteEvent> list(count);
    for (jsize i = 0; i < count; ++i) {
        list[i] = {sPtr[i], dPtr[i], nPtr[i], vPtr[i]};
    }
    gEngine->setTrackMidiNotes(trackId, list);

    env->ReleaseLongArrayElements(starts, sPtr, JNI_ABORT);
    env->ReleaseLongArrayElements(durations, dPtr, JNI_ABORT);
    env->ReleaseIntArrayElements(notes, nPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(velocities, vPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeConfigureSequencer(
    JNIEnv* env, jobject, jint steps, jfloat swing, jintArray laneNotes, jfloatArray flatVelocities) {
    if (!gEngine) return;
    jint* notesPtr = env->GetIntArrayElements(laneNotes, nullptr);
    jfloat* velPtr = env->GetFloatArrayElements(flatVelocities, nullptr);
    std::array<StepSequencerTrack, 6> lanes{};
    for (int l = 0; l < 6; ++l) {
        lanes[l].midiNote = notesPtr[l];
        for (int s = 0; s < 32; ++s) {
            lanes[l].velocities[s] = velPtr[l * 32 + s];
        }
    }
    gEngine->configureStepSequencer(steps, swing, lanes);
    env->ReleaseIntArrayElements(laneNotes, notesPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(flatVelocities, velPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeAddAudioClip(
    JNIEnv* env, jobject, jint trackId, jint clipId, jlong startFrame, jlong offsetFrame, jlong lengthFrames,
    jfloat gain, jfloat pitchSemitones, jfloatArray stereoPcm) {
    if (!gEngine) return;
    jsize len = env->GetArrayLength(stereoPcm);
    jfloat* ptr = env->GetFloatArrayElements(stereoPcm, nullptr);
    auto vec = std::make_shared<std::vector<float>>(ptr, ptr + len);
    env->ReleaseFloatArrayElements(stereoPcm, ptr, JNI_ABORT);

    AudioClipData clip{clipId, startFrame, offsetFrame, lengthFrames, gain, pitchSemitones, vec};
    gEngine->addAudioClip(trackId, clip);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeClearAudioClips(JNIEnv*, jobject, jint trackId) {
    if (gEngine) gEngine->clearAudioClips(trackId);
}

JNIEXPORT void JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeSetMasterVolume(JNIEnv*, jobject, jfloat vol) {
    if (gEngine) gEngine->setMasterVolume(vol);
}

JNIEXPORT jfloatArray JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeGetMeterSnapshot(JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(2 + AudioEngine::kSpectrumBands);
    std::vector<float> buf(2 + AudioEngine::kSpectrumBands, 0.0f);
    if (gEngine) {
        buf[0] = gEngine->getMasterPeakL();
        buf[1] = gEngine->getMasterPeakR();
        gEngine->getSpectrum(buf.data() + 2);
    }
    env->SetFloatArrayRegion(arr, 0, static_cast<jsize>(buf.size()), buf.data());
    return arr;
}

JNIEXPORT jfloatArray JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeConsumeRecordedAudio(JNIEnv* env, jobject) {
    if (!gEngine) return env->NewFloatArray(0);
    auto pcm = gEngine->consumeRecordedAudio();
    jfloatArray arr = env->NewFloatArray(static_cast<jsize>(pcm.size()));
    if (!pcm.empty()) {
        env->SetFloatArrayRegion(arr, 0, static_cast<jsize>(pcm.size()), pcm.data());
    }
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_com_rcstudio_daw_audio_NativeAudioEngine_nativeExportWav(
    JNIEnv* env, jobject, jstring outputPath, jdouble durationSec, jint sampleRate, jint channels, jint bitsPerSample) {
    if (!gEngine) return JNI_FALSE;
    return gEngine->renderOfflineToWav(
        jstringToString(env, outputPath),
        durationSec,
        sampleRate,
        channels,
        bitsPerSample
    ) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
#endif
