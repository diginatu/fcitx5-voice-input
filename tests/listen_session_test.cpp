#include "listen_session.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    // A fresh session is not listening.
    {
        ListenSession s;
        assert(!s.listening());
    }

    // beginListening() starts listening and hands out a generation that owns
    // the UI.
    {
        ListenSession s;
        const uint64_t gen = s.beginListening();
        assert(s.listening());
        assert(s.isCurrent(gen));
    }

    // stopListening() ends the recording but keeps the generation current: the
    // transcript is still in flight and may hide the indicator when it lands.
    {
        ListenSession s;
        const uint64_t gen = s.beginListening();
        s.stopListening();
        assert(!s.listening());
        assert(s.isCurrent(gen));
    }

    // Regression: a new recording started while the previous transcript is
    // still in flight must survive that transcript landing. The old generation
    // no longer owns the UI, so its callback must not clear listening().
    {
        ListenSession s;
        const uint64_t first = s.beginListening();
        s.stopListening(); // user pressed the key again -> "Transcribing..."
        const uint64_t second = s.beginListening(); // impatient new recording
        assert(s.listening());
        assert(first != second);
        assert(!s.isCurrent(first));
        assert(s.isCurrent(second));
    }

    // currentGeneration() reports the generation the begin* calls handed out,
    // so callers that did not start the job (finishRecording, the capture
    // delivery) can tag their callbacks with it. stopListening() leaves it
    // alone; starting a new job moves it on.
    {
        ListenSession s;
        const uint64_t gen = s.beginListening();
        assert(s.currentGeneration() == gen);
        s.stopListening();
        assert(s.currentGeneration() == gen);
        const uint64_t next = s.beginListening();
        assert(s.currentGeneration() == next);
        assert(next != gen);
    }

    // abandon() (cancel) stops listening and makes the cancelled generation
    // stale, so nothing it left in flight can touch the UI.
    {
        ListenSession s;
        const uint64_t gen = s.beginListening();
        s.abandon();
        assert(!s.listening());
        assert(!s.isCurrent(gen));
    }

    // beginRetry() is a job that produces a transcript without recording, so it
    // owns the UI but does not enter the listening state.
    {
        ListenSession s;
        const uint64_t gen = s.beginRetry();
        assert(!s.listening());
        assert(s.isCurrent(gen));
    }

    // A recording started while a retry is in flight supersedes it.
    {
        ListenSession s;
        const uint64_t retry = s.beginRetry();
        const uint64_t listen = s.beginListening();
        assert(!s.isCurrent(retry));
        assert(s.isCurrent(listen));
        assert(s.listening());
    }

    std::cout << "listen_session_test passed\n";
    return 0;
}
