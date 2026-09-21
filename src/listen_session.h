#pragma once

#include <cstdint>

// Tracks which job -- a listening session or a history retry -- currently owns
// the module's indicator and recording state.
//
// Speech results arrive asynchronously and can land long after the job that
// issued them ended, possibly after the user already started a new recording. A
// late callback that unconditionally cleared the recording flag used to desync
// the module from AudioCapture: the module believed it was idle while a capture
// session was still open, so every later activation press hit the "already
// recording" guard in AudioCapture::start() and the addon stayed wedged until
// Fcitx5 was restarted.
//
// Each async callback therefore carries the generation it was issued under and
// asks isCurrent() before touching shared state. Pure (no PulseAudio, no Fcitx)
// so it can be unit-tested directly, mirroring capture_delivery.h and
// wav_header.h.
class ListenSession {
public:
    // Begins recording and returns the generation that now owns the UI.
    uint64_t beginListening() {
        ++generation_;
        listening_ = true;
        return generation_;
    }

    // Begins a job that produces a transcript without recording (a history
    // retry), and returns the generation that now owns the UI.
    uint64_t beginRetry() {
        ++generation_;
        listening_ = false;
        return generation_;
    }

    // The user stopped recording. The generation stays current because its
    // transcript is still in flight and may still hide the indicator.
    void stopListening() { listening_ = false; }

    // Abandons the current job (the user cancelled), so anything it left in
    // flight becomes stale.
    void abandon() {
        ++generation_;
        listening_ = false;
    }

    bool listening() const { return listening_; }

    // The generation handed out by the most recent begin* call, for callers
    // that need to tag a callback with the job already in progress.
    uint64_t currentGeneration() const { return generation_; }

    // True while gen is the newest job. Only then may its callbacks touch the
    // indicator or the recording state.
    bool isCurrent(uint64_t gen) const { return gen == generation_; }

private:
    uint64_t generation_ = 0;
    bool listening_ = false;
};
