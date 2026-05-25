#pragma once
#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/key.h>

#include <string>

FCITX_CONFIGURATION(
    VoiceInputConfig,
    fcitx::KeyListOption activationKey{
        this,
        "ActivationKey",
        "Activation key",
        {fcitx::Key{FcitxKey_F12}},
        fcitx::KeyListConstrain({fcitx::KeyConstrainFlag::AllowModifierLess})};
    fcitx::KeyListOption cancelKey{
        this,
        "CancelKey",
        "Cancel key",
        {fcitx::Key{FcitxKey_Escape}},
        fcitx::KeyListConstrain({fcitx::KeyConstrainFlag::AllowModifierLess})};
    fcitx::Option<std::string> endpoint{
        this, "Endpoint", "Whisper STT endpoint",
        "http://localhost:9000/asr?output=txt&encode=false"};);
