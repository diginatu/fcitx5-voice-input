## Agent Guide: fcitx5-voice-input

This file is for agentic coding tools (Cursor, Copilot agents, OpenAI, etc.) working in this repository.
Read this fully before making code or build changes.

---

### 1. Repository Overview

- C++20 project that implements a Fcitx5 addon for voice input.
- Build system is CMake; `Makefile` wraps configuration and Ninja builds.
- Main custom module lives in `src/` (`voiceinput-module.*`, `audio_capture.*`, `speech_recognizer.*`, `wav_header.h`).
- Runtime flow: a configurable hotkey (default `F12`) starts listening, audio is captured via PulseAudio into an in-memory WAV buffer (`lastRecording_`), the buffer is POSTed as `multipart/form-data` (field `audio_file`) to a Whisper-compatible HTTP endpoint via libcurl on a detached worker thread (default `http://localhost:9000/asr?output=txt&encode=false`), and the resulting transcript is dispatched back to the Fcitx event loop and committed to the active input context.
- Addon descriptor template: `src/voiceinput.conf.in` (configured via CMake to produce `voiceinput.conf` for `addon/`).
- User-facing configuration schema: defined in C++ via `FCITX_CONFIGURATION` in `src/voiceinput_config.h` (`VoiceInputConfig` — `ActivationKey`, `CancelKey`, `Endpoint`). Exposed at runtime via `VoiceInputModule::getConfig()` / `setConfig()`; persisted to `~/.config/fcitx5/conf/voiceinput.conf` via `readAsIni` / `safeSaveAsIni`.
- The `quickphrase/` directory is vendored code from upstream Fcitx kept as a reference implementation. It is NOT wired into the root `CMakeLists.txt` and is not built — do not assume changes there have any effect on the produced addon.
- Icon assets live under `data/icons/` (e.g. `microphone.svg`).

Always skim `README.md` and this `AGENTS.md` file before larger edits.

---

### 2. Build, Clean, Install

Important: building and installing the addon typically requires dev libraries and sometimes root permissions.
Agents should ask the user to run builds when possible rather than executing them directly.

**Prerequisites (Debian/Ubuntu example)**

```bash
sudo apt install build-essential cmake ninja-build gettext pkg-config \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev \
  libpulse-dev libcurl4-openssl-dev
```

**Preferred build commands (from repo root)**

- Fresh debug build via `Makefile` (recommended during development):

```bash
make          # runs the "build" target
```

- The `build` target performs:

```bash
rm -fr build && mkdir build && cd build && \
  cmake -G Ninja .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 && \
  ninja
```

- Clean build directory:

```bash
make clean
```

- Install (requires root, installs into `/usr`):

```bash
make install
```

**Alternative manual CMake build (without `Makefile`)**

```bash
mkdir -p build
cd build
cmake ..
make
```

**After installing**

```bash
fcitx5 -r   # restart Fcitx5 so it picks up the new addon
```

Agents should not assume `fcitx5` is available on the host; mention this to the user if suggesting runtime tests.

---

### 3. Current Implementation Status

The addon loads, registers a configurable activation hotkey (default `F12`), shows a status indicator, records audio in memory via PulseAudio, uploads the WAV to a Whisper-compatible HTTP endpoint, and commits the returned transcript to the focused input context. Before designing changes, read the source — do not assume anything beyond the documented state works.

**Implemented:**

- `src/audio_capture.{h,cpp}` — real PulseAudio capture using `pa_simple_*` on a worker thread. `start()` opens an `S16LE / 16 kHz / mono` record stream; `stop()` joins the worker and returns a WAV-wrapped `std::vector<uint8_t>` (RIFF header + PCM payload) ready for upload.
- `src/wav_header.h` — pure `buildWavHeader(pcmByteCount, sampleRate, channels, bitsPerSample)` returning the 44-byte RIFF/fmt/data header. Unit-tested via `tests/wav_header_test.cpp`.
- `src/speech_recognizer.{h,cpp}` — async HTTP client. `transcribe(wav, onResult, onError)` spawns a detached `std::thread` per request that POSTs the WAV via libcurl's easy + mime API (multipart field `audio_file`, MIME `audio/wav`) with a 60s timeout. The static worker takes everything by value so the recognizer object can be destroyed while a request is in flight. `curl_global_init` is guarded by `std::once_flag`. The header also exposes an inline `trimTranscript()` helper (trailing-whitespace strip) so tests don't need to link libcurl. Unit-tested via `tests/speech_recognizer_test.cpp`.
- `src/voiceinput_config.h` — `VoiceInputConfig` declared via `FCITX_CONFIGURATION`, exposing three options: `ActivationKey` (`KeyListOption`, default `[F12]`), `CancelKey` (`KeyListOption`, default `[Escape]`), `Endpoint` (`Option<std::string>`, default `http://localhost:9000/asr?output=txt&encode=false`). Header-only so `tests/voiceinput_config_test.cpp` exercises it without linking the full addon.
- `src/voiceinput-module.{h,cpp}` — constructs `audioCapture_` and `recognizer_` (with the endpoint from `config_`) in the ctor; `reloadConfig()` reads `conf/voiceinput.conf` from the Fcitx config dir via `readAsIni`; `setConfig()` persists via `safeSaveAsIni` and rebuilds `recognizer_` so URL changes take effect immediately (any in-flight request keeps its old URL by value). `getConfig()` returns `&config_` so Fcitx's config tool can introspect the schema at runtime — no `configdesc/` file is installed. `startListening()` opens the capture; pressing the activation key while active runs `finishRecording()`, which hides the indicator, moves the WAV into `recognizer_->transcribe(...)`, and wraps both callbacks with `instance_->eventDispatcher().schedule(...)` so the result hits the Fcitx main thread before reaching `onSpeechResult()` (which commits via `InputContext::commitString`). The cancel key while active runs `cancel()`, which stops capture and discards the buffer; the cancel key during the STT request has no effect (the user cannot cancel an in-flight upload — by design). Key matching uses `key().checkKeyList(config_.X.value())`, mirroring `quickphrase/quickphrase.cpp:84`.
- Root `CMakeLists.txt` — `pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)`, `find_package(CURL REQUIRED)`, `enable_testing()`, `add_subdirectory(tests)`. `src/CMakeLists.txt` unconditionally links `CURL::libcurl`.

