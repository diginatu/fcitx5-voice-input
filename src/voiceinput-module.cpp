#include "voiceinput-module.h"
#include "audio_capture.h"
#include "history.h"
#include "speech_recognizer.h"
#include <fcitx-config/iniparser.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h> // For AddonManager
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>       // For fcitx::Event
#include <fcitx/globalconfig.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <functional>
#include <utility>

namespace {
// Build the SpeechRecognizer from the current addon configuration, translating
// the API-format enum into the recognizer's backend flag.
std::unique_ptr<SpeechRecognizer> makeRecognizer(const VoiceInputConfig &config) {
  RecognizerConfig rc;
  rc.endpoint = config.endpoint.value();
  rc.openAiCompatible = config.apiFormat.value() == ApiFormat::OpenAICompatible;
  rc.model = config.model.value();
  rc.apiKey = config.apiKey.value();
  rc.prompt = config.prompt.value();
  return std::make_unique<SpeechRecognizer>(std::move(rc));
}

// Build the History from config: resolves the data dir under the Fcitx
// standard path so $XDG_DATA_HOME is respected. Returns null when disabled.
std::unique_ptr<History> makeHistory(const VoiceInputConfig &config) {
  if (!config.historyEnabled.value()) {
    return nullptr;
  }
  std::filesystem::path dir =
      fcitx::StandardPaths::global().userDirectory(
          fcitx::StandardPathsType::Data);
  dir /= "fcitx5/voiceinput/history";
  auto size = static_cast<std::size_t>(std::max(1, config.historySize.value()));
  return std::make_unique<History>(std::move(dir), size);
}

// A history-picker candidate that runs an arbitrary action when selected.
class HistoryCandidateWord : public fcitx::CandidateWord {
public:
  HistoryCandidateWord(const std::string &label, std::function<void()> action)
      : fcitx::CandidateWord(fcitx::Text(label)), action_(std::move(action)) {}

  void select(fcitx::InputContext *) const override {
    if (action_) {
      action_();
    }
  }

private:
  std::function<void()> action_;
};
} // namespace

VoiceInputModule::VoiceInputModule(fcitx::Instance *instance)
    : instance_(instance), audioCapture_(std::make_unique<AudioCapture>()) {
  reloadConfig();
  recognizer_ = makeRecognizer(config_);
  history_ = makeHistory(config_);
  dispatcher_.attach(&instance_->eventLoop());
  registerEventWatchers();
}

VoiceInputModule::~VoiceInputModule() = default;

void VoiceInputModule::reloadConfig() {
  fcitx::readAsIni(config_, "conf/voiceinput.conf");
}

void VoiceInputModule::setConfig(const fcitx::RawConfig &config) {
  config_.load(config, true);
  fcitx::safeSaveAsIni(config_, "conf/voiceinput.conf");
  recognizer_ = makeRecognizer(config_);
  history_ = makeHistory(config_);
}

void VoiceInputModule::registerEventWatchers() {
  eventHandlers_.emplace_back(instance_->watchEvent(
      fcitx::EventType::InputContextKeyEvent,
      fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event &event) {
        auto &ke = static_cast<fcitx::KeyEvent &>(event);
        // While the history picker is open, drive the candidate list.
        if (pickerOpen_) {
          auto *ic = ke.inputContext();
          auto candidateList = ic->inputPanel().candidateList();
          if (!candidateList) {
            pickerOpen_ = false;
          } else if (!ke.isRelease()) {
            int idx = ke.key().digitSelection();
            if (idx >= 0 && idx < candidateList->size()) {
              ke.filterAndAccept();
              candidateList->candidate(idx).select(ic);
              return;
            }
            if (ke.key().check(FcitxKey_Escape)) {
              ke.filterAndAccept();
              closePicker();
              return;
            }
            if (ke.key().check(FcitxKey_Return) ||
                ke.key().check(FcitxKey_KP_Enter)) {
              ke.filterAndAccept();
              if (candidateList->cursorIndex() >= 0) {
                candidateList->candidate(candidateList->cursorIndex())
                    .select(ic);
              }
              return;
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultPrevCandidate())) {
              ke.filterAndAccept();
              candidateList->toCursorMovable()->prevCandidate();
              ic->updateUserInterface(
                  fcitx::UserInterfaceComponent::InputPanel);
              return;
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultNextCandidate())) {
              ke.filterAndAccept();
              candidateList->toCursorMovable()->nextCandidate();
              ic->updateUserInterface(
                  fcitx::UserInterfaceComponent::InputPanel);
              return;
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultPrevPage())) {
              auto *pageable = candidateList->toPageable();
              if (pageable->hasPrev()) {
                ke.filterAndAccept();
                pageable->prev();
                ic->updateUserInterface(
                    fcitx::UserInterfaceComponent::InputPanel);
                return;
              }
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultNextPage())) {
              auto *pageable = candidateList->toPageable();
              if (pageable->hasNext()) {
                ke.filterAndAccept();
                pageable->next();
                ic->updateUserInterface(
                    fcitx::UserInterfaceComponent::InputPanel);
                return;
              }
            }
            // Eat any other key so the picker stays modal.
            ke.filterAndAccept();
            return;
          }
        }
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
        } else if (!active_ && !ke.isRelease() &&
                   ke.key().checkKeyList(config_.retryKey.value())) {
          ke.filterAndAccept();
          retryLast();
        } else if (!active_ && !ke.isRelease() &&
                   ke.key().checkKeyList(config_.recommitKey.value())) {
          ke.filterAndAccept();
          recommitLast();
        } else if (!active_ && !ke.isRelease() &&
                   ke.key().checkKeyList(config_.historyKey.value())) {
          ke.filterAndAccept();
          openHistoryPicker();
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
  errorTimer_.reset();
  if (!audioCapture_->start()) {
    FCITX_WARN() << "voiceinput: failed to start audio capture";
    showIndicator("Audio init failed");
    return;
  }
  active_ = true;
  showIndicator("Listening…");
}

