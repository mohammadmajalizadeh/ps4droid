# Translator decision: FEXCore vs Box64

Status: Phase 0 draft, awaiting approval.

**Recommendation: FEXCore**, pinned to a release tag, built as a static library with a small out-of-tree patch for bionic. Box64 is the documented fallback.

Sources read: FEX at `ab135df0` and Box64 at `4dbba130` (both cloned 2026-11-12). **[V]** means verified in the cited file; **[U]** means not verified and names how it will be checked.

## What the translator has to do for us

This is not the job either project was built for. Both are Linux user-mode emulators: they load a Linux x86-64 ELF and translate its Linux syscalls. We need neither. shadPS4 loads the PS4 ELF and implements the PS4 system libraries itself. We need only:

1. Translate x86-64 code already sitting in memory at a fixed (identity-mapped) address, one host thread per guest thread.
2. A cheap exit from guest code into a host function (HLE call), with access to guest registers.
3. Re-entrant host → guest calls (callbacks, thread entry, signal handlers).
4. Guest FS base under our control (PS4 TCB).
5. Guest state reconstruction from a host signal context (GPU page-fault tracking, guest exception delivery).
6. Code invalidation when guest executable memory is written, unmapped or patched.
7. Jaguar's instruction set: SSE–SSE4.2, SSE4a, AVX, F16C, BMI1, AES, PCLMULQDQ, MOVBE, POPCNT, LZCNT. Not AVX2, not FMA, not BMI2.
8. Bionic, API 31+, inside or beside an ART process.

## Comparison

| Criterion | FEXCore | Box64 |
|---|---|---|
| Licence | MIT (`LICENSE`) [V]. Compatible with GPL-2.0-or-later; keep the notice. | MIT (`LICENSE`) [V]. Same. |
| Bundled third-party code | vixl (BSD-3), SoftFloat-3e (BSD-3), cephes, xxhash, fmt, rpmalloc, tiny-json (`External/`) [V]. All permissive; record each in `NOTICE`. jemalloc is avoidable (see below). | Largely self-contained C. |
| Shape | C++20 library with an abstract `FEXCore::Context::Context` (`FEXCore/include/FEXCore/Core/Context.h`) [V]. The Linux frontend is a separate tree (`Source/Tools`). A second frontend already exists for Wine (`Source/Windows/ARM64EC`, `WOW64`) [V], which shows the core runs without the Linux syscall layer. | A C executable. Global singleton `my_context` is referenced 1125 times across `src/` [V]. ELF loading, wrapped libraries, signals and the dynarec are interleaved. A Wine WOW64 build exists (`wine/wow64`, CMake `WOW64`) [V], so separation is possible, but there is no library API. |
| Guest → host call hook | `SetSyscallHandler(FEXCore::HLE::SyscallHandler*)`; `HandleSyscall(CpuStateFrame*)` receives full guest state [V]. Also thunks (`SetThunkHandler`, `AddThunkTrampolineIRHandler`). | `AddBridge(bridge, wrapper_t, fnc, ...)` (`src/include/bridge.h`) [V]: a bridge is a guest-callable address that runs a `wrapper_t(x64emu_t*, fnc)`. Conceptually a very good fit for HLE functions. |
| Host → guest call | `Context::HandleCallback(Thread, RIP)` [V]. | `RunFunction(fnc, nargs, ...)` (`src/include/callback.h`), `EmuCall`/`DynaCall` [V]. |
| Threads | `CreateThread(CPUState*)`, `ExecuteThread`, `DestroyThread` [V]: we own the host thread. | Thread creation is tied to its wrapped pthread implementation. |
| FS base | `CPUState::fs_cached` (`CoreState.h:136`) [V]. | Emulated segment state in `x64emu_t`; settable, but through internals. |
| Signal-time state reconstruction | `RestoreRIPFromHostPC`, `ReconstructCompactedEFLAGS`, `ReconstructXMMRegisters`; pluggable `SignalDelegator` [V]. | Has its own signal layer (`src/os/emit_signals_linux.c`) built around emulating Linux signals for the guest; we would have to cut into it. |
| Code invalidation | `InvalidateCodeBuffersCodeRange`, `SyscallHandler::InvalidateGuestCodeRange`, `MarkGuestExecutableRange` [V]. | Dynablock invalidation via protection tracking in `custommem.c`; internal API. |
| AVX | Advertised unconditionally on real ARM64 hosts (`Source/Common/HostFeatures.cpp:551`) and implemented on 128-bit NEON (`OpcodeDispatcher/AVX_128.cpp`) [V]. No SVE needed. | ARM64 dynarec has AVX files (`dynarec_arm64_avx*.c`) [V]; enabled by `BOX64_AVX` level (`src/os/my_cpuid.c`). |
| SSE4a | In the JIT (`HostFeatures.SupportsSSE4a`, CPUID bit at `CPUID.cpp:1092`, handlers in `OpcodeDispatcher/Vector.cpp`) [V]. | CPUID bit is set (`my_cpuid.c:404`), but `EXTRQ`/`INSERTQ`/`MOVNTSS`/`MOVNTSD` exist only in the interpreter (`src/emu/x64run660f.c:1818`, `x64runf20f.c:332`, `x64runf30f.c:101`); no ARM64 dynarec implementation [V]. Each use drops out of the dynarec block. shadPS4 upstream patches exactly these four instructions (`cpu_patches.cpp:941`), which says games do use them. |
| Missing-instruction behaviour | No general interpreter (only helper fallbacks under `Interpreter/Fallbacks`). An unimplemented opcode is a guest SIGILL. | Full interpreter fallback (`src/emu/`): slower but keeps running. A real robustness advantage. |
| x87 | 80-bit soft-float (SoftFloat-3e) with a reduced-precision option. | Doubles by default, with precision options. PS4 code is SSE/AVX; x87 is rare (`long double`). |
| Memory ordering (TSO) | Per-range and per-instruction TSO control (`AddForceTSOInformation`), hardware TSO hook (`SetHardwareTSOSupport`) [V]. | `BOX64_DYNAREC_STRONGMEM` levels 0–4 (`src/include/env.h:72`) [V]. |
| Performance | Optimising IR JIT with register allocation and flag elimination. | Fast splatter-style dynarec with flag deferral. I have no trustworthy numbers comparing the two, and none at all for PS4 workloads. **[U]**: measure on the Phase 3 test binaries; benchmark Box64 only if FEX misses the exit criteria below. |
| Android / bionic | No Android references anywhere in the tree (searched for `ANDROID`, `bionic`) [V]. The FEX wiki FAQ says FEXCore can be built for an Android AArch64 target as a library. Glibc-specific pieces are optional (`ENABLE_JEMALLOC_GLIBC_ALLOC`, `ENABLE_FEX_ALLOCATOR`; `FEXCore/Source/Utils/AllocatorOverride.cpp`) [V]. **[U]**: bionic build is unproven by us; first task of Phase 3. | First-class: CMake `ANDROID`, `TERMUX` options, bionic handling throughout [V]. |
| Maintenance | Very active (commit the day before cloning), monthly tagged releases, commercially funded. | Very active (commit on the day of cloning), essentially single-maintainer. |
| Evidence for this exact use | `zenithblue-oss/shadps4-arm64` embeds FEXCore statically in shadPS4 and reaches in-game on Android (`src/core/fex/fex_guest_engine.cpp`) [V]. A PS5 emulator port ("sharpdroid") is described publicly as doing the same. The same Android project previously shipped Box64 and moved to FEX. | The earlier Bachata S4 releases, later replaced. |

