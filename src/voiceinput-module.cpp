#include "voiceinput-module.h"
#include "speech_recognizer.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h> // For AddonManager
#include <fcitx/event.h>       // For fcitx::Event
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx-utils/keysym.h>

VoiceInputModule::VoiceInputModule(fcitx::Instance *instance)
    : instance_(instance) {
  registerEventWatchers();
}

VoiceInputModule::~VoiceInputModule() = default;

void VoiceInputModule::registerEventWatchers() {
  eventHandlers_.emplace_back(instance_->watchEvent(
      fcitx::EventType::InputContextKeyEvent,
      fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event &event) {
        auto &ke = static_cast<fcitx::KeyEvent &>(event);
        // Example: F12 triggers
        if (!ke.isRelease() && ke.key().sym() == FcitxKey_F12) {
          if (!active_) {
            ke.filterAndAccept();
            startListening();
          } else {
            // if pressed again, cancel
            ke.filterAndAccept();
            cancel();
          }
        } else if (active_) {
          // Allow ESC to cancel while active
          if (!ke.isRelease() && ke.key().sym() == FcitxKey_Escape) {
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
  active_ = true;
  showIndicator("Listening…");
  // recognizer_->start(... callbacks ...)
}

void VoiceInputModule::onSpeechResult(const std::string &text) {
  if (auto *ic = instance_->inputContextManager().mostRecentInputContext()) {
    ic->commitString(text);
  }
  hideIndicator();
  active_ = false;
}

void VoiceInputModule::cancel() {
  // recognizer_->stop();
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
