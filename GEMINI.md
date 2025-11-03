# Gemini Workspace Context: fcitx5-voice-input

## Project Overview

This project is a voice input module for the Fcitx5 input method framework. It allows users to press a hotkey, speak, and have the recognized text inserted into the currently focused application. It is designed as a "Quick Phrase" style module, meaning it works on top of the user's existing IME without requiring a manual switch.

**Core Functionality:**

1.  **Hotkey Trigger:** A global hotkey (configurable, defaults to `F12`) activates the voice input mode.
2.  **Audio Capture:** When triggered, the module starts capturing audio from the microphone using PulseAudio.
3.  **Speech Recognition:** The captured audio is sent to a speech recognition service. (The specific service/API is not yet implemented).
4.  **Text Commitment:** The recognized text is then committed to the active input context by Fcitx5.

**Technical Details:**

*   **Language:** C++ (C++20)
*   **Build System:** CMake
*   **Framework:** Fcitx5 Addon
*   **Dependencies:**
    *   Fcitx5 (core, utils)
    *   PulseAudio (for audio capture)
    *   cURL (for potential network-based speech recognition)

**Architecture:**

*   `VoiceInputModule`: The main addon class that inherits from `fcitx::AddonInstance`. It manages the addon's lifecycle, event watching (for the hotkey), and orchestrates the voice input process.
*   `AudioCapture`: A class responsible for handling microphone input via PulseAudio. (Implementation is a TODO).
*   `SpeechRecognizer`: A class that abstracts the speech-to-text functionality. It will take the captured audio and return a text result via a callback. (Implementation is a TODO).
*   **Configuration:**
    *   `conf/default.yaml`: Defines the default hotkey.
    *   `src/voiceinput.conf`: Defines user-configurable options for the Fcitx5 config tool.
    *   `src/voiceinput-addon.conf.in`: The addon descriptor file for Fcitx5.

## Building and Running

**1. Dependencies (Debian/Ubuntu Example)**

```bash
sudo apt install build-essential cmake ninja-build gettext pkg-config \
  libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev \
  libpulse-dev libcurl4-openssl-dev
```

**2. Build Commands**

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

## Development Conventions

*   **Code Style:** The code follows the general style of the Fcitx5 project. Class members are typically `snake_case` with a trailing underscore (e.g., `instance_`).
*   **Modularity:** The logic is separated into distinct classes (`VoiceInputModule`, `SpeechRecognizer`, `AudioCapture`) for clarity and maintainability.
*   **Event-Driven:** The module is driven by Fcitx5 events, primarily the `InputContextKeyEvent` to detect the trigger hotkey.
*   **In-Progress Implementation:** The core logic for audio capture and speech recognition in `audio_capture.cpp` and `speech_recognizer.cpp` is currently placeholder and needs to be implemented.
