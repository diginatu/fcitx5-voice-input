#pragma once
#include <array>
#include <cstdint>
#include <cstring>

inline std::array<uint8_t, 44> buildWavHeader(
        uint32_t pcmByteCount,
        uint32_t sampleRate = 16000,
        uint16_t channels = 1,
        uint16_t bitsPerSample = 16) {
    std::array<uint8_t, 44> h{};
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(h.data() + off, &v, 4); };
    auto put16 = [&](size_t off, uint16_t v) { std::memcpy(h.data() + off, &v, 2); };

    const uint32_t byteRate   = sampleRate * channels * bitsPerSample / 8;
    const uint16_t blockAlign = channels * bitsPerSample / 8;
    const uint32_t riffSize   = 36 + pcmByteCount;

    std::memcpy(h.data() + 0,  "RIFF", 4);
    put32(4, riffSize);
    std::memcpy(h.data() + 8,  "WAVE", 4);
    std::memcpy(h.data() + 12, "fmt ", 4);
    put32(16, 16);
    put16(20, 1);
    put16(22, channels);
    put32(24, sampleRate);
    put32(28, byteRate);
    put16(32, blockAlign);
    put16(34, bitsPerSample);
    std::memcpy(h.data() + 36, "data", 4);
    put32(40, pcmByteCount);
    return h;
}
