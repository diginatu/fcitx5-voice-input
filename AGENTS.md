## Agent Guide: fcitx5-voice-input

This file is for agentic coding tools (Cursor, Copilot agents, OpenAI, etc.) working in this repository.
Read this fully before making code or build changes.

---

### 1. Repository Overview

- C++20 project that implements a Fcitx5 addon for voice input.
- Build system is CMake; `Makefile` wraps configuration and Ninja builds.
- Main custom module lives in `src/` (`voiceinput-module.*`, `audio_capture.*`, `speech_recognizer.*`).
- Intended runtime flow: a global hotkey (currently hardcoded to `F12`) starts listening, audio is captured via PulseAudio, speech is recognized via `SpeechRecognizer`, and the final text is committed to the active input context.
- Addon descriptor template: `src/voiceinput.conf.in` (configured via CMake to produce `voiceinput.conf` for `addon/`).
- User-facing config description: `src/voiceinput.conf` (installed to `configdesc/`). NOTE: this file is currently a flat key=value stub, not a real Fcitx config descriptor — see Implementation Status.
- `conf/default.yaml` exists but is not loaded by anything; treat it as a placeholder until a config loader is added or remove it.
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

This project is in an early scaffolding state. The addon loads, registers the F12 hotkey, and toggles a status-area indicator, but the voice pipeline is not connected. Before designing changes, read the source — do not assume anything beyond the indicator works.

**Stubs (returning immediately, no behavior):**

- `src/audio_capture.cpp` — `AudioCapture::start()` / `stop()` are empty TODOs. PulseAudio (`libpulse-simple`) is linked but not used.
- `src/speech_recognizer.cpp` — `SpeechRecognizer::start()` / `stop()` are empty TODOs. cURL is linked but not used. No backend (Vosk, Whisper, cloud STT) has been chosen.

**Wiring gaps in `src/voiceinput-module.cpp`:**

- `recognizer_` (`std::unique_ptr<SpeechRecognizer>`) is declared but never constructed.
- The calls `recognizer_->start(...)` in `startListening()` and `recognizer_->stop()` in `cancel()` are commented out.
- `AudioCapture` is not instantiated or referenced anywhere in the module.
- `onSpeechResult()` exists but has no caller — no callback is wired from the (non-existent) recognizer back into it.

**Known correctness issues to fix when touching nearby code:**

- The key-event handler's `else` branch calls `filterAndAccept()` on every non-F12 key while inactive, which would swallow normal typing. The eat-keys behavior should only apply while `active_` is true.
- The hotkey is hardcoded to `FcitxKey_F12`. There is no `FCITX_CONFIGURATION` struct, no `getConfig()` / `setConfig()` override, so `voiceinput.conf`'s `Hotkey=` line is decorative. See `quickphrase/quickphrase.h`'s `QuickPhraseConfig` (`KeyListOption`) for the canonical pattern.
- `_()` is used for translatable strings, but no i18n domain is registered (`registerDomain()`) and no gettext catalog or `fcitx5_install_translation` call exists.
- `pkg_check_modules(PULSEAUDIO libpulse-simple)` in the root `CMakeLists.txt` is not `REQUIRED`, but `${PULSEAUDIO_LIBRARIES}` is linked unconditionally. Either mark it required or guard the link.
- `src/voiceinput.conf` is installed into `configdesc/` (`src/CMakeLists.txt`) but it is not in the format Fcitx expects for a config descriptor; the descriptor should be generated from a `Configuration` struct.

When implementing real behavior, prefer this order to minimize churn:

1. PulseAudio capture in `AudioCapture` (worker thread, push PCM frames).
2. Pick a recognizer backend, implement `SpeechRecognizer::start/stop`, deliver results via `ResultCB` on the Fcitx event loop (use `Instance::eventDispatcher()` to hop back from worker threads safely).
3. Construct `recognizer_` in `VoiceInputModule`'s constructor and uncomment the `start`/`stop` calls.
4. Fix the key-eating bug.
5. Add `FCITX_CONFIGURATION` for the hotkey; replace the stub `voiceinput.conf` with a generated descriptor.
6. Register an i18n domain (or drop `_()` until translations exist).

---

### 4. Tests, Linting, and Single-Test Runs

- There is currently no automated unit-test or integration-test suite in this repository.
- There is no configured static-analysis or lint target (no `.clang-format`, no CTest, no dedicated lint target in `CMakeLists.txt`).
- The main validation path today is manual functional testing inside a running Fcitx5 session:
  - Build and install the addon.
  - Restart Fcitx5 (`fcitx5 -r`).
  - Focus a text field, press the hotkey (currently hardcoded to `F12` — see Implementation Status), and verify behavior.
- Because no formal tests exist, there is no "run a single test" command at this time.

When adding tests in the future (e.g., via CTest or a separate test runner), prefer naming that makes it easy to run a single test case (e.g., `ctest -R VoiceInput_*`).

---

### 5. Agent-Specific Workflow Rules

- Treat this `AGENTS.md` as the single source of truth for agent behavior in this repo.
- Ask the user to execute the build when you need to verify behavior, instead of unilaterally running build or install commands yourself.
- When you suggest running commands, prefer `make`, `make clean`, and `make install` from the repo root.
- Treat `quickphrase/` sources as upstream vendor code; avoid style churn or refactors there unless the change is clearly required.
- Prefer implementing new voice-input behavior under `src/` and new configuration under `conf/`.
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
