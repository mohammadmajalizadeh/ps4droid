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

The Phase 0 recommendations are in force because the user said to proceed without changing them: FEXCore; Vulkan 1.3 floor; in-process bionic gated on E1; cherry-pick MIT bridge files from the existing fork; import into app-specific storage. Nothing built so far depends on any of them, so they can still be changed at no cost. Test devices: still unknown.

## Next steps, in order

1. Run `.github/workflows/android.yml` (or the NDK commands in `CLAUDE.md`) and record the real results in `STUBS.md`.
2. Run `va_probe` on a 39-bit and a 48-bit device, standalone and inside an app process (E1).
3. Stage-0 host tools (`protoc`, `glslang`, font embedder) so the Android configure can generate sources (`STUBS.md` B3).
4. NDK-built FFmpeg (`STUBS.md` B1).
5. Full build and link of the core as a shared library; list undefined references.

## Open experiments

| ID | Question | Status |
|---|---|---|
| E1 | Can the guest ranges be reserved inside an ART process on 39-bit and 48-bit devices? | Tool written and host-tested; not run on Android |
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