**Known correctness issues to fix when touching nearby code:**

- `_()` is not used anywhere yet; config labels are plain English. No i18n domain is registered (`registerDomain()`) and no gettext catalog or `fcitx5_install_translation` call exists. Add the domain before introducing `_()` macros.
- There is no user-visible "Transcribing…" indicator between `finishRecording()` and the result callback — the indicator is hidden immediately on the second hotkey press.
- STT errors are only logged via `FCITX_WARN`; the user gets no on-screen feedback when the server is unreachable or returns non-2xx.

Suggested next steps (rough order, to minimize churn):

1. Show a "Transcribing…" indicator while the HTTP request is in flight and a brief error indicator on failure.
2. Register an i18n domain and wrap user-visible strings (including config labels in `VoiceInputConfig`) in `_()`.
3. Decide whether to support cancelling an in-flight STT request (currently not supported by design — would require libcurl multi + a progress callback).

---

### 4. Tests, Linting, and Single-Test Runs

- CTest is enabled at the root `CMakeLists.txt`. Test sources live under `tests/`.
- Current coverage:
  - `tests/wav_header_test.cpp` exercises `src/wav_header.h` (magic bytes, sample rate, channels, bit depth, byte rate, block align, RIFF/data sizes).
  - `tests/speech_recognizer_test.cpp` exercises `trimTranscript()` from `src/speech_recognizer.h` (trailing whitespace stripping; leading whitespace preserved; empty/all-whitespace edge cases). The test only includes the header — it does not link libcurl — so keep any new test-only helpers `inline` in the header.
  - `tests/voiceinput_config_test.cpp` exercises `VoiceInputConfig` defaults (`ActivationKey=F12`, `CancelKey=Escape`, the documented endpoint URL) and a simple `RawConfig` round-trip for the endpoint. Links `Fcitx5::Core` + `Fcitx5::Utils`.
  - Pure-function tests like these should also be mutation-checked (briefly break the implementation, confirm the test fails, revert) per the global TDD guideline.
- PulseAudio capture, the libcurl POST, and the Fcitx5 addon path are still verified manually inside a running Fcitx5 session:
  - Make sure a Whisper-compatible server is reachable at the configured endpoint (default `http://localhost:9000/asr?output=txt&encode=false`).
  - Build and install the addon.
  - Restart Fcitx5 (`fcitx5 -r`).
  - (Optional) Open `fcitx5-configtool` → Addons → Voice Input → Configure to change the activation key, cancel key, or endpoint URL. Changes persist to `~/.config/fcitx5/conf/voiceinput.conf`.
  - Focus a text field, press the activation key (default F12), speak, press the activation key again. Confirm the `FCITX_INFO` log shows a recorded byte count roughly equal to `44 + 32000 * seconds_spoken`. Within ~1–3s the transcript should be committed into the focused field. STT errors appear as `voiceinput: STT error: …` via `FCITX_WARN`. The cancel key (default Escape) during recording cancels and discards the buffer; the cancel key during an in-flight STT request has no effect.
- No `.clang-format` or static-analysis target is configured.

**Running tests**

```bash
cd build
ctest --output-on-failure        # all tests
ctest -R wav_header_test         # single test by name
```

When adding new tests, place them under `tests/`, register them in `tests/CMakeLists.txt`, and prefer naming that makes it easy to run a single test case (e.g., `ctest -R VoiceInput_*`).

---

### 5. Agent-Specific Workflow Rules

- Treat this `AGENTS.md` as the single source of truth for agent behavior in this repo.
- Ask the user to execute the build when you need to verify behavior, instead of unilaterally running build or install commands yourself.
- When you suggest running commands, prefer `make`, `make clean`, and `make install` from the repo root.
- Treat `quickphrase/` sources as upstream vendor code; avoid style churn or refactors there unless the change is clearly required.
- Prefer implementing new voice-input behavior under `src/`. New user-facing configuration options should be added to `VoiceInputConfig` in `src/voiceinput_config.h`; they are automatically surfaced in `fcitx5-configtool` and persisted to `~/.config/fcitx5/conf/voiceinput.conf`.
- Do not add new external dependencies without calling that out explicitly to the user and updating `CMakeLists.txt` and installation docs.

