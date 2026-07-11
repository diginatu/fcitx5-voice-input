# Voice-Input History/Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist voice-input history (successful transcripts always, failed-request audio only) so the user can recover a lost transcript or re-transcribe a recording without re-speaking.

**Architecture:** A pure `History` class (`src/history.{h,cpp}`, `std::filesystem` + `<fstream>`, no Fcitx/libcurl deps) writes one file per entry to `~/.local/share/fcitx5/voiceinput/history/`. `VoiceInputModule` owns a `History`, saves transcripts on success and WAVs on STT error, and (phases 2–3) adds recovery hotkeys and a candidate-list picker. Config gains `HistoryEnabled`, `HistorySize`, `RetryKey`, `RecommitKey`, `HistoryKey`.

**Tech Stack:** C++20, CMake + Ninja, Fcitx5 addon APIs, libcurl (existing), PulseAudio (existing), CTest.

## Global Constraints

- C++20 (`CMAKE_CXX_STANDARD 20`).
- Types `PascalCase`; methods `camelCase`; member vars `snake_case_` with trailing underscore.
- `#pragma once` in new headers; 4-space indent; braces on same line; ASCII only.
- New voice-input code goes under `src/`; new config options go in `VoiceInputConfig` (`src/voiceinput_config.h`).
- `History` must not depend on Fcitx or libcurl (so its test links nothing extra).
- No new external dependencies.
- Every new behavior is mutation-checked: break the impl, confirm the test fails, revert, then report the mutation evidence with the green result.
- Ask the user to run `make` / `make test` / `make install`; do not run build/install unilaterally. (Running `cmake`/`ctest` inside `build/` to verify a single unit test is acceptable when needed, but prefer asking.)
- History write failures log `FCITX_WARN` and never block committing text.

---

### Task 1: `History` component — save & read transcripts

**Files:**
- Create: `src/history.h`
- Create: `src/history.cpp`
- Test: `tests/history_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class HistoryEntryKind { Transcript, FailedAudio };`
  - `struct HistoryEntry { std::filesystem::path path; HistoryEntryKind kind; };`
  - `class History` constructed `History(std::filesystem::path dir, std::size_t maxEntries);`
  - `bool saveTranscript(const std::string &text);` — writes `<stamp>.txt`, returns false on I/O failure.
  - `bool saveFailedAudio(const std::vector<uint8_t> &wav);` — writes `<stamp>.wav`.
  - `std::vector<HistoryEntry> listEntries() const;` — newest-first.
  - `std::optional<HistoryEntry> latestTranscript() const;`
  - `std::optional<HistoryEntry> latestFailedAudio() const;`
  - `bool remove(const std::filesystem::path &path);`
  - Free helper for tests: `std::string readFile(const std::filesystem::path &);` (declared in header, `inline`).
- Note: the timestamp-name generator is a private detail. To make tests deterministic and collision-testing possible, expose a protected/overridable clock via constructor injection: add an optional 3rd ctor param `std::function<std::string()> stampFn` defaulting to a real local-time `YYYYmmdd-HHMMSS` generator. Collision handling appends `-1`, `-2`, ... when a file with the base name already exists.

- [ ] **Step 1: Write the failing test (save + read round-trip, both kinds)**

Create `tests/history_test.cpp`:

```cpp
#include "history.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
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
```

- [ ] **Step 2: Register the test in CMake**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(history_test history_test.cpp ${CMAKE_SOURCE_DIR}/src/history.cpp)
target_include_directories(history_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME history_test COMMAND history_test)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd build && cmake .. >/dev/null && cmake --build . --target history_test 2>&1 | tail -20`
Expected: FAIL to compile — `history.h` not found / `History` undefined.

- [ ] **Step 4: Write minimal implementation**

Create `src/history.h`:

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
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
```

Note: add `#include <sstream>` for `std::ostringstream` in the header.

Create `src/history.cpp`:

```cpp
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
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd build && cmake --build . --target history_test 2>&1 | tail -5 && ctest -R history_test --output-on-failure`
Expected: PASS — `history_test: OK`.

- [ ] **Step 6: Mutation-check**

Temporarily change `saveTranscript` to write `text.data(), 0` (empty), rebuild, run `ctest -R history_test` → expect FAIL. Revert. Then change the sort comparator to `<` (oldest-first), rebuild, run → the ordering assertions in later tasks will catch it; for now confirm `listEntries` still returns both entries. Revert. Record the failing output.

