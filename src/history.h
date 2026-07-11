#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

enum class HistoryEntryKind { Transcript, FailedAudio };

struct HistoryEntry {
    std::filesystem::path path;
    HistoryEntryKind kind;
};

// Reads an entire file into a string. Header-only so tests need no extra link.
inline std::string readFile(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Persists voice-input history entries as one file per entry under a directory.
// Transcripts are saved as ".txt", failed recordings as ".wav". No Fcitx or
// libcurl dependencies so it can be unit-tested standalone.
class History {
public:
    using StampFn = std::function<std::string()>;

    History(std::filesystem::path dir, std::size_t maxEntries,
            StampFn stampFn = defaultStampFn());

    bool saveTranscript(const std::string &text);
    bool saveFailedAudio(const std::vector<uint8_t> &wav);

    std::vector<HistoryEntry> listEntries() const;
    std::optional<HistoryEntry> latestTranscript() const;
    std::optional<HistoryEntry> latestFailedAudio() const;
    bool remove(const std::filesystem::path &path);

private:
    static StampFn defaultStampFn();
    // Returns a unique path with the given extension, resolving collisions.
    std::filesystem::path allocatePath(const std::string &ext);
    bool writeBytes(const std::string &ext, const void *data, std::size_t n);
    void prune();

    std::filesystem::path dir_;
    std::size_t maxEntries_;
    StampFn stampFn_;
};
