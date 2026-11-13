# Progress

Last updated: 2026-11-12 (Phase 1 partly done).

## Done

- Phase 0: design documents (`ARCHITECTURE.md`, `TRANSLATOR_DECISION.md`, `VULKAN_AUDIT.md`), `CLAUDE.md`.
- Phase 1, the part that needs no NDK:
  - `core/shadPS4` added as a submodule pinned to upstream `09f84a0e`; `tools/fetch_core.sh` fetches its 45 dependencies.
  - Stand-in AArch64 compile sweep of all 430 `src/` translation units: 409 compile unmodified. Full breakdown in `STUBS.md`.
  - Patch series `core/patches/0001`–`0004` for the four real source failures; `tools/apply_patches.sh` applies it.
  - Experiment E1 tool: `platform/android/probe/va_probe.cpp`, with a host test that simulates an occupied low address range. Passes on Linux x86-64; cross-compiles for AArch64.
  - Top-level `CMakeLists.txt`, `platform/android/CMakeLists.txt`, CI workflow `.github/workflows/android.yml` (written, never executed).

## In progress

- Phase 1 is **not complete**. It cannot be completed without the NDK.

## Blocked

- **Android NDK unavailable in the working environment** (`dl.google.com` denied by network policy, still denied after the user said it was allowed). Blocks: the real `arm64-v8a` build, every bionic finding, the link step, and running `va_probe` on a device.
- No Android device or emulator is attached, so E1–E4 are all still open.

## Decisions taken

The Phase 0 recommendations are in force because the user said to proceed without changing them: FEXCore; Vulkan 1.3 floor; in-process bionic gated on E1; cherry-pick MIT bridge files from the existing fork; import into app-specific storage. Nothing built so far depends on any of them, so they can still be changed at no cost. Test device: Nothing Phone (3a) Pro, see below. It is the only device, so 48-bit-versus-39-bit and Mali coverage remain open.

## Test device

Nothing Phone (3a) Pro (named by the user on 2026-11-12). From public spec sheets, not from the device:

| Item | Value | Consequence |
|---|---|---|
| SoC | Snapdragon 7s Gen 3 (SM7635): 1x Cortex-A720 2.5 GHz, 3x A720 2.4 GHz, 4x A520 1.8 GHz | A mid-range chip. Fine for bring-up and homebrew (Phases 2-4). Commercial titles at playable speed are doubtful; the public reports for the existing port are from flagship SoCs. |
| GPU | Adreno 810 (some spec sites say 710; the review and GPU databases say 810) | An 8xx-generation part: needs a Turnip build with Adreno 8xx support (reported as Mesa 26.0 and later). Stock-driver capabilities unknown until E4. |
| RAM | 8 GB or 12 GB depending on variant | Decides how large the guest memory backing file can be (`ARCHITECTURE.md` A5). Variant not yet known. |
| OS | Android 15 at launch (Nothing OS 3.1), updatable | API 35+, above our floor of 31. |
| Address-space width, page size | unknown | `va_probe` answers both. |

A static ARM64 build of `va_probe` (musl, no NDK needed) was produced with the stand-in compiler and handed to the user. It has not been executed on ARM64 by us. Standalone, it reports address-space width, page size, kernel and RAM; it does **not** show what the Android runtime maps into an app process, which needs the in-app run in Phase 2.

## Device facts measured on 2026-11-13

Raw log: `docs/device-logs/2026-11-13-nothing-3a-pro-va_probe.txt`.

| Item | Measured | Consequence |
|---|---|---|
| Model / OS | A059P, Android 17, kernel 6.1 (android14 GKI) | Far above API 31; nothing to change. |
| RAM | 11.1 GiB visible (12 GB variant) | Full 8.4 GiB guest backing file is possible on paper but leaves ~3 GiB for Android and the app; start with a smaller default and measure lmkd behaviour. |
| Page size | 4 KiB | No 16 KiB issues on this device (we still link for 16 KiB). |
| User address space | **39-bit** (highest mappable `0x7ffffff000`, 512 GiB total) | Upstream user range (85 TiB) and the 1 TiB fallback both fail with ENOMEM. 192 GiB (`USER_MAX = 0x3FFFFFFFFF`) reserves fine. This confirms `ARCHITECTURE.md` A1 and the value the existing fork ships. |
| System ranges | `system_reserved` (32 GiB) reserves whole; `system_managed` reserves from `0x1075000` onwards | The only occupant was the probe's own image: the static build is linked at `0x1000000`. An artifact of how the probe was built, not of Android. |
| Vulkan driver | `ro.hardware.vulkan=adreno` (Qualcomm proprietary) | Capabilities still unknown (E4). |

What this does **not** answer: the probe ran in the `shell` SELinux domain as a fresh process. An app process is forked from zygote and carries the Java runtime's mappings, which is exactly what A2/A3 worry about. That needs the in-app run (Phase 2). E1 is therefore half done.

## First GitHub Actions run (2026-11-12, run 43332753412, commit dbf772b)

| Job | Result | What it shows |
|---|---|---|
| host-tests | pass | probe test passes on ubuntu-24.04 |
| ndk-platform | pass | **First real NDK r27c build.** `va_probe` built for arm64-v8a, API 31; all four LOAD segments 16 KiB aligned (`0x4000`) |
| ndk-core | fail at configure | `src/imgui/renderer/CMakeLists.txt:6` used `CMAKE_SOURCE_DIR`, which is our wrapper, not shadPS4. Fixed in patch 0005; configure through the wrapper then verified with the stand-in toolchain. NDK re-run pending. |

The NDK installs and works on GitHub's runners, so the sandbox's download block no longer blocks the project.

## Next steps, in order

1. Re-run the workflow with patch 0005 and record the NDK compile sweep in `STUBS.md`.
2. Run `va_probe` inside an app process (Phase 2 app) on the test device (E1, second half).
3. Stage-0 host tools (`protoc`, `glslang`, font embedder) so the Android configure can generate sources (`STUBS.md` B3).
4. NDK-built FFmpeg (`STUBS.md` B1).
5. Full build and link of the core as a shared library; list undefined references.

## Open experiments

| ID | Question | Status |
|---|---|---|
| E1 | Can the guest ranges be reserved inside an ART process on 39-bit and 48-bit devices? | Standalone run on the 39-bit test device done (all three ranges fit with `USER_MAX = 0x3FFFFFFFFF`); in-app run pending; no 48-bit device |
| E2 | Do our signal handlers coexist with ART's `libsigchain` and FEXCore? | Not started (Phase 3) |
| E3 | Anonymous `PROT_EXEC` JIT mappings on Android 12–16 | Not started (Phase 3) |
| E4 | Per-device Vulkan capability capture | Not started (Phase 2) |

## Known gaps

- Everything listed under "What it does not prove" in `STUBS.md`.
- The AArch64 signal-frame parsing in patch 0001 compiles but has never executed.
- The CI workflow is untested.
- Phase 0 gaps still stand: proprietary Adreno/Mali driver data unverified; guest-callback call sites uncounted; FEXCore not yet built for bionic.

## Known issues

- A leftover scratch directory from an aborted first sweep exists outside the repo in the sandbox (`build/syntax`); it is not part of the project.
