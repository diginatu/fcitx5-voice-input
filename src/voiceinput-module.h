#pragma once
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/instance.h>
#include <fcitx-utils/handlertable.h>
#include <memory>
#include <string>
#include <vector>

class SpeechRecognizer;

class VoiceInputModule final : public fcitx::AddonInstance {
public:
  VoiceInputModule(fcitx::Instance *instance);
  ~VoiceInputModule() override;

  fcitx::Instance *instance() { return instance_; }

  void onSpeechResult(const std::string &text);

private:
  void registerEventWatchers();
  void showIndicator(const std::string &text);
  void hideIndicator();
  void startListening();
  void cancel();

  fcitx::Instance *instance_;
  std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
      eventHandlers_;

  bool active_ = false;
  std::unique_ptr<SpeechRecognizer> recognizer_;
};