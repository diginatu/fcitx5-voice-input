#include "capture_delivery.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <vector>

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> v) {
    return std::vector<uint8_t>(v);
}

// Records what a resolve() callback received, and how many times.
struct Sink {
    int calls = 0;
    std::vector<uint8_t> last;
    CaptureDelivery::ResultCB cb() {
        return [this](std::vector<uint8_t> wav) {
            ++calls;
            last = std::move(wav);
        };
    }
};

} // namespace

int main() {
    // Stop flag: starts clear, set by requestStop() and by resolve().
    {
        CaptureDelivery d;
        assert(!d.stopRequested());
        d.requestStop();
        assert(d.stopRequested());
    }
    {
        CaptureDelivery d;
        Sink s;
        d.resolve(s.cb()); // resolve must also request stop so the worker exits
        assert(d.stopRequested());
    }

    // Order A: worker finishes first, then module resolves to deliver.
    {
        CaptureDelivery d;
        Sink s;
        d.setResult(bytes({1, 2, 3}));
        assert(s.calls == 0); // nothing delivered until resolve()
        d.resolve(s.cb());
        assert(s.calls == 1);
        assert((s.last == bytes({1, 2, 3})));
    }

    // Order B: module resolves first, then worker finishes.
    {
        CaptureDelivery d;
        Sink s;
        d.resolve(s.cb());
        assert(s.calls == 0); // no result yet
        d.setResult(bytes({4, 5}));
        assert(s.calls == 1);
        assert((s.last == bytes({4, 5})));
    }

    // Discard (cancel): resolve(nullptr) must never deliver, in either order.
    {
        CaptureDelivery d;
        Sink s;
        d.resolve(nullptr);
        d.setResult(bytes({9, 9}));
        assert(s.calls == 0);
    }
    {
        CaptureDelivery d;
        Sink s;
        d.setResult(bytes({9, 9}));
        d.resolve(nullptr);
        assert(s.calls == 0);
    }

    // Deliver exactly once even if setResult / resolve are called repeatedly.
    {
        CaptureDelivery d;
        Sink s;
        d.setResult(bytes({7}));
        d.setResult(bytes({8})); // worker should never call twice, but be safe
        d.resolve(s.cb());
        d.resolve(s.cb());
        assert(s.calls == 1);
        assert((s.last == bytes({7}))); // first result wins
    }

    // Empty result still delivers (so the module can show "0 bytes" / hide).
    {
        CaptureDelivery d;
        Sink s;
        d.resolve(s.cb());
        d.setResult({});
        assert(s.calls == 1);
        assert(s.last.empty());
    }

    // Worker keeps the session alive via shared_ptr; delivery still works after
    // the owner drops its reference.
    {
        auto d = std::make_shared<CaptureDelivery>();
        Sink s;
        d->resolve(s.cb());
        std::weak_ptr<CaptureDelivery> weak = d;
        // simulate AudioCapture dropping its reference while the worker runs
        auto workerRef = d;
        d.reset();
        assert(!weak.expired()); // worker still holds it
        workerRef->setResult(bytes({1}));
        assert(s.calls == 1);
    }

    std::cout << "capture_delivery_test: OK" << std::endl;
    return 0;
}