- [ ] **Step 7: Commit**

```bash
git add src/history.h src/history.cpp tests/history_test.cpp tests/CMakeLists.txt
git commit -m "feat(history): add History component with save/read round-trip"
```

---

### Task 2: `History` — ordering, pruning, collisions, latest/remove

**Files:**
- Test: `tests/history_test.cpp` (extend)

**Interfaces:**
- Consumes: `History` from Task 1.
- Produces: nothing new (hardens existing API).

- [ ] **Step 1: Add failing tests**

Insert these blocks into `main()` in `tests/history_test.cpp` before the final `std::cout`:

```cpp
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
```

- [ ] **Step 2: Run to verify they pass (impl already covers them)**

Run: `cd build && cmake --build . --target history_test 2>&1 | tail -5 && ctest -R history_test --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Mutation-check each new behavior**

For each, break the impl, rebuild, confirm FAIL, revert:
- Ordering: change `listEntries` comparator to `<` → ordering asserts FAIL.
- Prune: change loop start index from `maxEntries_` to `maxEntries_ + 10` → prune count asserts FAIL.
- Collision: change `allocatePath` to overwrite (return base path without the `exists` loop) → collision test's `size()==2` FAILS.
- latest: make `latestFailedAudio` return `latestTranscript()` → its assert FAILS.
- remove: make `remove` return `true` without deleting → post-remove `size()==1` FAILS.
Record the failing outputs.

- [ ] **Step 4: Commit**

```bash
git add tests/history_test.cpp
git commit -m "test(history): cover ordering, pruning, collisions, latest, remove"
```

---

### Task 3: Config options for history (Phase 1)

**Files:**
- Modify: `src/voiceinput_config.h`
- Modify: `tests/voiceinput_config_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `config.historyEnabled` (bool), `config.historySize` (int) on `VoiceInputConfig`.

- [ ] **Step 1: Add failing assertions**

In `tests/voiceinput_config_test.cpp`, after the prompt assertion (line 37), add:

```cpp
    // Defaults: history enabled, size 50.
    assert(config.historyEnabled.value() == true);
    assert(config.historySize.value() == 50);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd build && cmake --build . --target voiceinput_config_test 2>&1 | tail -20`
Expected: FAIL to compile — no member `historyEnabled`.

- [ ] **Step 3: Add the options**

In `src/voiceinput_config.h`, inside the `FCITX_CONFIGURATION(...)` list, after the `prompt` option (line 42, before the closing `);`), add:

```cpp
    fcitx::Option<bool> historyEnabled{
        this, "HistoryEnabled", "Save history for recovery", true};
    fcitx::Option<int> historySize{
        this, "HistorySize", "Max history entries",
        50, fcitx::IntConstrain(1)};
```

Add `#include <fcitx-config/option.h>` is already present; ensure `IntConstrain` is available (it comes from `fcitx-config/option.h`).

- [ ] **Step 4: Run to verify it passes**

Run: `cd build && cmake --build . --target voiceinput_config_test 2>&1 | tail -5 && ctest -R voiceinput_config_test --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Mutation-check**

Change the `historySize` default to `10`, rebuild, run → FAIL. Revert. Change `historyEnabled` default to `false`, rebuild, run → FAIL. Revert. Record outputs.

- [ ] **Step 6: Commit**

```bash
git add src/voiceinput_config.h tests/voiceinput_config_test.cpp
git commit -m "feat(config): add HistoryEnabled and HistorySize options"
```

---

### Task 4: Wire `History` into the module (Phase 1)

**Files:**
- Modify: `src/voiceinput-module.h`
- Modify: `src/voiceinput-module.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `History` (Task 1), `config.historyEnabled` / `config.historySize` (Task 3).
- Produces: module saves transcripts on success and WAVs on STT error. No new public API.

This task has no unit test (it touches the Fcitx addon path, verified manually per the spec). Keep the change minimal and mechanical.

- [ ] **Step 1: Add `history.cpp` to the addon build**

In `src/CMakeLists.txt`, add `history.cpp` to the `add_library(voiceinput MODULE ...)` source list:

```cmake
add_library(voiceinput MODULE
  voiceinput-module.cpp
  speech_recognizer.cpp
  audio_capture.cpp
  history.cpp
)
```

- [ ] **Step 2: Declare the `History` member and helper**

