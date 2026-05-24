#include "wav_header.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

uint16_t readU16LE(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32LE(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool tagEquals(const uint8_t *p, const char *tag) {
    return std::memcmp(p, tag, 4) == 0;
}

} // namespace

int main() {
    constexpr uint32_t kPcmBytes = 32000; // 1 second @ 16 kHz mono S16LE
    const auto h = buildWavHeader(kPcmBytes);

    // RIFF / WAVE / fmt  / data tags
    assert(tagEquals(h.data() + 0, "RIFF"));
    assert(tagEquals(h.data() + 8, "WAVE"));
    assert(tagEquals(h.data() + 12, "fmt "));
    assert(tagEquals(h.data() + 36, "data"));

    // RIFF chunk size = 36 + dataSize
    assert(readU32LE(h.data() + 4) == 36u + kPcmBytes);

    // fmt subchunk: size=16, audioFormat=1 (PCM), channels=1, sampleRate=16000,
    //               byteRate=32000, blockAlign=2, bitsPerSample=16
    assert(readU32LE(h.data() + 16) == 16u);
    assert(readU16LE(h.data() + 20) == 1u);
    assert(readU16LE(h.data() + 22) == 1u);
    assert(readU32LE(h.data() + 24) == 16000u);
    assert(readU32LE(h.data() + 28) == 32000u);
    assert(readU16LE(h.data() + 32) == 2u);
    assert(readU16LE(h.data() + 34) == 16u);

    // data subchunk size = pcm byte count
    assert(readU32LE(h.data() + 40) == kPcmBytes);

    // Empty recording case: dataSize=0, riffSize=36
    const auto empty = buildWavHeader(0);
    assert(readU32LE(empty.data() + 4) == 36u);
    assert(readU32LE(empty.data() + 40) == 0u);

    std::cout << "wav_header_test: OK" << std::endl;
    return 0;
}
