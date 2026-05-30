#pragma once
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>

#include <string>

// Which speech-to-text HTTP API the configured endpoint speaks.
enum class ApiFormat { WhisperAsrWebservice, OpenAICompatible };

FCITX_CONFIG_ENUM_NAME_WITH_I18N(ApiFormat, "Whisper ASR webservice",
                                 "OpenAI-compatible (speaches/OpenAI)");

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
    fcitx::OptionWithAnnotation<ApiFormat, ApiFormatI18NAnnotation> apiFormat{
        this, "ApiFormat", "API format", ApiFormat::WhisperAsrWebservice};
    fcitx::Option<std::string> endpoint{
        this, "Endpoint", "STT endpoint URL",
        "http://localhost:9000/asr?output=txt&encode=false"};
    fcitx::Option<std::string> model{
        this, "Model", "Model (OpenAI-compatible backends)",
        "Systran/faster-whisper-small"};
    fcitx::Option<std::string> apiKey{
        this, "ApiKey", "API key (OpenAI-compatible backends)", ""};
    fcitx::Option<std::string> prompt{
        this, "Prompt", "Prompt (OpenAI-compatible backends)", ""};);