In `src/voiceinput-module.h`:
- Add `#include "history.h"` near the top includes (after `#include "voiceinput_config.h"`).
- Add a forward-nothing (History is a concrete type) member in the private section, after `audioCapture_`:

```cpp
  std::unique_ptr<History> history_;
```

- [ ] **Step 3: Build the History from config and keep the WAV on error**

In `src/voiceinput-module.cpp`:

Add a file-local helper in the anonymous namespace (after `makeRecognizer`):

```cpp
// Build the History from config: resolves the data dir under the Fcitx
// standard path so $XDG_DATA_HOME is respected.
std::unique_ptr<History> makeHistory(const VoiceInputConfig &config) {
  if (!config.historyEnabled.value()) {
    return nullptr;
  }
  std::filesystem::path dir =
      fcitx::StandardPath::global().userDirectory(
          fcitx::StandardPath::Type::Data);
  dir /= "fcitx5/voiceinput/history";
  auto size = static_cast<std::size_t>(std::max(1, config.historySize.value()));
  return std::make_unique<History>(std::move(dir), size);
}
```

Add includes at the top of the `.cpp`: `#include "history.h"`, `#include <fcitx-utils/standardpath.h>`, `#include <algorithm>`, `#include <filesystem>`.

In the constructor, after `recognizer_ = makeRecognizer(config_);` add:

```cpp
  history_ = makeHistory(config_);
```

In `setConfig`, after `recognizer_ = makeRecognizer(config_);` add:

```cpp
  history_ = makeHistory(config_);
```

- [ ] **Step 4: Save transcript on success**

In `onSpeechResult`, after the `commitString` block (after line 139), add:

```cpp
  if (history_) {
    if (!history_->saveTranscript(text)) {
      FCITX_WARN() << "voiceinput: failed to save transcript to history";
    }
  }
```

- [ ] **Step 5: Save WAV on STT error**

In `onCaptureComplete`, the `recognizer_->transcribe(...)` call currently moves `wav` into `transcribe`. To keep a copy for the error path, capture a copy in the error lambda. Change the `transcribe` call so the error callback has access to the bytes:

```cpp
  recognizer_->transcribe(
      wav, // pass by copy; keep our own for the error path below
      [this](std::string text) {
        dispatcher_.schedule(
            [this, text = std::move(text)]() { onSpeechResult(text); });
      },
      [this, wav](std::string err) {
        dispatcher_.schedule([this, err = std::move(err), wav]() {
          if (history_) {
            if (!history_->saveFailedAudio(wav)) {
              FCITX_WARN() << "voiceinput: failed to save audio to history";
            }
          }
          FCITX_WARN() << "voiceinput: STT error: " << err;
          showIndicator("Error: " + err);
          struct timespec ts;
          clock_gettime(CLOCK_MONOTONIC, &ts);
          uint64_t usec = static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
                          static_cast<uint64_t>(ts.tv_nsec) / 1000ULL +
                          3'000'000ULL;
          errorTimer_ = instance_->eventLoop().addTimeEvent(
              CLOCK_MONOTONIC, usec, 0,
              [this](fcitx::EventSourceTime *, uint64_t) -> bool {
                hideIndicator();
                return true;
              });
        });
      });
```

Note: `transcribe`'s first parameter is `std::vector<uint8_t>` taken by value in the recognizer, so passing `wav` (an lvalue) copies it; the outer `wav` remains valid to capture. Remove the earlier `std::move(wav)` on that argument. `onCaptureComplete` receives `wav` by value, so capturing it by copy in the lambda is safe.

- [ ] **Step 6: Ask the user to build & smoke-test**

Ask the user to run `make` and confirm it compiles. Then the manual test plan (spec Section 5, steps 1–3): with the STT server down, record → a `.wav` appears under `~/.local/share/fcitx5/voiceinput/history/`; with the server up, a successful record leaves only a `.txt`.

- [ ] **Step 7: Commit**

```bash
git add src/voiceinput-module.h src/voiceinput-module.cpp src/CMakeLists.txt
git commit -m "feat(history): save transcripts on success and audio on STT error"
```

---

### Task 5: Config options for recovery hotkeys (Phase 2)

