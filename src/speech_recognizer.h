#pragma once
#include <functional>
#include <string>

class SpeechRecognizer {
public:
    using ResultCB = std::function<void(const std::string&)>;
    using ErrorCB  = std::function<void(const std::string&)>;

    void start(ResultCB onResult, ErrorCB onError);
    void stop();
};
