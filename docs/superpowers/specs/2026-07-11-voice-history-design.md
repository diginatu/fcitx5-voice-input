# Design: Voice-input history for recovery

Date: 2026-07-11
Status: Approved

## Goals

Recover from two failure scenarios:

1. **The STT request failed** (server down, timeout, auth error) — keep the
   **audio** so it can be re-transcribed without re-speaking.
2. **The transcript was lost after commit** (committed into the wrong window,
   or the application discarded it) — keep the **text** so it can be
   re-committed.

Privacy constraint: audio is persisted **only when the STT request fails**.
Successful recordings never touch disk; only their transcript text is kept.

Delivered in three independently shippable phases:

1. History core (on-disk persistence + config).
2. Recovery hotkeys (retry / re-commit newest entry).
3. History picker UI (candidate list of recent entries).

Each phase gets its own implementation plan; later phases build only on the
`History` API defined in phase 1.

## Section 1 — On-disk format and the `History` component (Phase 1)

**Location:** `~/.local/share/fcitx5/voiceinput/history/`, resolved via
Fcitx's standard-path API for the pkgdata directory so `$XDG_DATA_HOME` is
respected. Created on demand.

**Format:** one file per history entry, named by local timestamp,
e.g. `20260711-143502.txt` / `20260711-143502.wav`. On a same-second
collision a numeric suffix is appended (`20260711-143502-1.txt`).

- `.txt` — a successful transcript: the exact UTF-8 text that was committed.
- `.wav` — a recording whose STT request failed (RIFF WAV, S16LE / 16 kHz /
  mono, as produced by `audio_capture`). If a later retry succeeds, the
  transcript is written as a **new** `.txt` entry and the `.wav` is deleted.

**`src/history.{h,cpp}`** — a small class with no Fcitx or libcurl
dependencies (only `std::filesystem` and `<fstream>`), constructed with a
directory path and a max-entry count so unit tests can point it at a temp
directory:

- `saveTranscript(const std::string &text)` — writes `<stamp>.txt`, then
  prunes.
- `saveFailedAudio(const std::vector<uint8_t> &wav)` — writes `<stamp>.wav`,
  then prunes.
- `listEntries()` — newest-first list of `{path, kind}` where `kind` is
  `Transcript` or `FailedAudio` (consumed by phases 2–3).
- `latestTranscript()` / `latestFailedAudio()` — convenience lookups for the
  phase-2 hotkeys.
- `remove(path)` — deletes an entry (used after a successful retry).
- Pruning deletes the oldest files beyond the configured max; each file is
  one entry regardless of kind. Ordering is by filename (timestamps sort
  lexicographically).
- Save/prune functions report failure to the caller (return value); they
  never throw across the module boundary.

## Section 2 — Module integration and configuration (Phase 1)

- `VoiceInputModule` owns a `History` instance, rebuilt in `setConfig()` the
  same way `recognizer_` is (via a file-local helper reading `config_`).
- **On success** (`onSpeechResult`): after `commitString`, call
  `saveTranscript(text)`.
- **On STT error:** the error callback captures a copy of the WAV bytes by
  value (in memory — this is the only path where audio reaches disk) and
  calls `saveFailedAudio(wav)` alongside the existing transient error
  indicator.
- History failures (unwritable directory, full disk) log `FCITX_WARN` and
  never block committing text or showing results.
- **New config options** in `VoiceInputConfig` (auto-surfaced in
  fcitx5-configtool, persisted to `~/.config/fcitx5/conf/voiceinput.conf`):
  - `HistoryEnabled` — bool, default **true**. When false, nothing is saved
    and the phase-2/3 features report "No history".
  - `HistorySize` — int, default **50**, constrained to ≥ 1: the maximum
    number of entries kept.

## Section 3 — Recovery hotkeys (Phase 2)

Handled in the existing key-event watcher, active only while **not**
recording (`!active_`), matched via `key().checkKeyList(...)` like the
existing keys. Both default to **empty key lists** — the user assigns them in
fcitx5-configtool, so no new keys are claimed by default:

- `RetryKey` — takes the newest `.wav`, shows "Transcribing…", and sends it
  through the current recognizer configuration. On success: commit the text,
  `saveTranscript`, and delete that `.wav`. On error: keep the `.wav` and
  show the usual transient error indicator.
- `RecommitKey` — reads the newest `.txt` and commits its content into the
  focused input context.
- Either key with no matching entry shows a brief "No history" indicator,
  reusing the existing 3-second auto-hide timer.

## Section 4 — History picker UI (Phase 3)

- `HistoryKey` (default empty key list) opens a quickphrase-style candidate
  list in the input panel, showing recent entries newest-first:
  - Transcripts render as a truncated preview (about 40 characters).
  - Failed recordings render as `[failed recording 14:35:02]`.
- Selecting a transcript commits it. Selecting a failed recording retries it
  (same flow as `RetryKey`). Escape closes the picker.
- Per-input-context state via `InputContextProperty`, following the
  `quickphrase/` reference implementation for candidate-list handling and
  paging.

## Section 5 — Error handling and testing

- **Unit tests** — `tests/history_test.cpp`, registered in
  `tests/CMakeLists.txt`, linking nothing beyond the standard library:
  - save/read round-trips for both entry kinds with exact byte/text content;
  - newest-first ordering of `listEntries()`;
  - pruning beyond `HistorySize` deletes the oldest entries;
  - same-second collision suffixing;
  - `latestTranscript()` / `latestFailedAudio()` selection;
  - `remove()` deletes exactly the given entry.
  - Each behavior is mutation-checked (briefly break the implementation,
    confirm the test fails, revert) per the repository TDD rule.
- `tests/voiceinput_config_test.cpp` extended to cover the new defaults as
  each phase lands: `HistoryEnabled=true` and `HistorySize=50` in phase 1;
  empty `RetryKey` / `RecommitKey` in phase 2; empty `HistoryKey` in
  phase 3.
- PulseAudio capture, the HTTP request, the hotkeys, and the picker are
  verified manually in a running Fcitx5 session:
  1. With the STT server stopped, record something — confirm a `.wav`
     appears in the history directory and the error indicator shows.
  2. Start the server, press `RetryKey` — confirm the transcript commits,
     a `.txt` appears, and the `.wav` is gone.
  3. Record successfully — confirm only a `.txt` appears (no `.wav`).
  4. Press `RecommitKey` in another text field — confirm the newest
     transcript is committed again.
  5. Open the picker with `HistoryKey` — confirm entries list newest-first
     and selection commits/retries.
  6. Set `HistorySize=2`, add three entries — confirm the oldest is pruned.
- **Known limitation (documented):** the WAV copy held for error-saving
  doubles peak memory for one recording's duration; acceptable at
  ~1.9 MB/min of audio. A wedged capture source still leaks its worker (a
  pre-existing issue, out of scope here); history only records what is
  actually delivered.