**Files:**
- Modify: `src/voiceinput_config.h`
- Modify: `tests/voiceinput_config_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `config.retryKey`, `config.recommitKey` (`KeyListOption`, default empty).

- [ ] **Step 1: Add failing assertions**

In `tests/voiceinput_config_test.cpp`, after the history-size assertion from Task 3, add:

```cpp
    // Defaults: retry/recommit keys unbound.
    assert(config.retryKey.value().empty());
    assert(config.recommitKey.value().empty());
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd build && cmake --build . --target voiceinput_config_test 2>&1 | tail -20`
Expected: FAIL to compile — no member `retryKey`.

- [ ] **Step 3: Add the options**

In `src/voiceinput_config.h`, after `historySize`, add:

```cpp
    fcitx::KeyListOption retryKey{
        this, "RetryKey", "Retry last failed recording", {},
        fcitx::KeyListConstrain({fcitx::KeyConstrainFlag::AllowModifierLess})};
    fcitx::KeyListOption recommitKey{
        this, "RecommitKey", "Re-commit last transcript", {},
        fcitx::KeyListConstrain({fcitx::KeyConstrainFlag::AllowModifierLess})};
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd build && cmake --build . --target voiceinput_config_test 2>&1 | tail -5 && ctest -R voiceinput_config_test --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Mutation-check**

Change `retryKey` default to `{fcitx::Key{FcitxKey_F11}}`, rebuild, run → the `.empty()` assert FAILS. Revert. Record output.

- [ ] **Step 6: Commit**

```bash
git add src/voiceinput_config.h tests/voiceinput_config_test.cpp
git commit -m "feat(config): add RetryKey and RecommitKey options"
```

---

### Task 6: Recovery hotkey handling (Phase 2)

**Files:**
- Modify: `src/voiceinput-module.h`
- Modify: `src/voiceinput-module.cpp`

**Interfaces:**
- Consumes: `History::latestFailedAudio`, `History::latestTranscript`, `History::remove`, `readFile`; `config.retryKey`, `config.recommitKey`.
- Produces: private methods `void retryLast();` and `void recommitLast();`.

This task touches the Fcitx event path; verified manually. Follow the existing `registerEventWatchers` and `onCaptureComplete` patterns exactly.

- [ ] **Step 1: Declare the methods**

In `src/voiceinput-module.h` private section, after `void cancel();`, add:

```cpp
  void retryLast();
  void recommitLast();
```

- [ ] **Step 2: Handle the keys in the watcher**

In `registerEventWatchers`, extend the handler. The current structure claims the activation key, then handles active-state keys. Add, in the branch where `!active_` (i.e. not currently recording), matching before the activation-key block's else. Concretely, inside the lambda, after the activation-key `if/else` and before the `else if (active_)`, insert a top-level check for the recovery keys guarded by `!active_`:

```cpp
        } else if (!active_ && !ke.isRelease() &&
                   ke.key().checkKeyList(config_.retryKey.value())) {
          ke.filterAndAccept();
          retryLast();
        } else if (!active_ && !ke.isRelease() &&
                   ke.key().checkKeyList(config_.recommitKey.value())) {
          ke.filterAndAccept();
          recommitLast();
        } else if (active_) {
```

(That is: convert the existing `else if (active_)` into the chain above, keeping the existing `active_` body as the final `else if`.)

- [ ] **Step 3: Implement `recommitLast`**

Add to `src/voiceinput-module.cpp` (after `cancel()`):

```cpp
void VoiceInputModule::recommitLast() {
  if (!history_) {
    return;
  }
  auto entry = history_->latestTranscript();
  if (!entry) {
    showTransientError("No history");
    return;
  }
  std::string text = readFile(entry->path);
  if (auto *ic = instance_->inputContextManager().mostRecentInputContext()) {
    ic->commitString(text);
  }
}
```

- [ ] **Step 4: Implement `retryLast`**

```cpp
void VoiceInputModule::retryLast() {
  if (!history_) {
    return;
  }
  auto entry = history_->latestFailedAudio();
  if (!entry) {
    showTransientError("No history");
    return;
  }
  std::string data = readFile(entry->path);
  std::vector<uint8_t> wav(data.begin(), data.end());
  std::filesystem::path wavPath = entry->path;
  showIndicator("Transcribing…");
  recognizer_->transcribe(
      wav,
      [this, wavPath](std::string text) {
        dispatcher_.schedule([this, text = std::move(text), wavPath]() {
          onSpeechResult(text);
          if (history_) {
            history_->remove(wavPath); // retry succeeded; drop the audio
          }
        });
      },
      [this](std::string err) {
        dispatcher_.schedule([this, err = std::move(err)]() {
          FCITX_WARN() << "voiceinput: retry STT error: " << err;
          showTransientError("Error: " + err);
        });
      });
}
```