As of this revision there are no Cursor rules under `.cursor/rules/` or `.cursorrules`, and no GitHub Copilot instruction file at `.github/copilot-instructions.md`.

---

### 6. C++ Code Style and Conventions

The style is based on upstream Fcitx5 modules (see `quickphrase/*.cpp`) with a few project-local patterns (see `src/voiceinput-module.*`).
Match the existing surrounding code rather than enforcing a new global style.

**Language and general guidelines**

- Use C++20 (`CMAKE_CXX_STANDARD 20`).
- Prefer RAII and smart pointers (`std::unique_ptr`, `std::vector`) over raw `new`/`delete`.
- Avoid exceptions for normal control flow; many Fcitx APIs are not exception-aware.
- Keep translation and UI strings wrapped in `fcitx-utils/i18n.h` macros (`_()`).

**Includes and headers**

- Use `#pragma once` for include guards in new project headers (consistent with `src/*.h`).
- Order includes roughly as:
  - Matching header (for `.cpp` files).
  - Fcitx headers.
  - Standard library headers.
  - Local project headers.
- In `quickphrase/`, keep the existing `#ifndef/#define` guard pattern; do not convert those files to `#pragma once`.

**Namespaces and ownership**

- Fcitx core types live in namespace `fcitx`.
- The `QuickPhrase`-related classes live fully inside `namespace fcitx { ... }`.
- The voice-input module currently declares `VoiceInputModule`, `SpeechRecognizer`, and `AudioCapture` in the global namespace. Only the `VoiceInputModuleFactory` lives inside `namespace fcitx { ... }`. Keep new related types consistent with that choice unless you perform a coordinated refactor.
- For addon classes, inherit from `fcitx::AddonInstance` and register them using `FCITX_ADDON_FACTORY_V2` as shown in `voiceinput-module.cpp` and `quickphrase.cpp`.

**Naming conventions**

- Types: `PascalCase` (`VoiceInputModule`, `SpeechRecognizer`, `AudioCapture`).
- Methods and functions: `camelCase` starting with a lowercase letter (`startListening`, `onSpeechResult`, `registerEventWatchers`).
- Member variables: `snake_case` with a trailing underscore (`instance_`, `eventHandlers_`, `active_`).
- Boolean members: name them descriptively (`active_`, `enabled_`), not negated (`!inactive`).
- Configuration structs in Fcitx often use `PascalCase` type names and members aligned with option names (see `QuickPhraseConfig`).

**Formatting**

- Indentation in upstream Fcitx code is four spaces; new code in this repo should follow that where it touches or extends Fcitx-style files.
- Braces generally stay on the same line as the control statement or function name.
- Prefer early returns for invalid states or null pointers (as seen in `VoiceInputModule::onSpeechResult`).
- Keep lines reasonably short; wrap long argument lists vertically, aligning continuation lines under the first argument where practical.
- Do not introduce tabs; stick to spaces.

**Error handling and robustness**

- Do not crash inside Fcitx event handlers; always validate pointers and state.
- Follow the `SpeechRecognizer::ErrorCB` pattern for reporting recognition failures; a good default is to hide the indicator and keep the input context usable.
- When committing text to the input context, always check that an input context is available:

```cpp
if (auto *ic = instance_->inputContextManager().mostRecentInputContext()) {
    ic->commitString(text);
}
```

- Use `filter()`, `accept()`, and `filterAndAccept()` on `KeyEvent` objects carefully so keys are not leaked to other handlers when the addon has claimed them.
- Treat `quickphrase` providers as a reference implementation for more complex event sequences and UI updates.

---

### 7. Fcitx-Specific Patterns to Follow

- Register key-event watchers via `Instance::watchEvent`, with appropriate `EventWatcherPhase`.
- For features that maintain per-input-context state, use `InputContextProperty` and factories, as in `QuickPhraseState`.
- Use `InputContext::inputPanel()` and `updateUserInterface(UserInterfaceComponent::InputPanel)` when showing candidate lists or preedit text.
- For simple status indicators, follow the pattern in `VoiceInputModule::showIndicator` and `hideIndicator`, using `fcitx::SimpleAction` in the `StatusArea`.

---

### 8. When Editing or Adding Files

- Stick to ASCII in source files unless you have a specific reason to use non-ASCII and the surrounding file already does so.
- Add SPDX-style license headers only if you are aligning with existing upstream practice and the file currently uses such a header.
- Prefer small, focused changes over large refactors, especially inside `quickphrase/`.
- When adding new functionality, update `README.md` or `docs/README.md` if user-visible behavior changes.
- Coordinate changes in `CMakeLists.txt` (root and `src/` or `quickphrase/`) so new sources and dependencies are correctly built and installed.

If you are unsure whether a change might affect the running desktop environment (e.g., Fcitx input behavior), explain the impact to the user and suggest a manual test plan instead of guessing.
