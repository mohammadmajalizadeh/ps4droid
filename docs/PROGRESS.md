# Progress

Last updated: 2026-11-12 (end of Phase 0).

## Done

- Phase 0: cloned and read shadPS4 `09f84a0e`, FEX `ab135df0`, Box64 `4dbba130`, and the existing ARM64 fork `zenithblue-oss/shadps4-arm64` `be6bc2e9`.
- `docs/ARCHITECTURE.md`, `docs/TRANSLATOR_DECISION.md`, `docs/VULKAN_AUDIT.md`, `CLAUDE.md` written.

## In progress

- Nothing. Waiting for approval of the Phase 0 documents.

## Blocked on decisions (see `ARCHITECTURE.md` section 6)

1. Translator: FEXCore (recommended) or Box64.
2. Vulkan floor: 1.3 (recommended) versus the 1.1 in the brief.
3. Process model: in-process bionic with experiment E1 as the gate (recommended) or separate runtime.
4. Use of prior art: reference only, cherry-pick MIT bridge files (recommended), or fork.
5. Storage model: import into app-specific storage (recommended), SAF-backed VFS, or all-files access.
6. Which test devices are available.

## Not done in Phase 0, deliberately

- No code, no build, nothing run on a device or emulator.
- The shadPS4 submodule has not been added to this repo yet; the pinned commit is recorded in `CLAUDE.md`. Adding it is the first step of Phase 1.

## Open experiments

| ID | Question | Phase | Blocks |
|---|---|---|---|
| E1 | Can the guest ranges be reserved inside an ART process on 39-bit and 48-bit devices? What already occupies the low 4 GiB? | 1 | Process model |
| E2 | Do our SIGSEGV/SIGBUS handlers coexist with ART's `libsigchain` and FEXCore's delegator? | 3 | Signals design |
| E3 | Anonymous `PROT_EXEC` JIT mappings on Android 12–16 across vendors | 3 | Translator |
| E4 | Per-device Vulkan capability capture | 2 | Every "expected"/"unknown" cell in `VULKAN_AUDIT.md` |

## Known gaps in the research

- Adreno and Mali proprietary-driver columns in `VULKAN_AUDIT.md` are unverified.
- Mesa data came from a GitHub mirror of `docs/features.txt`, not the canonical repository.
- The second reported Android port (by "edeegg") was not located or read.
- The number of HLE → guest callback call sites is not yet counted.
- FEXCore has not been built against bionic by us.
- Several Vulkan core features are requested without an audited guard (`VULKAN_AUDIT.md` section 3).

## Known issues

- None yet (no code).
