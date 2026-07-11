#include "history.h"

#include <algorithm>
#include <ctime>

History::History(std::filesystem::path dir, std::size_t maxEntries,
                 StampFn stampFn)
    : dir_(std::move(dir)), maxEntries_(maxEntries),
      stampFn_(std::move(stampFn)) {}

History::StampFn History::defaultStampFn() {
    return []() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
        return std::string(buf);
    };
}

std::filesystem::path History::allocatePath(const std::string &ext) {
    std::string stamp = stampFn_();
    std::filesystem::path p = dir_ / (stamp + ext);
    int suffix = 1;
    while (std::filesystem::exists(p)) {
        p = dir_ / (stamp + "-" + std::to_string(suffix++) + ext);
    }
    return p;
}

bool History::writeBytes(const std::string &ext, const void *data,
                         std::size_t n) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        return false;
    }
    std::filesystem::path p = allocatePath(ext);
    std::ofstream out(p, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
    out.close();
    if (!out) {
        return false;
    }
    prune();
    return true;
}

bool History::saveTranscript(const std::string &text) {
    return writeBytes(".txt", text.data(), text.size());
}

bool History::saveFailedAudio(const std::vector<uint8_t> &wav) {
    return writeBytes(".wav", wav.data(), wav.size());
}

std::vector<HistoryEntry> History::listEntries() const {
    std::vector<HistoryEntry> out;
    std::error_code ec;
    if (!std::filesystem::exists(dir_, ec)) {
        return out;
    }
    for (const auto &de : std::filesystem::directory_iterator(dir_, ec)) {
        if (!de.is_regular_file()) {
            continue;
        }
        std::string ext = de.path().extension().string();
        if (ext == ".txt") {
            out.push_back({de.path(), HistoryEntryKind::Transcript});
        } else if (ext == ".wav") {
            out.push_back({de.path(), HistoryEntryKind::FailedAudio});
        }
    }
    // Newest first: filenames are timestamps, so reverse-lexicographic.
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        return a.path.filename() > b.path.filename();
    });
    return out;
}

std::optional<HistoryEntry> History::latestTranscript() const {
    for (const auto &e : listEntries()) {
        if (e.kind == HistoryEntryKind::Transcript) {
            return e;
        }
    }
    return std::nullopt;
}

std::optional<HistoryEntry> History::latestFailedAudio() const {
    for (const auto &e : listEntries()) {
        if (e.kind == HistoryEntryKind::FailedAudio) {
            return e;
        }
    }
    return std::nullopt;
}

bool History::remove(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
}

void History::prune() {
    auto entries = listEntries(); // newest-first
    for (std::size_t i = maxEntries_; i < entries.size(); ++i) {
        std::error_code ec;
        std::filesystem::remove(entries[i].path, ec);
    }
}
