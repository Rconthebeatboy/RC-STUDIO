#include "WavCodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace rcstudio {

namespace {
inline uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void writeU16(std::ofstream& out, uint16_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
    out.write(reinterpret_cast<const char*>(b), 2);
}

inline void writeU32(std::ofstream& out, uint32_t v) {
    uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
    out.write(reinterpret_cast<const char*>(b), 4);
}
} // namespace

bool WavCodec::readWav(const std::string& filePath, WavData& outData) {
    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;

    const std::streamsize fileSize = in.tellg();
    if (fileSize < 44) return false;
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), fileSize)) return false;

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t pos = 12;
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* dataPtr = nullptr;
    uint32_t dataSize = 0;

    while (pos + 8 <= bytes.size()) {
        const uint8_t* chunkId = bytes.data() + pos;
        uint32_t chunkSize = readU32(bytes.data() + pos + 4);
        pos += 8;

        if (pos + chunkSize > bytes.size()) break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = readU16(bytes.data() + pos);
            channels = readU16(bytes.data() + pos + 2);
            sampleRate = readU32(bytes.data() + pos + 4);
            bitsPerSample = readU16(bytes.data() + pos + 14);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataPtr = bytes.data() + pos;
            dataSize = chunkSize;
        }
        pos += chunkSize + (chunkSize & 1);
    }

    if (!dataPtr || dataSize == 0 || channels == 0 || sampleRate == 0) return false;
    if (audioFormat != 1 && audioFormat != 3) return false; // PCM or IEEE float

    outData.sampleRate = static_cast<int>(sampleRate);
    outData.channels = static_cast<int>(channels);
    outData.bitsPerSample = static_cast<int>(bitsPerSample);
    outData.interleavedSamples.clear();

    if (audioFormat == 1 && bitsPerSample == 16) {
        const size_t count = dataSize / 2;
        outData.interleavedSamples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            int16_t s = static_cast<int16_t>(readU16(dataPtr + i * 2));
            outData.interleavedSamples[i] = static_cast<float>(s) / 32768.0f;
        }
    } else if (audioFormat == 1 && bitsPerSample == 24) {
        const size_t count = dataSize / 3;
        outData.interleavedSamples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            int32_t v = (static_cast<int32_t>(dataPtr[i * 3])) |
                        (static_cast<int32_t>(dataPtr[i * 3 + 1]) << 8) |
                        (static_cast<int32_t>(dataPtr[i * 3 + 2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;
            outData.interleavedSamples[i] = static_cast<float>(v) / 8388608.0f;
        }
    } else if (audioFormat == 3 && bitsPerSample == 32) {
        const size_t count = dataSize / 4;
        outData.interleavedSamples.resize(count);
        std::memcpy(outData.interleavedSamples.data(), dataPtr, count * sizeof(float));
    } else {
        return false;
    }

    return true;
}

bool WavCodec::writeWav(const std::string& filePath,
                        const std::vector<float>& interleavedSamples,
                        int sampleRate,
                        int channels,
                        int bitsPerSample) {
    if (bitsPerSample != 16 && bitsPerSample != 24) {
        bitsPerSample = 16;
    }
    if (channels < 1 || channels > 2 || sampleRate <= 0) return false;

    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) return false;

    const uint16_t bytesPerSample = static_cast<uint16_t>(bitsPerSample / 8);
    const uint32_t dataSize = static_cast<uint32_t>(interleavedSamples.size() * bytesPerSample);
    const uint32_t riffSize = 36 + dataSize;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * bytesPerSample);
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;

    out.write("RIFF", 4);
    writeU32(out, riffSize);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 1); // PCM
    writeU16(out, static_cast<uint16_t>(channels));
    writeU32(out, static_cast<uint32_t>(sampleRate));
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, static_cast<uint16_t>(bitsPerSample));

    out.write("data", 4);
    writeU32(out, dataSize);

    if (bitsPerSample == 16) {
        for (float s : interleavedSamples) {
            float clamped = std::clamp(s, -1.0f, 1.0f);
            int16_t pcm = static_cast<int16_t>(std::lrintf(clamped * 32767.0f));
            uint8_t b[2] = {static_cast<uint8_t>(pcm & 0xFF), static_cast<uint8_t>((pcm >> 8) & 0xFF)};
            out.write(reinterpret_cast<const char*>(b), 2);
        }
    } else {
        for (float s : interleavedSamples) {
            float clamped = std::clamp(s, -1.0f, 1.0f);
            int32_t pcm = static_cast<int32_t>(std::lrintf(clamped * 8388607.0f));
            uint8_t b[3] = {
                static_cast<uint8_t>(pcm & 0xFF),
                static_cast<uint8_t>((pcm >> 8) & 0xFF),
                static_cast<uint8_t>((pcm >> 16) & 0xFF)
            };
            out.write(reinterpret_cast<const char*>(b), 3);
        }
    }

    return out.good();
}

} // namespace rcstudio