## Why FEXCore

1. **It is a library with the right seams.** Every one of requirements 1–6 maps to an existing public virtual on `Context`, `SyscallHandler` or `SignalDelegator`. With Box64 the same hooks exist only as internals of a monolithic executable, so we would be maintaining a fork.
2. **SSE4a in the JIT.** On Box64 every Jaguar-specific instruction falls back to the interpreter.
3. **It has been demonstrated for this emulator.** That removes most of the feasibility risk.
4. **Upstream-trackable.** We consume FEX as a pinned submodule plus a patch that only touches the build (the prior art's `runtime/patches/fex-fexcore-only.patch` is CMake-only [V]).

## What we give up

- Box64's interpreter fallback. Mitigation: log the faulting opcode with guest RIP, keep a table in `docs/PROGRESS.md`, and upstream fixes to FEX.
- Box64's ready-made bionic support. Mitigation below.

## Integration design (for Phase 3)

```
translator/
  include/translator/guest_cpu.h   our narrow interface; nothing else in the tree includes FEX headers
  src/fex_engine.cpp               Context creation, HostFeatures, thread lifecycle
  src/fex_syscalls.cpp             SyscallHandler: HLE dispatch, executable-range queries, invalidation
  src/fex_signals.cpp              SignalDelegator glue to platform/android/signals
  src/hle_stub.cpp                 emits the per-import guest stub
  tests/                           standalone x86-64 test blobs (integer, SSE2–4.2, SSE4a, AVX, F16C, BMI1, AES)
```

- **Guest → HLE:** each import resolves to a 16-byte guest stub `mov eax, <index>; syscall; ret` in a stub page. `HandleSyscall` looks up the index and calls the compile-time adapter generated by `HostCallWrapperImpl` (see `ARCHITECTURE.md` 3.2). A real PS4 `syscall` instruction from guest code is distinguishable because it does not originate in the stub page.
- **HLE → guest:** `Core::CallGuest()` → `HandleCallback`. Nested and re-entrant.
- **Threads:** `CreateThread` in the host thread that upstream already creates per guest thread; set `fs_cached`; `ExecuteThread`.
- **Isolation rule:** FEX types never leak outside `translator/`. If we ever have to swap in Box64, only this directory changes.

## Bionic risks and how each is retired

| Risk | Check |
|---|---|
| FEXCore does not compile against bionic/libc++ (NDK r27+) | Phase 3 task 1: build `libFEXCore.a` with the NDK toolchain, allocator hooks off. Report the real compiler output. |
| FEX allocator assumes it can place memory anywhere; we need it to stay out of the guest range | Reserve the guest address space before creating the context. |
| FEX `SignalDelegator` versus ART `libsigchain` | `ARCHITECTURE.md` experiment E2. |
| `HostFeatures` detection reads `/proc/cpuinfo` and `getauxval`; some are restricted on Android | Fill `HostFeatures` ourselves from `getauxval(AT_HWCAP*)`. |
| Heterogeneous cores with different feature sets | Detect per-cluster and use the intersection. |

## Exit criteria for the decision

FEXCore stays unless, by the end of Phase 3, any of these is true:

- it cannot be built for bionic without patching non-build source files in more than a handful of places;
- the unit tests for the Jaguar instruction list fail in ways upstream will not fix;
- HLE call overhead through the syscall path exceeds ~100 ns per call on a Cortex-X class core (measured, not assumed).

If any trips, we stop and re-open this document with measurements before touching Box64.
