#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

// Decouples "the capture worker produced a WAV buffer" from "the module decided
// what to do with it", with deliver-exactly-once semantics. Pure (no PulseAudio,
// no Fcitx) so it can be unit-tested directly, mirroring how wav_header.h and
// trimTranscript() are exercised without linking the addon.
//
// Two parties drive it, possibly from different threads:
//   * the capture worker calls setResult(wav) once when it exits;
//   * the module calls resolve(cb) once to decide delivery (cb = deliver the
//     buffer, nullptr = discard). resolve() also raises the stop flag so the
//     worker loop exits.
//
// Delivery happens exactly once, as soon as both sides have acted, regardless of
// which came first. The callback is invoked outside the lock so it may re-enter.
class CaptureDelivery {
public:
    using ResultCB = std::function<void(std::vector<uint8_t>)>;

    void requestStop() { stop_.store(true); }
    bool stopRequested() const { return stop_.load(); }

    // Called by the worker exactly once on exit; the first call wins.
    void setResult(std::vector<uint8_t> wav) {
        ResultCB cb;
        std::vector<uint8_t> out;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (resultReady_) {
                return;
            }
            resultReady_ = true;
            result_ = std::move(wav);
            if (!takeDelivery(cb, out)) {
                return;
            }
        }
        if (cb) {
            cb(std::move(out));
        }
    }

    // Called by the module exactly once; the first call wins. A non-null cb
    // delivers the result, a null cb discards it. Always requests stop so the
    // worker exits its capture loop.
    void resolve(ResultCB cb) {
        stop_.store(true);
        ResultCB fire;
        std::vector<uint8_t> out;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (resolved_) {
                return;
            }
            resolved_ = true;
            cb_ = std::move(cb);
            if (!takeDelivery(fire, out)) {
                return;
            }
        }
        if (fire) {
            fire(std::move(out));
        }
    }

private:
    // Caller must hold mutex_. When both sides are ready and nothing has been
    // delivered yet, marks delivered and hands back the callback (possibly null,
    // meaning discard) and the buffer so they can be used outside the lock;
    // returns true. Otherwise returns false.
    bool takeDelivery(ResultCB &cbOut, std::vector<uint8_t> &out) {
        if (!resultReady_ || !resolved_ || delivered_) {
            return false;
        }
        delivered_ = true;
        cbOut = std::move(cb_);
        out = std::move(result_);
        return true;
    }

    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    bool resultReady_ = false;
    bool resolved_ = false;
    bool delivered_ = false;
    std::vector<uint8_t> result_;
    ResultCB cb_;
};
