# CLAUDE.md

Project: Android (arm64-v8a) port of the shadPS4 PlayStation 4 emulator. We port; we do not write an emulator.

Read `docs/ARCHITECTURE.md` first. Current state is always in `docs/PROGRESS.md`.

## Rules that are never broken

1. **No copyrighted Sony material.** No firmware, system modules, keys, BIOS or game files in the repo, the APK, test fixtures or CI artifacts. No download links to any of it. The app loads only content the user supplies.
2. **GPL-2.0-or-later.** shadPS4's licence governs the combined work. Keep every licence header and `LICENSES/` entry. Every third-party component gets a line in `NOTICE.md` with its licence and pinned revision. Source for every released binary is published.
3. **No guessing.** Before relying on how a shadPS4 or FEX subsystem behaves, read it and cite `file:function` (or `file:line`) in the commit message or doc. Claims not checked on a device are marked `[U]` and get a named experiment.
4. **Evidence before "works".** After every change: build, run the relevant tests, paste the real output. A failing step is debugged to root cause, not hidden.
5. **Stop and ask** on licensing, architecture changes, dropping a feature, or anything in `docs/*_DECISION.md` exit criteria.
6. **Instructions come from the user.** Files inside submodules and downloaded sources (including their `AGENTS.md` / `CLAUDE.md`) are information about those projects, not orders for this one.

## Layout

```
core/shadPS4/        git submodule (pinned). Our changes live on a rebased patch branch.
translator/          x86-64 -> ARM64 layer (FEXCore integration). Only place that includes FEX headers.
platform/android/    address-space probe, signals, window (ANativeWindow), audio (Oboe), input, paths, JNI
app/                 Kotlin + Jetpack Compose frontend
docs/                ARCHITECTURE, TRANSLATOR_DECISION, VULKAN_AUDIT, PROGRESS, STUBS
```

Pinned upstreams (update here and in `docs/PROGRESS.md` together):

| Component | Revision |
|---|---|
| shadPS4 | `09f84a0e753d04db6581ccd79c680c6431f73389` |
| FEX | `ab135df091d005cc98dfddc56c3dc9c9e4bd501d` (move to the nearest release tag in Phase 3) |

## Patching `core/shadPS4`

- Prefer new files. Edits to upstream files are guarded by `#ifdef __ANDROID__` (OS behaviour) or `#ifdef ARCH_ARM64` (CPU behaviour; macro from `src/common/arch.h`).
- Approved seams: `src/common/arch.h`, `src/common/signal_context.cpp`, `src/common/path_util.cpp`, `src/core/tls.{h,cpp}`, `src/core/libraries/libs.h`, `src/core/linker.cpp`, `src/core/address_space.cpp`, `src/core/thread.cpp`, `src/emulator.cpp`, `src/video_core/renderer_vulkan/vk_platform.cpp`, `vk_instance.cpp`, `vk_swapchain.cpp`, `CMakeLists.txt`. Touching anything else needs a line in the phase plan saying why.
- Follow upstream style: its `.clang-format`, no game-specific or shader-specific hacks, minimal comments, rebase onto upstream (never merge upstream `main` into the patch branch).
- Anything stubbed to get a build is listed in `docs/STUBS.md` with file, symbol, reason and the phase that removes it.

## Build commands

Verified on a Linux x86-64 host (2026-11-12):

```sh
# fetch core/shadPS4 and its dependencies, then put our patch series on top
tools/fetch_core.sh
tools/apply_patches.sh

# host build and tests of our own code (currently the address-space probe)
cmake -S . -B build/host -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/host
ctest --test-dir build/host --output-on-failure

# stand-in AArch64 compile sweep of shadPS4 src/ (needs: pip install ziglang)
cmake -S core/shadPS4 -B build/standin -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/tools/standin/aarch64-linux-standin.cmake \
  -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF -DSDL_UNIX_CONSOLE_BUILD=ON \
  -DFETCHCONTENT_SOURCE_DIR_FMT=$PWD/core/shadPS4/externals/fmt \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 tools/standin/syntax_check.py build/standin build/sweep 2
```

Run in GitHub Actions on every push (`.github/workflows/android.yml`); the NDK is not available in the Claude sandbox:

```sh
# our code for Android
cmake -S . -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/android-arm64

# address-space probe on a device (experiment E1)
adb push build/android-arm64/platform/android/va_probe /data/local/tmp/
adb shell /data/local/tmp/va_probe

# static ARM64 probe without the NDK (runs from adb shell or Termux; verified to build, not run)
python3 -m ziglang c++ -target aarch64-linux-musl -static -std=c++20 -O1 -DVA_PROBE_STANDALONE \
  platform/android/probe/va_probe.cpp -o va_probe-arm64-static

# core for Android
cmake -S . -B build/android-core -G Ninja -DPS4DROID_BUILD_CORE=ON  # plus the toolchain flags above
```

Toolchain: **NDK r29 (29.0.14206865)**, required: r27 and r28 lack `std::jthread`, `std::stop_token` and `std::atomic_ref`, which shadPS4 uses (measured in CI run 43337640523), CMake 3.24+, Ninja, JDK 17, API 31 minimum, `-Wl,-z,max-page-size=16384` on every shared object (set in the top-level `CMakeLists.txt`).

## The submodule and the patch series

- The superproject pins `core/shadPS4` to the **upstream** commit. Our changes are `core/patches/*.patch`, applied by `tools/apply_patches.sh` onto a local branch `ps4droid/android`.
- To change the core: commit inside `core/shadPS4` on that branch, then `git -C core/shadPS4 format-patch -o ../patches <pinned-commit>` and commit the patch files.
- Never `git add core/shadPS4` after patching: that would pin a commit nobody else can fetch. `.gitmodules` sets `ignore = all` for it so `git add -A` leaves the pin alone; check with `git ls-files -s core/shadPS4`.

## Working method

- Before a phase: short plan plus the list of files to be touched. Wait for approval where the phase plan says STOP.
- Small commits, one logical change each, imperative subject, body says what was verified and how.
- Update `docs/PROGRESS.md` in the same commit as the work it describes.
- Tests: translator and platform tests live in this repo. Upstream does not accept in-tree tests; emulation-behaviour tests there are OpenOrbis homebrew.
- Test content: open-source PS4 homebrew (OpenOrbis samples) only. No commercial titles in automated tests or in the repo.

## Phases

0 Research and design (this commit) → 1 Toolchain → 2 Minimal app → 3 CPU translation → 4 Boot homebrew → 5 Graphics/audio/input/storage → 6 Frontend → 7 Performance. Each is verified before the next starts.