void VoiceInputModule::finishRecording() {
  active_ = false;
  showIndicator("Transcribing…");
  // Non-blocking: the worker delivers the WAV later, on its own thread. Hop back
  // to the Fcitx main thread before touching any module/input-context state.
  audioCapture_->finish([this](std::vector<uint8_t> wav) {
    dispatcher_.schedule([this, wav = std::move(wav)]() mutable {
      onCaptureComplete(std::move(wav));
    });
  });
}

void VoiceInputModule::onCaptureComplete(std::vector<uint8_t> wav) {
  FCITX_INFO() << "voiceinput: captured " << wav.size() << " bytes";
  if (wav.empty()) {
    hideIndicator();
    return;
  }
  recognizer_->transcribe(
      wav, // pass by copy; keep our own for the error path below
      [this](std::string text) {
        dispatcher_.schedule(
            [this, text = std::move(text)]() { onSpeechResult(text); });
      },
      [this, wav](std::string err) {
        dispatcher_.schedule([this, err = std::move(err), wav]() {
          if (history_) {
            if (!history_->saveFailedAudio(wav)) {
              FCITX_WARN() << "voiceinput: failed to save audio to history";
            }
          }
          FCITX_WARN() << "voiceinput: STT error: " << err;
          showTransientError("Error: " + err);
        });
      });
}

void VoiceInputModule::onSpeechResult(const std::string &text) {
  if (auto *ic = instance_->inputContextManager().mostRecentInputContext()) {
    ic->commitString(text);
  }
  if (history_) {
    if (!history_->saveTranscript(text)) {
      FCITX_WARN() << "voiceinput: failed to save transcript to history";
    }
  }
  hideIndicator();
  active_ = false;
}

void VoiceInputModule::cancel() {
  // Drop the buffer; user cancelled. Non-blocking, like finishRecording().
  audioCapture_->cancel();
  hideIndicator();
  active_ = false;
}

void VoiceInputModule::recommitLast() {
  if (!history_) {
    return;
  }
  auto entry = history_->latestTranscript();
  if (!entry) {
    showTransientError("No history");
    return;
  }
  std::string text = readFile(entry->path);
  if (auto *ic = instance_->inputContextManager().mostRecentInputContext()) {
    ic->commitString(text);
  }
}

void VoiceInputModule::retryLast() {
  if (!history_) {
    return;
  }
  auto entry = history_->latestFailedAudio();
  if (!entry) {
    showTransientError("No history");
    return;
  }
  retryEntry(entry->path);
}

void VoiceInputModule::retryEntry(const std::filesystem::path &wavPath) {
  std::string data = readFile(wavPath);
  std::vector<uint8_t> wav(data.begin(), data.end());
  showIndicator("Transcribing…");
  recognizer_->transcribe(
      wav,
      [this, wavPath](std::string text) {
        dispatcher_.schedule([this, text = std::move(text), wavPath]() {
          onSpeechResult(text);
          if (history_) {
            history_->remove(wavPath); // retry succeeded; drop the audio
          }
        });
      },
      [this](std::string err) {
        dispatcher_.schedule([this, err = std::move(err)]() {
          FCITX_WARN() << "voiceinput: retry STT error: " << err;
          showTransientError("Error: " + err);
        });
      });
}

void VoiceInputModule::openHistoryPicker() {
  if (!history_) {
    return;
  }
  auto entries = history_->listEntries();
  if (entries.empty()) {
    showTransientError("No history");
    return;
  }
  auto *ic = instance_->inputContextManager().mostRecentInputContext();
  if (!ic) {
    return;
  }
  auto list = std::make_unique<fcitx::CommonCandidateList>();
  list->setPageSize(10);
  for (const auto &e : entries) {
    if (e.kind == HistoryEntryKind::Transcript) {
      std::string text = readFile(e.path);
      std::string label = text.substr(0, 40);
      list->append<HistoryCandidateWord>(label, [this, text]() {
        closePicker();
        if (auto *ic =
                instance_->inputContextManager().mostRecentInputContext()) {
          ic->commitString(text);
        }
      });
    } else {
      std::string label =
          "[failed recording " + e.path.stem().string() + "]";
      std::filesystem::path path = e.path;
      list->append<HistoryCandidateWord>(label, [this, path]() {
        closePicker();
        retryEntry(path);
      });
    }
  }
  if (!list->empty()) {
    list->setGlobalCursorIndex(0);
  }
  ic->inputPanel().reset();
  ic->inputPanel().setCandidateList(std::move(list));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  pickerOpen_ = true;
}

void VoiceInputModule::closePicker() {
  pickerOpen_ = false;
  hideIndicator();
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

void VoiceInputModule::showTransientError(const std::string &text) {
  showIndicator(text);
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t usec = static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
                  static_cast<uint64_t>(ts.tv_nsec) / 1000ULL + 3'000'000ULL;
  errorTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, usec, 0,
      [this](fcitx::EventSourceTime *, uint64_t) -> bool {
        hideIndicator();
        return true;
      });
}

namespace fcitx {
class VoiceInputModuleFactory : public AddonFactory {
  AddonInstance *create(AddonManager *manager) override {
    return new VoiceInputModule(manager->instance());
  }
};
} // namespace fcitx

#ifdef FCITX_ADDON_FACTORY_V2
FCITX_ADDON_FACTORY_V2(voiceinput, fcitx::VoiceInputModuleFactory)
#else
FCITX_ADDON_FACTORY(fcitx::VoiceInputModuleFactory)
#endif
