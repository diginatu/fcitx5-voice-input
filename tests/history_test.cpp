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

    std::cout << "history_test: OK" << std::endl;
    return 0;
}