- [ ] **Step 5: Extract the transient-error helper**

The 3-second auto-hide error indicator is duplicated. Add a private helper `void showTransientError(const std::string &text);` (declare in header after `hideIndicator`) and move the timer logic there:

```cpp
void VoiceInputModule::showTransientError(const std::string &text) {
  showIndicator(text);
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t usec = static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
                  static_cast<uint64_t>(ts.tv_nsec) / 1000ULL + 3'000'000ULL;
  errorTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, usec, 0,
      [this](fcitx::EventSourceTime *, uint64_t) -> bool {
        hideIndicator();
        return true;
      });
}
```

Then replace the inline timer block in `onCaptureComplete`'s error lambda with `showTransientError("Error: " + err);` (keeping the `saveFailedAudio` and `FCITX_WARN` lines before it).

- [ ] **Step 6: Ask the user to build & manual-test**

Ask the user to run `make`, then spec Section 5 steps 2 and 4: with a saved `.wav`, press `RetryKey` → transcript commits, `.txt` appears, `.wav` removed; press `RecommitKey` in another field → newest transcript re-committed. Unbound keys do nothing; keys with no entry show "No history".

- [ ] **Step 7: Commit**

```bash
git add src/voiceinput-module.h src/voiceinput-module.cpp
git commit -m "feat(history): add retry and re-commit recovery hotkeys"
```

---

### Task 7: History picker UI (Phase 3)

**Files:**
- Modify: `src/voiceinput_config.h`
- Modify: `tests/voiceinput_config_test.cpp`
- Modify: `src/voiceinput-module.h`
- Modify: `src/voiceinput-module.cpp`

**Interfaces:**
- Consumes: `History::listEntries`, `readFile`, retry flow from Task 6.
- Produces: `config.historyKey`; a candidate-list picker opened by that key.

This is the largest UI task. Reference `quickphrase/quickphrase.cpp` for candidate-list construction and key handling. Verified manually.

- [ ] **Step 1: Add `HistoryKey` config + failing test**

In `tests/voiceinput_config_test.cpp`, after the recommit-key assertion, add:

```cpp
    // Default: history picker key unbound.
    assert(config.historyKey.value().empty());
```

In `src/voiceinput_config.h`, after `recommitKey`, add:

```cpp
    fcitx::KeyListOption historyKey{
        this, "HistoryKey", "Open history picker", {},
        fcitx::KeyListConstrain({fcitx::KeyConstrainFlag::AllowModifierLess})};
```

- [ ] **Step 2: Run config test — fail then pass**

Run: `cd build && cmake --build . --target voiceinput_config_test 2>&1 | tail -20` (FAIL: no `historyKey`), then add the option, rebuild, `ctest -R voiceinput_config_test` (PASS). Mutation-check: set a non-empty default → `.empty()` asserts FAIL; revert.

- [ ] **Step 3: Commit the config change**

```bash
git add src/voiceinput_config.h tests/voiceinput_config_test.cpp
git commit -m "feat(config): add HistoryKey option for the picker"
```

- [ ] **Step 4: Build the candidate list**

Add a private method `void openHistoryPicker();` (declare in header). Implement using Fcitx's `CommonCandidateList`:

```cpp
void VoiceInputModule::openHistoryPicker() {
  if (!history_) {
    return;
  }
  auto entries = history_->listEntries();
  if (entries.empty()) {
    showTransientError("No history");
    return;
  }
  auto *ic = instance_->inputContextManager().mostRecentInputContext();
  if (!ic) {
    return;
  }
  auto list = std::make_unique<fcitx::CommonCandidateList>();
  list->setPageSize(10);
  for (const auto &e : entries) {
    std::string label;
    std::function<void()> action;
    if (e.kind == HistoryEntryKind::Transcript) {
      std::string text = readFile(e.path);
      label = text.substr(0, 40);
      action = [this, text]() {
        if (auto *ic = instance_->inputContextManager()
                           .mostRecentInputContext()) {
          ic->commitString(text);
        }
      };
    } else {
      label = "[failed recording " + e.path.stem().string() + "]";
      std::filesystem::path path = e.path;
      action = [this, path]() { retryEntry(path); };
    }
    list->append<HistoryCandidateWord>(fcitx::Text(label), std::move(action));
  }
  ic->inputPanel().reset();
  ic->inputPanel().setCandidateList(std::move(list));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}
```

