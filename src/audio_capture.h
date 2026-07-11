#pragma once
#include "capture_delivery.h"

#include <cstdint>
#include <memory>

class AudioCapture {
public:
    using ResultCB = CaptureDelivery::ResultCB;

    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture &) = delete;
    AudioCapture &operator=(const AudioCapture &) = delete;

    // Begins recording on a detached worker thread. Returns false if a recording
    // is already in progress.
    bool start();

    // Stops recording and asks for the captured WAV to be delivered to onResult
    // once the worker finishes. Non-blocking: it never joins the worker, so the
    // calling (Fcitx main) thread can never be frozen by a stalled audio device.
    // onResult may be invoked on the worker thread; the caller is responsible for
    // marshalling back to its own thread.
    void finish(ResultCB onResult);

    // Stops recording and discards whatever was captured. Non-blocking.
    void cancel();

    bool isRecording() const { return static_cast<bool>(session_); }

    static constexpr uint32_t kSampleRate = 16000;
    static constexpr uint16_t kChannels = 1;
    static constexpr uint16_t kBitsPerSample = 16;

private:
    // Self-contained worker: owns its PCM buffer locally and keeps the session
    // alive via the shared_ptr it is passed, so it is safe to outlive the
    // AudioCapture object (e.g. when a stalled device wedges a read).
    static void captureLoop(std::shared_ptr<CaptureDelivery> session);

    std::shared_ptr<CaptureDelivery> session_;
};
