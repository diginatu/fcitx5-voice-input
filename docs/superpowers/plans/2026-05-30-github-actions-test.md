# GitHub Actions Test Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a CI workflow that builds the project and runs all three CTest tests on every push and pull request to `main`.

**Architecture:** A single GitHub Actions workflow file installs system dependencies via apt, configures and builds with CMake + Ninja, then runs ctest. No caching or matrix — keep it simple.

**Tech Stack:** GitHub Actions, Ubuntu 26.04, CMake, Ninja, CTest, apt

---

### Task 1: Create the test workflow

**Files:**
- Create: `.github/workflows/test.yml`

- [ ] **Step 1: Create the workflow file**

```yaml
name: Test

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  test:
    runs-on: ubuntu-26.04

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            cmake \
            ninja-build \
            pkg-config \
            gettext \
            libfcitx5-dev \
            libpulse-dev \
            libcurl4-openssl-dev

      - name: Configure
        run: cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr

      - name: Build
        run: ninja -C build

      - name: Test
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/test.yml docs/superpowers/specs/2026-05-30-github-actions-test-design.md docs/superpowers/plans/2026-05-30-github-actions-test.md
git commit -m "Add GitHub Actions workflow to run tests"
```
