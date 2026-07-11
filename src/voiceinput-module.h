#pragma once
#include "history.h"
#include "voiceinput_config.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/instance.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/handlertable.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fcitx { struct EventSourceTime; }

class SpeechRecognizer;
class AudioCapture;

class VoiceInputModule final : public fcitx::AddonInstance {
public:
  VoiceInputModule(fcitx::Instance *instance);
  ~VoiceInputModule() override;

  fcitx::Instance *instance() { return instance_; }

  void onSpeechResult(const std::string &text);

  const fcitx::Configuration *getConfig() const override { return &config_; }
  void setConfig(const fcitx::RawConfig &config) override;
  void reloadConfig() override;

private:
  void registerEventWatchers();
  void showIndicator(const std::string &text);
  void hideIndicator();
  void showTransientError(const std::string &text);
  void startListening();
  void finishRecording();
  void onCaptureComplete(std::vector<uint8_t> wav);
  void cancel();
  void retryLast();
  void retryEntry(const std::filesystem::path &wavPath);
  void recommitLast();

  VoiceInputConfig config_;
  fcitx::Instance *instance_;
  std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
      eventHandlers_;

  fcitx::EventDispatcher dispatcher_;
  bool active_ = false;
  std::unique_ptr<SpeechRecognizer> recognizer_;
  std::unique_ptr<AudioCapture> audioCapture_;
  std::unique_ptr<History> history_;
  std::unique_ptr<fcitx::EventSourceTime> errorTimer_;
};