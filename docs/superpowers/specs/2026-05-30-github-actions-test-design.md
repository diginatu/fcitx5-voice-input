---
title: GitHub Actions Test Workflow
date: 2026-05-30
---

## Goal

Add a CI workflow that builds the project and runs all three CTest tests on every push and PR to `main`.

## Workflow file

`.github/workflows/test.yml`

## Trigger

- `push` to `main`
- `pull_request` targeting `main`

No path filter — runs on any change to the repo.

## Runner

`ubuntu-26.04`

## Steps

1. `actions/checkout@v4`
2. `sudo apt-get install -y cmake ninja-build pkg-config gettext libfcitx5-dev libpulse-dev libcurl4-openssl-dev`
3. CMake configure: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr ..`
4. Build: `ninja`
5. Test: `ctest --output-on-failure`

## Tests covered

| Test | Dependencies |
|------|-------------|
| `wav_header_test` | none (pure header) |
| `speech_recognizer_test` | none (pure header) |
| `voiceinput_config_test` | Fcitx5::Core, Fcitx5::Utils |
