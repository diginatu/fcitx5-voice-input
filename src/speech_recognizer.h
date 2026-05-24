#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Trim trailing whitespace (\r, \n, \t, space) from a Whisper text response.
// Leading whitespace is preserved. Defined inline so test binaries can
// exercise it without linking libcurl.
inline std::string trimTranscript(std::string s) {
    const auto pos = s.find_last_not_of("\r\n\t ");
    if (pos == std::string::npos) {
        s.clear();
    } else {
        s.erase(pos + 1);
    }
    return s;
}

class SpeechRecognizer {
public:
    using ResultCB = std::function<void(std::string)>;
    using ErrorCB = std::function<void(std::string)>;

    explicit SpeechRecognizer(std::string endpoint);
    ~SpeechRecognizer();

    SpeechRecognizer(const SpeechRecognizer &) = delete;
    SpeechRecognizer &operator=(const SpeechRecognizer &) = delete;

    // Fire-and-forget. Spawns a detached worker that POSTs `wav` as
    // multipart/form-data and invokes onResult or onError on the worker
    // thread. Callers that need main-thread delivery must wrap the callbacks
    // accordingly (e.g. via fcitx::EventDispatcher::schedule).
    void transcribe(std::vector<uint8_t> wav, ResultCB onResult,
                    ErrorCB onError);

private:
    static void runRequest(std::string url, std::vector<uint8_t> wav,
                           ResultCB onResult, ErrorCB onError);

    std::string endpoint_;
};
