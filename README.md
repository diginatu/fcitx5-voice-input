# Fcitx5 Voice Input Module

A Fcitx5 module that enables voice input via a global hotkey. It's designed as a "Quick Phrase" style module, meaning it works on top of your existing Input Method Engine (IME) without needing to switch.

Press a hotkey, speak, and the recognized text will be committed to your currently focused application.

## High-level Architecture

- The module loads with Fcitx5 and registers a global hotkey.
- When the hotkey is pressed, it begins capturing audio and shows a "listening" indicator.
- The audio is sent to a speech recognition component.
- The final recognized text is committed to the active input context.

## Directory Structure

```
fcitx5-voiceinput/
├── CMakeLists.txt
├── src/
│   ├── CMakeLists.txt
│   ├── voiceinput-module.h
│   ├── voiceinput-module.cpp
│   ├── speech_recognizer.h
│   └── ...
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

The `SpeechRecognizer` class is an abstraction for the speech-to-text engine. It can be implemented to work with a local engine (like Vosk) or a cloud-based service (like Google Speech-to-Text). It captures audio and returns the final text via a callback.

A minimal interface looks like this:
```cpp
class SpeechRecognizer {
public:
    using ResultCB = std::function<void(const std::string&)>;
    using ErrorCB  = std::function<void(const std::string&)>;

    void start(ResultCB onResult, ErrorCB onError);
    void stop();
};
```

## Appendix: Useful Links

- [Fcitx5 Wiki: Basic Concepts](https://fcitx-im.org/wiki/Basic_concept)
- [Fcitx5 Wiki: Addon Types](https://fcitx-im.org/wiki/Addon_Type)
- [Fcitx5 on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)
- [ArchWiki Fcitx5 Setup](https://wiki.archlinux.org/title/Fcitx5)