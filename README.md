# Fcitx5 Voice Input Module

A Fcitx5 module that enables voice input via a global hotkey. It's designed as a "Quick Phrase" style module, meaning it works on top of your existing Input Method Engine (IME) without needing to switch.

Press a hotkey, speak, and the recognized text will be committed to your currently focused application.

## High-level Architecture

- The module loads with Fcitx5 and registers a global hotkey (currently hardcoded to `F12`).
- When the hotkey is pressed, the module starts capturing 16 kHz mono PCM audio via PulseAudio into an in-memory WAV buffer and shows a "Listening…" indicator. Press `F12` again to finalize the buffer, or `Esc` to cancel and discard.
- The finalized WAV buffer is POSTed as `multipart/form-data` (field name `audio_file`) to a Whisper-compatible HTTP endpoint via libcurl, on a detached worker thread. The endpoint is currently hardcoded to `http://localhost:9000/asr?output=txt&encode=false`.
- Once the request returns, the transcribed text is dispatched back to the Fcitx event loop and committed to the active input context. Errors are logged via `FCITX_WARN`. The user cannot cancel an in-flight STT request.

## Prerequisites

### Runtime

- Linux with Fcitx5 installed and running.
- A PulseAudio-compatible sound server with a default record source (PipeWire's `pipewire-pulse` works too) — this is what `pa_simple_new` connects to for capture.
- A Whisper-compatible HTTP server reachable at the configured endpoint (see **Speech Recognition Server** below).

### Build

In addition to the runtime requirements:

- C++20 compiler, CMake ≥ 3.21, Ninja, pkg-config, gettext.
- Development headers for Fcitx5 (core/utils/config), PulseAudio, and libcurl.

On Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build gettext pkg-config \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev \
  libpulse-dev libcurl4-openssl-dev
```

### Speech Recognition Server

This module expects a [whisper-asr-webservice](https://github.com/ahmetoner/whisper-asr-webservice)-compatible HTTP server. By default it targets `http://localhost:9000/asr?output=txt&encode=false`:

- `output=txt` makes the server return the transcript as a plain text body (no JSON parsing required).
- `encode=false` skips the server-side ffmpeg re-encode step, because the addon already sends a 16 kHz mono S16LE WAV that Whisper accepts natively.

A quick way to bring one up locally is the upstream Docker image:

```bash
docker run -d -p 9000:9000 --name whisper \
  onerahmet/openai-whisper-asr-webservice:latest
```

The endpoint URL is currently hardcoded in `VoiceInputModule`'s constructor (`src/voiceinput-module.cpp`); making it user-configurable is tracked together with the hardcoded F12 hotkey.

## Directory Structure

```
fcitx5-voiceinput/
├── CMakeLists.txt
├── src/
│   ├── CMakeLists.txt
│   ├── voiceinput-module.{h,cpp}
│   ├── audio_capture.{h,cpp}      # PulseAudio capture into a WAV buffer
│   ├── wav_header.h               # 44-byte RIFF/PCM header builder
│   ├── speech_recognizer.{h,cpp}  # libcurl POST to Whisper-compatible HTTP STT
│   └── voiceinput.conf{,.in}
├── tests/
│   ├── CMakeLists.txt
│   ├── wav_header_test.cpp           # CTest target: ctest -R wav_header_test
│   └── speech_recognizer_test.cpp    # CTest target: ctest -R speech_recognizer_test
├── data/
└── ...
```

## Build and Install

See **Prerequisites** above for the packages you need to install first.

**1. Build**

The repo ships a `Makefile` wrapper that configures a fresh Debug build with Ninja and `CMAKE_INSTALL_PREFIX=/usr`. From the repo root:

```bash
make           # fresh debug build into ./build via cmake + ninja
make clean     # remove ./build
```

Or invoke CMake by hand if you want to control the build directory or flags:

```bash
mkdir build && cd build
cmake ..
make
```

**2. Install and Run**

```bash
sudo make install
fcitx5 -r # Restart fcitx5 to load the new addon
```

**3. Environment Setup**

For applications to see Fcitx5, you may need to set these environment variables:
```bash
export XMODIFIERS=@im=fcitx
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
```

## Usage

After installing the addon, restarting Fcitx5, and starting a Whisper-compatible server at the configured endpoint (see **Prerequisites → Speech Recognition Server** above):

1. Focus any text field.
2. Press **F12** — a "Listening…" indicator appears in the input panel.
3. Speak your phrase.
4. Press **F12** again — the indicator hides immediately and the recorded WAV is uploaded in the background. Within ~1–3 s (depending on the server and model size) the transcript is committed into the focused field.
5. To cancel while recording, press **Esc** — the buffer is discarded and nothing is uploaded. Once the request is in flight, it cannot be cancelled.

The addon logs to wherever your Fcitx5 instance logs (e.g. `journalctl --user -u fcitx5` on systemd setups, or its stderr if you started it manually). Useful lines:

- `voiceinput: captured N bytes` — capture finished, request fired. `N` should be roughly `44 + 32000 * seconds_spoken`.
- `voiceinput: STT error: …` — the server returned non-2xx or libcurl reported a transport error (e.g. connection refused if the Whisper server isn't running).

## Testing

Unit tests are built alongside the addon and registered with CTest:

```bash
cd build
ctest --output-on-failure          # run all tests
ctest -R wav_header_test           # run a single test by name
ctest -R speech_recognizer_test
```

Current coverage:

- `wav_header_test` — verifies the 44-byte RIFF/PCM header builder (`src/wav_header.h`).
- `speech_recognizer_test` — verifies the inline `trimTranscript()` helper that strips trailing whitespace from a Whisper text response (`src/speech_recognizer.h`).

The libcurl HTTP path and the Fcitx5 addon integration are not unit-tested; verify them manually using the steps under **Usage**. When adding new pure-function tests, follow the same mutation-check discipline used elsewhere in the project: briefly break the implementation, confirm the new test fails, then revert.

## Configuration

-   `src/voiceinput-addon.conf.in`: This is the addon descriptor file. It tells Fcitx5 the name of the module, its category (`Module`), and the library to load (`voiceinput`).
-   `src/voiceinput.conf`: This file defines user-configurable options that will appear in the Fcitx5 configuration tool, such as the hotkey to trigger voice input.

## Core Implementation Concepts

The module is implemented as a C++ class `VoiceInputModule` that inherits from `fcitx::AddonInstance`. The key API patterns are:

-   **Constructor:** The constructor receives a pointer to the main `fcitx::Instance` object. This pointer is stored as a member variable (`instance_`) and is essential for interacting with the rest of Fcitx.
-   **Factory:** Fcitx5 uses a factory pattern to create addons. A custom factory class (e.g., `VoiceInputModuleFactory`) must be defined. This factory's `create` method gets the `Instance` pointer from the `AddonManager` and passes it to the `VoiceInputModule`'s constructor.
-   **Factory Registration:** The `FCITX_ADDON_FACTORY_V2` macro is used to register the custom factory class with Fcitx.
-   **Event Handling:** The module uses `instance_->watchEvent()` to register a listener for key presses, which is used to detect the hotkey.

## Speech Recognizer Integration

The `SpeechRecognizer` class is an asynchronous HTTP client that posts a WAV buffer to a Whisper-compatible `/asr` endpoint and returns the transcript via callbacks. Each call to `transcribe()` spawns a detached worker thread, so the Fcitx event loop is never blocked on the network round-trip.

```cpp
class SpeechRecognizer {
public:
    using ResultCB = std::function<void(std::string)>;
    using ErrorCB  = std::function<void(std::string)>;

    explicit SpeechRecognizer(std::string endpoint);

    // Fire-and-forget. Callbacks fire on a worker thread; callers that need
    // main-thread delivery should wrap them with fcitx::EventDispatcher::schedule.
    void transcribe(std::vector<uint8_t> wav, ResultCB onResult, ErrorCB onError);
};
```

The server-side requirements and the default endpoint URL live under **Prerequisites → Speech Recognition Server**.

## Appendix: Useful Links

- [Fcitx5 Wiki: Basic Concepts](https://fcitx-im.org/wiki/Basic_concept)
- [Fcitx5 Wiki: Addon Types](https://fcitx-im.org/wiki/Addon_Type)
- [Fcitx5 on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)
- [ArchWiki Fcitx5 Setup](https://wiki.archlinux.org/title/Fcitx5)
