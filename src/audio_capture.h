#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture &) = delete;
    AudioCapture &operator=(const AudioCapture &) = delete;

    bool start();
    std::vector<uint8_t> stop();
    bool isRecording() const { return running_.load(); }

    static constexpr uint32_t kSampleRate = 16000;
    static constexpr uint16_t kChannels = 1;
    static constexpr uint16_t kBitsPerSample = 16;

private:
    void captureLoop();

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex pcmMutex_;
    std::vector<int16_t> pcm_;
};
