#include "voiceinput-module.h"
#include "audio_capture.h"
#include "speech_recognizer.h"
#include <fcitx-config/iniparser.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h> // For AddonManager
#include <fcitx/event.h>       // For fcitx::Event
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>

#include <utility>

VoiceInputModule::VoiceInputModule(fcitx::Instance *instance)
    : instance_(instance), audioCapture_(std::make_unique<AudioCapture>()) {
  reloadConfig();
  recognizer_ = std::make_unique<SpeechRecognizer>(config_.endpoint.value());
  registerEventWatchers();
}

VoiceInputModule::~VoiceInputModule() = default;

void VoiceInputModule::reloadConfig() {
  fcitx::readAsIni(config_, "conf/voiceinput.conf");
}

void VoiceInputModule::setConfig(const fcitx::RawConfig &config) {
  config_.load(config, true);
  fcitx::safeSaveAsIni(config_, "conf/voiceinput.conf");
  recognizer_ = std::make_unique<SpeechRecognizer>(config_.endpoint.value());
}

void VoiceInputModule::registerEventWatchers() {
  eventHandlers_.emplace_back(instance_->watchEvent(
      fcitx::EventType::InputContextKeyEvent,
      fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event &event) {
        auto &ke = static_cast<fcitx::KeyEvent &>(event);
        if (!ke.isRelease() &&
            ke.key().checkKeyList(config_.activationKey.value())) {
          if (!active_) {
            ke.filterAndAccept();
            startListening();
          } else {
            // if pressed again, finalize the recording
            ke.filterAndAccept();
            finishRecording();
          }
        } else if (active_) {
          if (!ke.isRelease() &&
              ke.key().checkKeyList(config_.cancelKey.value())) {
            ke.filterAndAccept();
            cancel();
          } else {
            // Eat keys while listening so they don't leak
            ke.filterAndAccept();
          }
        }
      }));
}

void VoiceInputModule::startListening() {
  if (!audioCapture_->start()) {
    FCITX_WARN() << "voiceinput: failed to start audio capture";
    showIndicator("Audio init failed");
    return;
  }
  active_ = true;
  showIndicator("Listening…");
}

void VoiceInputModule::finishRecording() {
  lastRecording_ = audioCapture_->stop();
  FCITX_INFO() << "voiceinput: captured " << lastRecording_.size() << " bytes";
  hideIndicator();
  active_ = false;
  if (lastRecording_.empty()) {
    return;
  }
  auto wav = std::move(lastRecording_);
  auto *dispatcher = &instance_->eventDispatcher();
  recognizer_->transcribe(
      std::move(wav),
      [this, dispatcher](std::string text) {
        dispatcher->schedule(
            [this, text = std::move(text)]() { onSpeechResult(text); });
      },
      [dispatcher](std::string err) {
        dispatcher->schedule([err = std::move(err)]() {
          FCITX_WARN() << "voiceinput: STT error: " << err;
        });
      });
}

void VoiceInputModule::onSpeechResult(const std::string &text) {
  if (auto *ic = instance_->inputContextManager().mostRecentInputContext()) {
    ic->commitString(text);
  }
  hideIndicator();
  active_ = false;
}

void VoiceInputModule::cancel() {
  // Drop the buffer; user cancelled.
  (void)audioCapture_->stop();
  hideIndicator();
  active_ = false;
}

void VoiceInputModule::showIndicator(const std::string &text) {
  auto *ic = instance_->inputContextManager().mostRecentInputContext();
  if (!ic) {
    return;
  }
  auto &panel = ic->inputPanel();
  panel.reset();
  panel.setAuxUp(fcitx::Text(text));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void VoiceInputModule::hideIndicator() {
  auto *ic = instance_->inputContextManager().mostRecentInputContext();
  if (!ic) {
    return;
  }
  ic->inputPanel().reset();
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

namespace fcitx {
class VoiceInputModuleFactory : public AddonFactory {
  AddonInstance *create(AddonManager *manager) override {
    return new VoiceInputModule(manager->instance());
  }
};
} // namespace fcitx

FCITX_ADDON_FACTORY_V2(voiceinput, fcitx::VoiceInputModuleFactory)
