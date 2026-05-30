#include "voiceinput_config.h"

#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/key.h>

#include <cassert>
#include <iostream>
#include <string>

int main() {
    VoiceInputConfig config;

    // Defaults: activation key F12, single binding.
    auto activation = config.activationKey.value();
    assert(activation.size() == 1);
    assert(activation[0].sym() == FcitxKey_F12);

    // Defaults: cancel key Escape, single binding.
    auto cancel = config.cancelKey.value();
    assert(cancel.size() == 1);
    assert(cancel[0].sym() == FcitxKey_Escape);

    // Defaults: documented Whisper endpoint.
    assert(config.endpoint.value() ==
           "http://localhost:9000/asr?output=txt&encode=false");

    // Round-trip: load an override for Endpoint from RawConfig.
    fcitx::RawConfig raw;
    raw["Endpoint"].setValue("http://example.com/asr");
    config.load(raw, true);
    assert(config.endpoint.value() == "http://example.com/asr");

    // Defaults: API format whisper, OpenAI model + empty key.
    assert(config.apiFormat.value() == ApiFormat::WhisperAsrWebservice);
    assert(config.model.value() == "Systran/faster-whisper-small");
    assert(config.apiKey.value().empty());
    assert(config.prompt.value().empty());

    std::cout << "voiceinput_config_test: OK" << std::endl;
    return 0;
}
