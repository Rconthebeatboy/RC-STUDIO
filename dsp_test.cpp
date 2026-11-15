#include "AudioEngine.h"
#include "WavCodec.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace rcstudio;

int main() {
    std::cout << "Running RC Studio C++ DSP Engine Verification...\n";

    AudioEngine engine;
    assert(engine.start(48000));
    engine.setBpm(140.0f);

    // 1. Add DrumKit, 808 Bass, and Sitar tracks
    int drumTrack = engine.addTrack("Phonk Drums", InstrumentType::DrumKit);
    int bassTrack = engine.addTrack("808 Sub", InstrumentType::Bass808);
    int sitarTrack = engine.addTrack("Indian Sitar", InstrumentType::Sitar);
    assert(drumTrack > 0 && bassTrack > 0 && sitarTrack > 0);

    // 2. Configure 16-step sequencer
    std::array<StepSequencerTrack, 6> lanes{};
    const int notes[6] = {36, 38, 39, 42, 46, 56};
    for (int i = 0; i < 6; ++i) lanes[i].midiNote = notes[i];
    lanes[0].velocities[0] = 1.0f;  lanes[0].velocities[6] = 0.9f;  lanes[0].velocities[10] = 0.95f;
    lanes[1].velocities[4] = 0.9f;  lanes[1].velocities[12] = 0.9f;
    lanes[3].velocities[0] = 0.7f;  lanes[3].velocities[2] = 0.6f;  lanes[3].velocities[4] = 0.7f;
    lanes[5].velocities[3] = 0.8f;  lanes[5].velocities[7] = 0.8f;  // Phonk cowbell
    engine.configureStepSequencer(16, 0.1f, lanes);
    engine.setStepSequencerEnabled(true);

    // 3. Add MIDI notes for 808 and Sitar
    engine.setTrackMidiNotes(bassTrack, {
        {0, 24000, 36, 0.95f},
        {24000, 24000, 39, 0.90f}
    });
    engine.setTrackMidiNotes(sitarTrack, {
        {0, 22000, 60, 0.85f},
        {12000, 22000, 63, 0.85f},
        {24000, 22000, 67, 0.85f}
    });

    // 4. Enable real-time DSP FX on 808 and Sitar
    TrackEffectChain bassFx;
    bassFx.distortion.enabled = true;
    bassFx.distortion.drive = 3.5f;
    bassFx.distortion.mix = 0.45f;
    bassFx.comp.enabled = true;
    engine.updateTrackFx(bassTrack, bassFx);

    TrackEffectChain sitarFx;
    sitarFx.eq.enabled = true;
    sitarFx.eq.highGainDb = 3.0f;
    sitarFx.delay.enabled = true;
    sitarFx.delay.mix = 0.25f;
    sitarFx.reverb.enabled = true;
    sitarFx.reverb.mix = 0.3f;
    engine.updateTrackFx(sitarTrack, sitarFx);

    // 5. Render offline 24-bit WAV @ 48kHz & 16-bit WAV @ 44.1kHz
    const std::string wav24 = "/tmp/rcstudio_test_24bit.wav";
    const std::string wav16 = "/tmp/rcstudio_test_16bit.wav";
    assert(engine.renderOfflineToWav(wav24, 2.0, 48000, 2, 24));
    assert(engine.renderOfflineToWav(wav16, 1.5, 44100, 2, 16));

    // 6. Read back and validate non-zero audio, no NaNs, and exact format
    WavData read24;
    assert(WavCodec::readWav(wav24, read24));
    assert(read24.sampleRate == 48000);
    assert(read24.channels == 2);
    assert(read24.bitsPerSample == 24);
    assert(read24.interleavedSamples.size() == 48000 * 2 * 2);

    float maxAbs = 0.0f;
    for (float s : read24.interleavedSamples) {
        assert(std::isfinite(s));
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    assert(maxAbs > 0.05f && maxAbs <= 1.0f);

    WavData read16;
    assert(WavCodec::readWav(wav16, read16));
    assert(read16.sampleRate == 44100);
    assert(read16.bitsPerSample == 16);

    std::remove(wav24.c_str());
    std::remove(wav16.c_str());

    std::cout << "ALL DSP TESTS PASSED! Peak amplitude = " << maxAbs << "\n";
    return 0;
}
