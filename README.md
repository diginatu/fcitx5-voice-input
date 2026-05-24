# Fcitx5 Voice Input Module

A Fcitx5 module that enables voice input via a global hotkey. It's designed as a "Quick Phrase" style module, meaning it works on top of your existing Input Method Engine (IME) without needing to switch.

Press a hotkey, speak, and the recognized text will be committed to your currently focused application.

## High-level Architecture

- The module loads with Fcitx5 and registers a global hotkey (currently hardcoded to `F12`).
- When the hotkey is pressed, the module starts capturing 16 kHz mono PCM audio via PulseAudio into an in-memory WAV buffer and shows a "Listening…" indicator. Press `F12` again to finalize the buffer, or `Esc` to cancel and discard.
- The finalized WAV buffer is POSTed as `multipart/form-data` (field name `audio_file`) to a Whisper-compatible HTTP endpoint via libcurl, on a detached worker thread. The endpoint is currently hardcoded to `http://localhost:9000/asr?output=txt&encode=false`.
- Once the request returns, the transcribed text is dispatched back to the Fcitx event loop and committed to the active input context. Errors are logged via `FCITX_WARN`. The user cannot cancel an in-flight STT request.

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

**1. Dependencies**

You'll need a C++ compiler, CMake, Ninja, and the development libraries for Fcitx5. For audio capture and speech recognition, you'll also need libraries like PulseAudio and a networking library like cURL.

Example on Debian/Ubuntu:
```bash
sudo apt install build-essential cmake ninja-build gettext pkg-config \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev \
  libpulse-dev libcurl4-openssl-dev
```

**2. Build**

```bash
mkdir build && cd build
cmake ..
make
```

**3. Install and Run**

```bash
sudo make install
fcitx5 -r # Restart fcitx5 to load the new addon
```

**4. Environment Setup**

For applications to see Fcitx5, you may need to set these environment variables:
```bash
export XMODIFIERS=@im=fcitx
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
```

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

### Speech Recognition Server

This module expects a [whisper-asr-webservice](https://github.com/ahmetoner/whisper-asr-webservice)-compatible HTTP server. By default it targets `http://localhost:9000/asr?output=txt&encode=false`:

- `output=txt` makes the server return the transcript as a plain text body (no JSON parsing required).
- `encode=false` skips the server-side ffmpeg re-encode step, because the addon already sends a 16 kHz mono S16LE WAV that Whisper accepts natively.

The endpoint URL is currently hardcoded in `VoiceInputModule`'s constructor (`src/voiceinput-module.cpp`); making it user-configurable is tracked together with the hardcoded F12 hotkey.

## Appendix: Useful Links

- [Fcitx5 Wiki: Basic Concepts](https://fcitx-im.org/wiki/Basic_concept)
- [Fcitx5 Wiki: Addon Types](https://fcitx-im.org/wiki/Addon_Type)
- [Fcitx5 on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)
- [ArchWiki Fcitx5 Setup](https://wiki.archlinux.org/title/Fcitx5)