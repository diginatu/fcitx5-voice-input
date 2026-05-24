#include "speech_recognizer.h"

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

size_t writeBodyCb(char *data, size_t size, size_t nmemb, void *userp) {
    const size_t total = size * nmemb;
    auto *body = static_cast<std::string *>(userp);
    body->append(data, total);
    return total;
}

void ensureCurlGlobalInit() {
    static std::once_flag flag;
    std::call_once(flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

SpeechRecognizer::SpeechRecognizer(std::string endpoint)
    : endpoint_(std::move(endpoint)) {
    ensureCurlGlobalInit();
}

SpeechRecognizer::~SpeechRecognizer() = default;

void SpeechRecognizer::transcribe(std::vector<uint8_t> wav, ResultCB onResult,
                                  ErrorCB onError) {
    if (wav.empty()) {
        if (onError) {
            onError("empty recording");
        }
        return;
    }
    std::thread(&SpeechRecognizer::runRequest, endpoint_, std::move(wav),
                std::move(onResult), std::move(onError))
        .detach();
}

void SpeechRecognizer::runRequest(std::string url, std::vector<uint8_t> wav,
                                  ResultCB onResult, ErrorCB onError) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (onError) {
            onError("curl_easy_init failed");
        }
        return;
    }

    std::string body;
    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "audio_file");
    curl_mime_filename(part, "audio.wav");
    curl_mime_type(part, "audio/wav");
    curl_mime_data(part, reinterpret_cast<const char *>(wav.data()),
                   wav.size());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBodyCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    const CURLcode rc = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (onError) {
            onError(std::string("curl: ") + curl_easy_strerror(rc));
        }
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        if (onError) {
            onError("HTTP " + std::to_string(httpStatus) + ": " + body);
        }
        return;
    }
    if (onResult) {
        onResult(trimTranscript(std::move(body)));
    }
}
