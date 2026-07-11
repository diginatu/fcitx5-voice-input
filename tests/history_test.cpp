#include "history.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A fresh empty temp dir unique to this process run.
fs::path makeTempDir() {
    fs::path base =
        fs::temp_directory_path() / ("voiceinput_history_test_" +
                                     std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

// Deterministic monotonically increasing stamps so ordering is predictable.
struct FakeClock {
    int n = 0;
    std::string operator()() {
        // zero-padded so lexicographic order == chronological order
        char buf[32];
        std::snprintf(buf, sizeof(buf), "20260101-000%03d", n++);
        return std::string(buf);
    }
};

} // namespace

int main() {
    // Save a transcript, read it back verbatim.
    {
        fs::path dir = makeTempDir();
        History h(dir, 50, FakeClock{});
        assert(h.saveTranscript("hello world"));
        auto entries = h.listEntries();
        assert(entries.size() == 1);
        assert(entries[0].kind == HistoryEntryKind::Transcript);
        assert(readFile(entries[0].path) == "hello world");
    }

    // Save failed audio, read the bytes back verbatim.
    {
        fs::path dir = makeTempDir();
        History h(dir, 50, FakeClock{});
        std::vector<uint8_t> wav = {'R', 'I', 'F', 'F', 0, 1, 2, 3};
        assert(h.saveFailedAudio(wav));
        auto entries = h.listEntries();
        assert(entries.size() == 1);
        assert(entries[0].kind == HistoryEntryKind::FailedAudio);
        std::string got = readFile(entries[0].path);
        assert(std::vector<uint8_t>(got.begin(), got.end()) == wav);
    }

    // Newest-first ordering across kinds.
    {
        fs::path dir = makeTempDir();
        History h(dir, 50, FakeClock{});
        h.saveTranscript("first");            // stamp ...000
        h.saveFailedAudio({1});               // stamp ...001
        h.saveTranscript("third");            // stamp ...002
        auto entries = h.listEntries();
        assert(entries.size() == 3);
        assert(readFile(entries[0].path) == "third");
        assert(entries[1].kind == HistoryEntryKind::FailedAudio);
        assert(readFile(entries[2].path) == "first");
    }

    // Prune keeps only the newest maxEntries.
    {
        fs::path dir = makeTempDir();
        History h(dir, 2, FakeClock{});
        h.saveTranscript("a"); // ...000
        h.saveTranscript("b"); // ...001
        h.saveTranscript("c"); // ...002
        auto entries = h.listEntries();
        assert(entries.size() == 2);
        assert(readFile(entries[0].path) == "c");
        assert(readFile(entries[1].path) == "b");
    }

    // Same-stamp collision gets a numeric suffix (both survive).
    {
        fs::path dir = makeTempDir();
        // A clock that always returns the same stamp.
        History h(dir, 50, []() { return std::string("20260101-120000"); });
        assert(h.saveTranscript("one"));
        assert(h.saveTranscript("two"));
        auto entries = h.listEntries();
        assert(entries.size() == 2);
    }

    // latestTranscript / latestFailedAudio pick the newest of each kind.
    {
        fs::path dir = makeTempDir();
        History h(dir, 50, FakeClock{});
        h.saveFailedAudio({9}); // ...000
        h.saveTranscript("t1"); // ...001
        h.saveFailedAudio({8}); // ...002
        auto lt = h.latestTranscript();
        auto la = h.latestFailedAudio();
        assert(lt.has_value() && readFile(lt->path) == "t1");
        assert(la.has_value());
        std::string audio = readFile(la->path);
        assert(audio.size() == 1 && (uint8_t)audio[0] == 8);
    }

    // remove deletes exactly the given entry.
    {
        fs::path dir = makeTempDir();
        History h(dir, 50, FakeClock{});
        h.saveTranscript("keep"); // ...000
        h.saveFailedAudio({1});   // ...001
        auto fa = h.latestFailedAudio();
        assert(fa.has_value());
        assert(h.remove(fa->path));
        auto entries = h.listEntries();
        assert(entries.size() == 1);
        assert(readFile(entries[0].path) == "keep");
    }

    std::cout << "history_test: OK" << std::endl;
    return 0;
}
