#include "speech_recognizer.h"

#include <curl/curl.h>

#include <mutex>
#include <thread>

namespace {
std::once_flag curlInitFlag;

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *response = static_cast<std::string *>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}
} // namespace

SpeechRecognizer::SpeechRecognizer(RecognizerConfig config)
    : config_(std::move(config)) {}

void SpeechRecognizer::transcribe(std::vector<uint8_t> wav, ResultCB onResult,
                                  ErrorCB onError) {
    std::call_once(curlInitFlag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    RequestSpec spec = buildRequestSpec(config_);

    std::thread([wav = std::move(wav), onResult = std::move(onResult),
                 onError = std::move(onError), url = config_.endpoint,
                 spec = std::move(spec), model = config_.model,
                 apiKey = config_.apiKey, prompt = config_.prompt]() mutable {
        CURL *curl = curl_easy_init();
        if (!curl) {
            onError("curl_easy_init failed");
            return;
        }

        std::string response;
        curl_mime *mime = curl_mime_init(curl);
        curl_mimepart *part = curl_mime_addpart(mime);
        curl_mime_name(part, spec.fileFieldName.c_str());
        curl_mime_filename(part, "audio.wav");
        curl_mime_data(part, reinterpret_cast<const char *>(wav.data()),
                       wav.size());
        curl_mime_type(part, "audio/wav");

        if (spec.sendModel) {
            curl_mimepart *modelPart = curl_mime_addpart(mime);
            curl_mime_name(modelPart, "model");
            curl_mime_data(modelPart, model.c_str(), CURL_ZERO_TERMINATED);
        }
        if (spec.sendResponseFormatText) {
            curl_mimepart *fmtPart = curl_mime_addpart(mime);
            curl_mime_name(fmtPart, "response_format");
            curl_mime_data(fmtPart, "text", CURL_ZERO_TERMINATED);
        }
        if (spec.sendPrompt) {
            curl_mimepart *promptPart = curl_mime_addpart(mime);
            curl_mime_name(promptPart, "prompt");
            curl_mime_data(promptPart, prompt.c_str(), CURL_ZERO_TERMINATED);
        }

        struct curl_slist *headers = nullptr;
        if (spec.sendAuthHeader) {
            std::string auth = "Authorization: Bearer " + apiKey;
            headers = curl_slist_append(headers, auth.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        CURLcode res = curl_easy_perform(curl);

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        if (res != CURLE_OK) {
            onError(curl_easy_strerror(res));
        } else if (httpCode < 200 || httpCode >= 300) {
            onError("HTTP " + std::to_string(httpCode));
        } else {
            onResult(trimTranscript(response));
        }

        curl_mime_free(mime);
        if (headers) {
            curl_slist_free_all(headers);
        }
        curl_easy_cleanup(curl);
    }).detach();
}
