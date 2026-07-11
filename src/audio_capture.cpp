#include "audio_capture.h"
#include "capture_delivery.h"
#include "wav_header.h"

#include <fcitx-utils/log.h>

#include <pulse/error.h>
#include <pulse/simple.h>

#include <array>
#include <thread>
#include <utility>

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    // Signal any in-flight worker to stop and discard its result. We
    // deliberately do NOT join: a worker parked in a blocking PulseAudio call
    // must never freeze teardown. The worker is self-contained and keeps the
    // session alive via its own shared_ptr, so it is safe to outlive us.
    if (session_) {
        session_->resolve(nullptr);
    }
}

bool AudioCapture::start() {
    if (session_) {
        return false;
    }
    session_ = std::make_shared<CaptureDelivery>();
    std::thread(&AudioCapture::captureLoop, session_).detach();
    return true;
}

void AudioCapture::finish(ResultCB onResult) {
    if (!session_) {
        if (onResult) {
            onResult({});
        }
        return;
    }
    session_->resolve(std::move(onResult));
    session_.reset();
}

void AudioCapture::cancel() {
    if (!session_) {
        return;
    }
    session_->resolve(nullptr);
    session_.reset();
}

void AudioCapture::captureLoop(std::shared_ptr<CaptureDelivery> session) {
    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = kSampleRate;
    spec.channels = static_cast<uint8_t>(kChannels);

    constexpr size_t kChunkSamples = 1600; // ~100 ms at 16 kHz
    constexpr uint32_t kChunkBytes =
        static_cast<uint32_t>(kChunkSamples * sizeof(int16_t));

    // Ask the server for fragments matching our read size so a stop request is
    // observed within roughly one chunk. Without this, the default fragsize can
    // be hundreds of ms or more.
    pa_buffer_attr attr{};
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.tlength = static_cast<uint32_t>(-1);
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);
    attr.fragsize = kChunkBytes;

    int err = 0;
    pa_simple *stream = pa_simple_new(nullptr, "fcitx5-voice-input",
                                      PA_STREAM_RECORD, nullptr, "Voice Input",
                                      &spec, nullptr, &attr, &err);
    if (!stream) {
        FCITX_WARN() << "voiceinput: pa_simple_new failed: " << pa_strerror(err);
        session->setResult({});
        return;
    }

    std::vector<int16_t> pcm;
    std::array<int16_t, kChunkSamples> chunk{};

    while (!session->stopRequested()) {
        if (pa_simple_read(stream, chunk.data(),
                           chunk.size() * sizeof(int16_t), &err) < 0) {
            FCITX_WARN() << "voiceinput: pa_simple_read failed: "
                         << pa_strerror(err);
            break;
        }
        pcm.insert(pcm.end(), chunk.begin(), chunk.end());
    }

    pa_simple_free(stream);

    if (pcm.empty()) {
        session->setResult({});
        return;
    }

    const uint32_t pcmBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const auto header =
        buildWavHeader(pcmBytes, kSampleRate, kChannels, kBitsPerSample);

    std::vector<uint8_t> wav;
    wav.reserve(header.size() + pcmBytes);
    wav.insert(wav.end(), header.begin(), header.end());
    const auto *pcmBytesPtr = reinterpret_cast<const uint8_t *>(pcm.data());
    wav.insert(wav.end(), pcmBytesPtr, pcmBytesPtr + pcmBytes);
    session->setResult(std::move(wav));
}
