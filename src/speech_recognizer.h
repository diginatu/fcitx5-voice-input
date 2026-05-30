#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Configuration for a SpeechRecognizer: which endpoint to call and how to shape
// the request. The whisper-asr-webservice backend and the OpenAI-compatible
// backend (e.g. speaches, OpenAI) differ in the multipart field name, the need
// for a "model" field, and authentication.
struct RecognizerConfig {
    std::string endpoint;
    bool openAiCompatible = false;
    std::string model;
    std::string apiKey;
};

// Asynchronous speech-to-text client.
//
// Posts a WAV byte buffer to a Whisper-compatible or OpenAI-compatible HTTP
// endpoint and delivers the transcript (or an error) through callbacks. Each
// transcribe() call performs the network request on a detached worker thread,
// so the calling thread (the Fcitx event loop) is never blocked.
class SpeechRecognizer {
public:
    using ResultCB = std::function<void(const std::string &)>;
    using ErrorCB = std::function<void(const std::string &)>;

    explicit SpeechRecognizer(RecognizerConfig config);

    // Async: returns immediately; exactly one of onResult/onError fires later.
    void transcribe(std::vector<uint8_t> wav, ResultCB onResult,
                    ErrorCB onError);

    const std::string &endpoint() const { return config_.endpoint; }

private:
    RecognizerConfig config_;
};

// Describes how the multipart POST should be built for a given backend. Kept as
// a pure function (and the struct header-only) so unit tests can exercise the
// backend-selection logic without linking libcurl.
struct RequestSpec {
    std::string fileFieldName;          // "audio_file" (whisper) or "file" (OpenAI)
    bool sendModel = false;             // add a "model" form field
    bool sendResponseFormatText = false; // add response_format=text form field
    bool sendAuthHeader = false;        // add "Authorization: Bearer <key>"
};

inline RequestSpec buildRequestSpec(const RecognizerConfig &cfg) {
    RequestSpec spec;
    if (cfg.openAiCompatible) {
        spec.fileFieldName = "file";
        spec.sendModel = true;
        spec.sendResponseFormatText = true;
        spec.sendAuthHeader = !cfg.apiKey.empty();
    } else {
        spec.fileFieldName = "audio_file";
    }
    return spec;
}

// Strip trailing whitespace (including CR/LF) the server may append. Kept inline
// in the header so unit tests can exercise it without linking libcurl.
inline std::string trimTranscript(const std::string &raw) {
    size_t end = raw.size();
    while (end > 0) {
        unsigned char c = static_cast<unsigned char>(raw[end - 1]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            --end;
        } else {
            break;
        }
    }
    return raw.substr(0, end);
}
