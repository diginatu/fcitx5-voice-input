#include "audio_capture.h"
#include "wav_header.h"

#include <fcitx-utils/log.h>

#include <pulse/error.h>
#include <pulse/simple.h>

#include <array>
#include <cstring>

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    if (running_.load()) {
        running_.store(false);
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

bool AudioCapture::start() {
    if (running_.exchange(true)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(pcmMutex_);
        pcm_.clear();
    }
    worker_ = std::thread(&AudioCapture::captureLoop, this);
    return true;
}

void AudioCapture::captureLoop() {
    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = kSampleRate;
    spec.channels = static_cast<uint8_t>(kChannels);

    int err = 0;
    pa_simple *stream = pa_simple_new(nullptr, "fcitx5-voice-input",
                                      PA_STREAM_RECORD, nullptr, "Voice Input",
                                      &spec, nullptr, nullptr, &err);
    if (!stream) {
        FCITX_WARN() << "voiceinput: pa_simple_new failed: " << pa_strerror(err);
        running_.store(false);
        return;
    }

    constexpr size_t kChunkSamples = 320; // ~20 ms at 16 kHz
    std::array<int16_t, kChunkSamples> chunk{};

    while (running_.load()) {
        if (pa_simple_read(stream, chunk.data(),
                           chunk.size() * sizeof(int16_t), &err) < 0) {
            FCITX_WARN() << "voiceinput: pa_simple_read failed: "
                         << pa_strerror(err);
            break;
        }
        std::lock_guard<std::mutex> lk(pcmMutex_);
        pcm_.insert(pcm_.end(), chunk.begin(), chunk.end());
    }

    pa_simple_free(stream);
}

std::vector<uint8_t> AudioCapture::stop() {
    if (running_.exchange(false)) {
        if (worker_.joinable()) {
            worker_.join();
        }
    } else if (worker_.joinable()) {
        worker_.join();
    }

    std::vector<int16_t> samples;
    {
        std::lock_guard<std::mutex> lk(pcmMutex_);
        samples.swap(pcm_);
    }
    if (samples.empty()) {
        return {};
    }

    const uint32_t pcmBytes =
        static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const auto header = buildWavHeader(pcmBytes, kSampleRate, kChannels,
                                       kBitsPerSample);

    std::vector<uint8_t> wav;
    wav.reserve(header.size() + pcmBytes);
    wav.insert(wav.end(), header.begin(), header.end());
    const auto *pcmBytesPtr = reinterpret_cast<const uint8_t *>(samples.data());
    wav.insert(wav.end(), pcmBytesPtr, pcmBytesPtr + pcmBytes);
    return wav;
}