Define a small `HistoryCandidateWord : fcitx::CandidateWord` in the `.cpp` anonymous namespace that stores the `std::function<void()>` and calls it in `select(InputContext*)`, then hides the panel. Reference `quickphrase.cpp` for the exact `CandidateWord` subclass shape.

Add `retryEntry(const std::filesystem::path &)` by refactoring `retryLast` to call `retryEntry(entry->path)` (retry-by-path), so the picker reuses it.

- [ ] **Step 5: Open the picker on `HistoryKey`, and let candidate keys work**

In `registerEventWatchers`, add another `!active_` branch (like Task 6) matching `config_.historyKey` that calls `openHistoryPicker()`. When a candidate list is visible, forward number/arrow/Escape keys to it — follow `quickphrase`'s handling: on a digit, `candidateList->candidate(idx).select(ic)`; on Escape, `inputPanel().reset()` and update UI. Guard this so it only runs when `ic->inputPanel().candidateList()` is non-null.

- [ ] **Step 6: Ask the user to build & manual-test**

Ask the user to run `make`, then spec Section 5 step 5: press `HistoryKey` → entries list newest-first, transcripts show ~40-char preview, failed recordings show `[failed recording <stamp>]`; selecting commits/retries; Escape closes.

- [ ] **Step 7: Commit**

```bash
git add src/voiceinput-module.h src/voiceinput-module.cpp
git commit -m "feat(history): add candidate-list history picker"
```

---

### Task 8: Documentation

**Files:**
- Modify: `README.md` (and/or `docs/README.md` if that is the user-facing doc)
- Modify: `CLAUDE.md` (Implementation Status section)

**Interfaces:** none.

- [ ] **Step 1: Update README**

Document the history feature: what is saved (transcripts always; failed-request audio only), where (`~/.local/share/fcitx5/voiceinput/history/`), the `HistoryEnabled` / `HistorySize` settings, and the `RetryKey` / `RecommitKey` / `HistoryKey` hotkeys (unbound by default; assign in fcitx5-configtool).

- [ ] **Step 2: Update CLAUDE.md**

Add `src/history.{h,cpp}` and `tests/history_test.cpp` to the Implementation Status and Tests sections, and the new config options to the `voiceinput_config.h` description. Note the error-path WAV-copy memory cost.

- [ ] **Step 3: Commit**

```bash
git add README.md docs/README.md CLAUDE.md
git commit -m "docs: document voice-input history/recovery feature"
```

---

## Self-Review

**Spec coverage:**
- Two recovery goals (failed STT audio, lost transcript) → Tasks 4, 6. ✓
- Audio only on failure → Task 4 Step 5. ✓
- On-disk format / location / collision → Tasks 1–2. ✓
- `History` API (save/list/latest/remove/prune) → Tasks 1–2. ✓
- Module integration + `setConfig` rebuild → Task 4. ✓
- `HistoryEnabled` / `HistorySize` → Task 3. ✓
- Retry / re-commit hotkeys → Tasks 5–6. ✓
- Picker UI → Task 7. ✓
- Unit tests + mutation checks → Tasks 1–3, 5, 7. ✓
- Config-default tests → Tasks 3, 5, 7. ✓
- Manual test plan → Tasks 4, 6, 7 build steps. ✓
- Known memory limitation documented → Task 8. ✓

**Placeholder scan:** No TBD/TODO; all code shown. ✓

**Type consistency:** `History` ctor `(path, size_t, StampFn=default)`, `saveTranscript`/`saveFailedAudio`/`listEntries`/`latestTranscript`/`latestFailedAudio`/`remove` consistent across Tasks 1–7. `showTransientError`, `retryLast`/`retryEntry`, `recommitLast`, `openHistoryPicker` consistent in Tasks 6–7. `readFile` header helper used in Tasks 1, 6, 7. ✓

**Note for the implementer:** Verify `fcitx::StandardPath` / `CommonCandidateList` / `CandidateWord` API names against the installed Fcitx5 headers before relying on them; if `StandardPath` is deprecated in the installed version, use `fcitx::StandardPaths` accordingly. This is the one place where the plan may need adjustment to the local Fcitx5 version.
