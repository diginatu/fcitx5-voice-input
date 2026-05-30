#include "speech_recognizer.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void expectEqual(const std::string &actual, const std::string &expected,
                 const std::string &label) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << " expected='" << expected
                  << "' actual='" << actual << "'\n";
        ++failures;
    }
}

void expectBool(bool actual, bool expected, const std::string &label) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << " expected=" << expected
                  << " actual=" << actual << "\n";
        ++failures;
    }
}
} // namespace

int main() {
    expectEqual(trimTranscript("hello"), "hello", "no whitespace");
    expectEqual(trimTranscript("hello\n"), "hello", "trailing newline");
    expectEqual(trimTranscript("hello \t\r\n"), "hello", "trailing mixed");
    expectEqual(trimTranscript("  hi"), "  hi", "leading preserved");
    expectEqual(trimTranscript(""), "", "empty");
    expectEqual(trimTranscript("   "), "", "all whitespace");
    expectEqual(trimTranscript("line1\nline2\n"), "line1\nline2",
                "internal newline kept");

    // Whisper ASR webservice backend: legacy field name, no model/auth.
    {
        RecognizerConfig cfg;
        cfg.endpoint = "http://localhost:9000/asr";
        cfg.openAiCompatible = false;
        cfg.model = "ignored";
        cfg.apiKey = "ignored";
        RequestSpec spec = buildRequestSpec(cfg);
        expectEqual(spec.fileFieldName, "audio_file", "whisper field name");
        expectBool(spec.sendModel, false, "whisper no model");
        expectBool(spec.sendResponseFormatText, false, "whisper no response_format");
        expectBool(spec.sendAuthHeader, false, "whisper no auth");
    }

    // OpenAI-compatible backend without an API key.
    {
        RecognizerConfig cfg;
        cfg.endpoint = "http://localhost:8000/v1/audio/transcriptions";
        cfg.openAiCompatible = true;
        cfg.model = "Systran/faster-whisper-small";
        cfg.apiKey = "";
        RequestSpec spec = buildRequestSpec(cfg);
        expectEqual(spec.fileFieldName, "file", "openai field name");
        expectBool(spec.sendModel, true, "openai sends model");
        expectBool(spec.sendResponseFormatText, true, "openai response_format=text");
        expectBool(spec.sendAuthHeader, false, "openai no auth without key");
    }

    // OpenAI-compatible backend with an API key set.
    {
        RecognizerConfig cfg;
        cfg.openAiCompatible = true;
        cfg.model = "whisper-1";
        cfg.apiKey = "sk-test";
        RequestSpec spec = buildRequestSpec(cfg);
        expectBool(spec.sendAuthHeader, true, "openai auth with key");
    }

    if (failures == 0) {
        std::cout << "All speech_recognizer tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
