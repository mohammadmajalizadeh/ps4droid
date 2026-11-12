# Architecture: porting shadPS4 to Android (arm64-v8a)

Status: Phase 0 draft, awaiting approval. No code has been written.

Everything below was read from source at these commits (cloned 2026-11-12):

| Repo | Commit | Licence |
|---|---|---|
| shadps4-emu/shadPS4 | `09f84a0e753d04db6581ccd79c680c6431f73389` | GPL-2.0-or-later |
| FEX-Emu/FEX | `ab135df091d005cc98dfddc56c3dc9c9e4bd501d` | MIT |
| ptitSeb/box64 | `4dbba1300a8d74c7c385f0d8a4e453c740622f9f` | MIT |
| zenithblue-oss/shadps4-arm64 (prior art) | `be6bc2e9c60799e071dd2fafa6216e8d80ec619c` | GPL-2.0-or-later, bridge files MIT |

Markers used in this document:

- **[V]** verified by reading the cited file/function.
- **[U]** unverified. Either it needs a real device, or I am relying on memory. Each one has a named experiment.

Paths are relative to the shadPS4 repo root unless prefixed.

## 1. What shadPS4 is, structurally

| Subsystem | Location | Platform-specific? |
|---|---|---|
| Entry / orchestration | `src/main.cpp`, `src/emulator.cpp` (`Emulator::Run`, line 290) | Yes: creates `Frontend::WindowSDL` (line 610), CLI args, desktop paths |
| Window | `src/sdl_window.cpp`, `src/sdl_window.h` (`WindowSystemType`: Headless, Windows, X11, Wayland, Metal) | Yes: SDL3, no Android case |
| Guest address space | `src/core/address_space.cpp`, `src/core/memory.cpp` | Yes: heavily |
| ELF/module loader, linker | `src/core/module.cpp`, `src/core/linker.cpp`, `src/core/loader/` | Partly: x86-only entry trampoline and patching |
| CPU patching | `src/core/cpu_patches.cpp` (2724 lines, Xbyak + Zydis) | x86-64 only; excluded from the build on arm64 (`CMakeLists.txt:1037`) |
| TLS | `src/core/tls.cpp` | Yes: per-OS, with a generic "POSIX non-x86_64" branch |
| Signals / exceptions | `src/core/signals.cpp`, `src/common/signal_context.cpp`, `src/core/libraries/kernel/threads/exception.cpp` | Yes: reads x86 `ucontext` registers |
| Host threads | `src/core/thread.cpp`, `src/core/libraries/kernel/threads/` | Partly |
| HLE libraries | `src/core/libraries/**` (5591 `LIB_FUNCTION` registrations) | Mostly portable C++, but the calling convention is the problem (section 3.2) |
| GPU emulation | `src/video_core/**`, `src/shader_recompiler/**` | Vulkan only; see `VULKAN_AUDIT.md` |
| GPU memory tracking | `src/video_core/page_manager.cpp` | Yes: userfaultfd or `mprotect` + SIGSEGV |
| Audio | `src/core/libraries/audio/` (`sdl_audio_out.cpp`, `openal_audio_out.cpp`, `sdl_audio_in.cpp`) behind `audioout_backend.h` | Backend interface exists |
| Input | `src/input/` (SDL gamepad/keyboard/mouse) | Yes: SDL |
| UI overlay | `src/imgui/` (SDL3 + Vulkan backends) | Yes: SDL |
| Host networking | `src/core/net/host_net.cpp` (epoll/eventfd) | Linux path should work on bionic [U] |
| Paths / config | `src/common/path_util.cpp` (`XDG_DATA_HOME`/`HOME`, line 97) | Yes |
| File system | `src/core/file_sys/` (`std::filesystem` on host paths) | Yes: incompatible with SAF content URIs |

Two facts shape the whole port:

1. **There is no CPU emulator.** `Linker::Execute` → `RunMainEntry` (`src/core/linker.cpp:40`) builds the initial stack in inline x86 asm and `jmp`s to the game's entry point. On any other architecture it hits `UNREACHABLE_MSG("RunMainEntry unimplemented for current architecture.")`. [V]
2. **Upstream already has partial non-x86 scaffolding.** `src/common/arch.h` defines `ARCH_ARM64`; `tls.cpp`, `address_space.cpp`, `rdtsc.h`, `stack_asm.cpp` and `CMakeLists.txt:372` have arm64 branches, and comments say "instruction translation can handle offsetting to the base". It is incomplete (`signal_context.cpp` only handles Apple arm64, then `#error`), but it means our patches extend existing seams instead of inventing them. [V]

## 2. Prior art that changes the plan

Phase 0 research found that this port has been done at least once already, in the open:

- **Bachata S4** (`JICA98/Bachata-S4`, GPL-2.0-or-later, first alpha July 2026) with its core fork **`zenithblue-oss/shadps4-arm64`**. I cloned the fork and read it. It statically links **FEXCore** into an ARM64 shadPS4 (`src/core/fex/fex_guest_engine.cpp`, 1590 lines; `src/core/guest_cpu/fex_hle_bridge.cpp`, `hle_call_adapter.h`), caps `USER_MAX` at `0x3FFFFFFFFF` for 39-bit Android kernels (`src/core/address_space.cpp:49-51`), and bridges HLE calls through FEX's syscall handler with guest callbacks through `Context::HandleCallback`. Its early releases used Box64; it later moved to FEX. It runs the core inside a managed **Debian/glibc runtime** with an embedded X server and a Vulkan transport ("Vortek"), not as a bionic JNI library.
- News coverage from June 2026 describes a second unofficial Android port by a developer named "edeegg". I did not locate or read its source.

Consequences:

- The FEXCore route is proven to reach in-game on Snapdragon/Turnip devices. That is strong evidence for `TRANSLATOR_DECISION.md`.
- Because that fork is GPL-2.0-or-later, we may legally study it, and reuse code with attribution. Whether to do so is a decision for you (question 4 at the end).
- Our stated architecture (bionic `.so` loaded over JNI, `ANativeWindow` surface, no glibc rootfs) is **different** from the prior art and has not been demonstrated. The risky part is section 3.1.

## 3. What breaks on Android, and the fix for each

### 3.1 Guest address space (highest risk)

**How it works upstream [V].** `AddressSpace::Impl::Impl()` (POSIX version, `src/core/address_space.cpp:672`) reserves three fixed regions with one `mmap(MAP_FIXED | MAP_NORESERVE)`:

| Region | Range | Size |
|---|---|---|
| System managed | `0x400000` – `0x7FFFFBFFF` | ~32 GiB |
| System reserved | `0x7FFFFC000` – `0xFFFFFFFFF` | ~32 GiB |
| User | `0x1000000000` – `0x54FFFFFFFFFF` (Linux) | ~85 TiB |

Guest pointers are host pointers: HLE code dereferences them directly, so the mapping must be identity. Module load bases are `ExecutableLoadBase = 0x400000`, `GameModuleLoadBase = 0x80000000`, `SystemModuleLoadBase = 0x800000000` (`src/core/module.cpp:24-26`), passed to `MemoryManager::MapMemory` without `Fixed`, so they are search hints, not hard requirements. Physical memory is one `memfd_create("BackingDmem")` of `ORBIS_KERNEL_TOTAL_MEM_DEV_PRO + ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE` = 7936 MB + 512 MB (`address_space.cpp:64`, `libraries/kernel/memory.h`), mapped `MAP_SHARED` into guest ranges.

**What breaks on Android.**

| # | Problem | Fix | Status |
|---|---|---|---|
| A1 | 39-bit VA kernels give 512 GiB of user space in total. The 85 TiB user region cannot exist. | `#ifdef __ANDROID__` `USER_MAX = 0x3FFFFFFFFF` (256 GiB), the same value the prior art ships. Probe the real VA width at startup and widen on 48-bit kernels. Upstream already shrinks the user range to `0x10000000000` on Windows and for SDK >= 3.00 titles (`address_space.cpp:134-141`), so games tolerate a small range. | Measured on the test device 2026-11-13: 39-bit, 192 GiB user range reserves, larger ones fail |
| A2 | `MAP_FIXED` at `0x400000` silently destroys whatever is there. In a zygote-forked app process the ART heap and boot image are expected to occupy parts of the low 4 GiB. | Never use blind `MAP_FIXED` on Android. Parse `/proc/self/maps`, reserve with `MAP_FIXED_NOREPLACE` around existing mappings, and start the system-managed region at the first free address, as the Windows implementation already does (`address_space.cpp:150-189`). | **[U]** Experiment E1 |
| A3 | If ART holds too much of `0x400000`–`0xFFFFFFFF`, a non-PIE `ET_SCE_EXEC` eboot linked at `0x400000` cannot load. | Most eboots are `ET_SCE_DYNEXEC` (relocatable). If E1 shows the low range is unusable in-process, fall back to running the core in a child process started from `nativeLibraryDir` with a clean address space, sharing frames through `AHardwareBuffer`. That is close to the prior art's model. | **[U]** Decision point after E1 |
| A4 | Reserving hundreds of GiB as `PROT_READ|PROT_WRITE` may be refused by the overcommit policy. | Reserve as `PROT_NONE` and `mprotect`/remap on commit. | [U] E1 |
| A5 | 8.4 GiB `memfd`. Pages are shmem; on an 8–12 GiB phone a game that commits most of it will be killed by lmkd. | Size the backing file from `ActivityManager.MemoryInfo.totalMem`; expose a per-game setting; fail with a clear message, not a kill. `memfd_create` itself is in bionic since API 30, fine for API 31. | Design done; limits [U] |
| A6 | 16 KiB page-size devices (Android 15+). | PS4 pages are 16 KiB (`AddressSpace` `PAGE_BITS = 14`, `address_space.h:37`), so a 16K host is a natural fit; a 4K host also works. Audit every hard-coded `0x1000` host assumption (`Module::LoadModuleToMemory` uses `BlockAlign = 0x1000`). Link all our `.so` files with `-Wl,-z,max-page-size=16384`. | Audit in Phase 1 |

**Experiment E1 (Phase 1, before any porting effort):** a 50-line JNI probe that dumps `/proc/self/maps`, reports the VA width, and attempts the reservations above on at least one 39-bit and one 48-bit device. The result decides in-process versus child-process.

### 3.2 Executing guest code: calling convention bridge

**Upstream [V].** HLE functions are ordinary C++ functions marked `PS4_SYSV_ABI` (`__attribute__((sysv_abi))`, `src/common/types.h:28`). `LIB_FUNCTION` (`src/core/libraries/libs.h:13`) registers `HOST_CALL(function)`, which is `Core::HostCallWrapperImpl<func>::wrap` (`src/core/tls.h:52-60`), and the linker writes that address straight into the guest's relocation slot (`Linker::Relocate`, `linker.cpp:296`). Guest code calls it natively. In the other direction, HLE code calls guest function pointers directly, for example `Module::Start` (`module.cpp:104`) and thread entry via `_runOnAnotherStack` (`libraries/kernel/threads/pthread.cpp:277`).

**On ARM64** both directions cross an instruction-set boundary. `sysv_abi` has no meaning on AArch64, so host functions use AAPCS64.

| Direction | Fix |
|---|---|
| Guest → HLE (5591 functions) | Specialise `HostCallWrapperImpl` under `#ifdef ARCH_ARM64`. It already has the full C++ signature as template parameters, so it can generate, per function and at compile time, an adapter that reads SysV argument registers and stack slots from the translator's guest state and calls the host function. The address registered with the linker becomes a small guest-code stub that traps into the translator's host-call hook. This is one patch in `tls.h`/`libs.h`, not 5591 edits. The prior art does the same thing with a registry (`hle_call_adapter.h`). |
| HLE → guest callbacks | A single `Core::CallGuest(fn, args...)` helper that enters the translator (`FEXCore::Context::HandleCallback`). Every call site that invokes a guest pointer must be routed through it. There is no macro marking these sites today, so Phase 3 starts with an inventory; the count is **[U]**. As a safety net, guest code pages are never host-executable (upstream already maps them non-exec on non-x86: `AddressSpace::Map`, `address_space.cpp:854-860`), so a missed site faults immediately with a guest PC and is easy to find. |
| Variadic HLE functions (`printf` family) | Upstream captures SysV varargs with `src/common/va_ctx.h`, which has a `// FIXME` on non-x86. Rebuild `VaCtx` from guest registers in the adapter. |
| Entry | Replace the inline-asm `RunMainEntry` (`linker.cpp:40`) with: build the same initial stack in guest memory, set guest `rdi/rsi/rsp`, and start the translator at `entry_addr`. |
| Stack switching | `stack_asm.cpp` already has an AArch64 `_runOnAnotherStack` [V]; with a translator the guest stack is just a register value, so this may become unnecessary. |
| Fibers | `libSceFiber` is compiled out on non-x86 (`libs.cpp:207`, `CMakeLists.txt:1037`). Re-implement context switch as a guest-state swap. Listed in `STUBS.md` until then. |

### 3.3 CPU feature gaps and `cpu_patches.cpp`

`cpu_patches.cpp` exists to rewrite guest instructions the *host x86 CPU* cannot run or that need redirecting: FS-relative TCB access, SSE4a `EXTRQ`/`INSERTQ`/`MOVNTSS`/`MOVNTSD` (table at line 941), and Rosetta-specific cases. With a translator, none of this is patched; the translator must implement it instead:

- The Jaguar instruction set the translator must cover: SSE through SSE4.2, **SSE4a**, AVX (not AVX2), F16C, BMI1, AES, PCLMUL, MOVBE, POPCNT, LZCNT/ABM. See `TRANSLATOR_DECISION.md`.
- `Module` code under `#ifdef ARCH_X86_64` (`module.cpp:131, 182, 215, 345, 381`: trampolines, static patching, red-zone protection) stays disabled.
- Memory patches / cheats (`src/common/memory_patcher.cpp`) write guest bytes, so they must call the translator's code-invalidation API afterwards.

### 3.4 TLS

Upstream on Linux x86-64 sets the host `GS` base to the guest TCB (`SetTcbBase`, `tls.cpp`, `arch_prctl(ARCH_SET_GS)`) and patches guest `fs:[0]` reads. On non-x86 there is already a `pthread_key` fallback (`tls.cpp`, "POSIX non-x86_64") [V]. Fix: keep that fallback for host-side `GetTcbBase()`, and additionally set the translator's guest FS base (`CPUState::fs_cached` in FEXCore) to the TCB in `SetTcbBase`. No instruction patching.

### 3.5 Signals and exceptions

Upstream installs one `sigaction` handler for SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGTRAP/SIGABRT/SIGSYS/SIGUSR1/`SIGSLEEP` (= `SIGVTALRM`) (`SignalDispatch::SignalDispatch`, `signals.cpp`) and uses them for: GPU page-fault tracking, illegal-instruction patching, guest exception delivery, and thread suspend.

| # | Problem on Android | Fix |
|---|---|---|
| S1 | `signal_context.cpp` has no Linux/Android arm64 branch (`#error`). | Add one: `uc_mcontext.pc`, write/exec from ESR in the `esr_context` record of `uc_mcontext.__reserved`. |
| S2 | ART installs its own SIGSEGV/SIGBUS handlers through `libsigchain` for implicit null checks and stack overflow. Replacing them breaks the Java side. | Register through bionic's sigchain-aware `sigaction` (the default when linked normally), run only when the fault address or PC belongs to us (guest range, JIT code cache), and chain otherwise. Never call `RemoveHandlers` with `SIG_DFL` on Android. [U] E2: verify handler ordering with a Java NPE while the emulator runs. |
| S3 | The translator needs the same signals for its own purposes (SMC detection, guard pages). | One owner: `platform/android/signals` dispatches in a fixed order: translator JIT → `Signals::DispatchAccessViolation` handlers (page manager) → guest exception delivery → ART chain. |
| S4 | Guest exception delivery builds an x86 `Ucontext` from the host `ucontext` (`exception.cpp:37`). On ARM64 the host registers are not the guest's. | Reconstruct guest state from the translator (`RestoreRIPFromHostPC`, `ReconstructCompactedEFLAGS`, `ReconstructXMMRegisters` in `FEXCore/include/FEXCore/Core/Context.h`). Deliver guest handlers only at safe points, not from async-signal context. The prior art reached the same conclusion (`fex_guest_engine.cpp:941-996`). |
| S5 | `SIGSLEEP`/`SIGUSR1` thread suspend via `pthread_kill` (`debug_state.cpp`). | Works on bionic; make sure the chosen signal numbers do not collide with ART (it reserves some real-time signals) or the translator. [U] |
| S6 | `MINSIGSTKSZ` alt stacks (`NativeThread::Initialize`, `thread.cpp`): 64 KiB may be too small for JIT state reconstruction with SVE state. | Use `sysconf(_SC_SIGSTKSZ)` or `getauxval(AT_MINSIGSTKSZ)` with a floor. |
| S7 | `page_manager.cpp` prefers `userfaultfd`. Unprivileged userfaultfd is restricted on Android by SELinux and `vm.unprivileged_userfaultfd`. | It already falls back to the signal path when the syscall fails (line 293) [V]. Force the fallback on Android. |

### 3.6 W^X, SELinux, JIT memory

- `untrusted_app` may `mmap(PROT_EXEC)` anonymous memory (execmem), which is how other Android emulators JIT. Executing files from writable app storage is denied since API 29; the prior art's release notes record exactly that failure (`error=13 Permission denied`) and moved binaries into the APK's native-lib directory. So: all executable ELF code ships in `lib/arm64-v8a/` with `extractNativeLibs` semantics understood; JIT code lives in anonymous mappings. [U] E3: confirm on Android 12–16, including devices with MTE or vendor-hardened policies.
- Guest "executable" mappings are RW only on the host (section 3.2), so guest self-modifying code needs translator invalidation: route `MemoryManager::Protect`/`MapMemory`/`UnmapMemory` changes on exec ranges to `InvalidateGuestCodeRange`.
- `memfd` pages mapped `PROT_EXEC` are not required by this design.

### 3.7 Threads

- `pthread_create` with default attributes (`NativeThread::Create`); guest stacks are guest memory. Works on bionic.
- `NativeThread::Initialize` sets x86 MXCSR/FPUCW to Orbis defaults (`0x9fc0`, `0x037f`) under `ARCH_X86_64`. On ARM64 set the translator's guest MXCSR/FCW instead.
- bionic lacks `pthread_cancel` and some `pthread_*_np`; each use becomes a stub entry or a rewrite. Inventory in Phase 1 (compile errors enumerate them).
- Scheduling: pin guest threads to big cores via `sched_setaffinity`; request sustained performance mode / ADPF hints from the app. Phase 7.

### 3.8 Files, paths, storage

- `path_util.cpp` derives the user dir from `XDG_DATA_HOME`/`HOME`. Fix: Android branch that takes the directory from JNI (`Context.getFilesDir()` / `getExternalFilesDir()`).
- The VFS (`src/core/file_sys/`) uses `std::filesystem` and POSIX fds on real paths. SAF tree URIs are not paths. Options, in order of preference:
  1. Games live under the app-specific external directory (`Android/data/<pkg>/files/games`), which is a real path with no permission. The SAF picker is used to *import* (copy) a user's dump there.
  2. A `FileBackend` abstraction that opens SAF documents by fd (`ContentResolver.openFileDescriptor`) and enumerates through JNI. More code, slower directory operations, no copy.
  3. `MANAGE_EXTERNAL_STORAGE`. Simple, but Google Play only allows it for a short list of app categories, so it would likely block a Play release. [U] against current policy text.
  This is question 5 at the end.
- Input format: upstream has no PKG installer in `src/core/file_format/` (only `psf`, `trp`, `rif`, `npbind`, `playgo_chunk`, `pfs.h`) [V], so the app accepts extracted game folders. No decryption code or keys are added by us.
- Optional firmware modules: `Linker::Execute` loads `libSceLibcInternal.sprx` from `GetSysModulesDir()` if the user supplied it and otherwise uses HLE (`linker.cpp:79-90`) [V]. We ship none; the settings screen only points at a user folder.

### 3.9 Window, surface, swapchain

- Add `WindowSystemType::Android` and a `VK_KHR_android_surface` path in `vk_platform.cpp` (`CreateSurface`, `GetInstanceExtensions`). `vk_platform.cpp:5` already has `#if defined(ANDROID) #define VK_USE_PLATFORM_ANDROID_KHR` but no surface code behind it [V].
- Replace `Frontend::WindowSDL` with a small `Frontend::Window` interface implemented by `platform/android` (wraps `ANativeWindow`, forwards size changes and lifecycle). `Emulator::Run` constructs the window at `emulator.cpp:610`; that line gets the abstraction.
- Lifecycle: `surfaceDestroyed` must tear down the swapchain and pause presentation without killing the guest. `vk_swapchain.cpp:115-116` already treats `eErrorSurfaceLostKHR` and `eErrorOutOfDateKHR` as recreate-swapchain; what is missing is recreating the `VkSurfaceKHR` itself from the new `ANativeWindow`.
- Pre-rotation (`currentTransform`) and `VK_EXT_swapchain_colorspace` availability: `VULKAN_AUDIT.md`.

### 3.10 Audio, input, overlay

- Audio: implement `AudioOutBackend` (`audioout_backend.h`) with Oboe (AAudio). SDL and OpenAL backends are not built.
- Input: `src/input/controller.cpp` holds the guest pad state and is fed by SDL events in `sdl_window.cpp`. Feed it from JNI instead: `InputDevice`/`KeyEvent`/`MotionEvent` for physical pads, and the Compose touch overlay. Gyro and touchpad map to Android sensors and a touch region.
- ImGui: upstream's in-game overlay (`src/imgui/`) uses the SDL3 platform backend. Keep the Vulkan renderer, write a minimal Android platform backend (display size + touch), and leave "big picture" UI (`src/imgui/big_picture/`) unbuilt since Compose replaces it.

### 3.11 Build system

- `CMakeLists.txt` assumes desktop: it builds SDL3, OpenAL, Discord RPC, libusb, hwinfo, the Vulkan loader, an updater. The Android build needs option gates: `ENABLE_DISCORD_RPC=OFF`, `ENABLE_UPDATER=OFF`, no bundled `vulkan-loader` (link the NDK's `libvulkan.so`), `libusb` off or Android-flavoured, `hwinfo` replaced.
- `ARCHITECTURE` detection already maps `aarch64` → `arm64` (`CMakeLists.txt:56`) [V].
- Output is a shared library, not the `shadps4` executable: add a `shadps4_core` library target under `if(ANDROID)`.
- Every source file that cannot compile yet is recorded in `docs/STUBS.md` in Phase 1.

## 4. Target layout

```
core/shadPS4/        submodule, pinned; our changes as a rebased patch branch
translator/          FEXCore integration: guest engine, HLE bridge, callbacks
platform/android/    address-space probe, signals, window, audio (Oboe), input, paths, JNI
app/                 Kotlin + Compose: library, SAF import, settings, driver loading, overlay
docs/                this file, decisions, audits, PROGRESS, STUBS
```

Patch policy for `core/shadPS4`: additions go in new files where possible; edits to upstream files are guarded by `#ifdef __ANDROID__` or `ARCH_ARM64` and kept to the seams named above (`arch.h`, `tls.h`, `libs.h`, `linker.cpp`, `address_space.cpp`, `signal_context.cpp`, `vk_platform.cpp`, `emulator.cpp`, `path_util.cpp`, `CMakeLists.txt`).

Upstream contribution rules worth knowing (from the repo's `AGENTS.md`): no game-specific hacks, no tests in-tree (hardware tests are OpenOrbis homebrew), rebase and never merge `main`. Our translator unit tests therefore live in our repo, not in the submodule.

## 5. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Low address range unusable inside an ART process (A2/A3) | Medium | Forces the child-process model | E1 in week one of Phase 1 |
| Vulkan driver gaps on non-Turnip devices | High | Device support limited to Adreno + Turnip at first | `VULKAN_AUDIT.md`; capability gate with a clear message |
| Performance: Jaguar is slow, but translation overhead plus shader compilation on mobile is heavy | High | Many titles unplayable | Phase 7; set expectations: prior art reports single-digit numbers of titles in-game on flagship SoCs |
| Guest callback call sites missed | Medium | Crashes at specific library calls | Fault-on-exec safety net, inventory in Phase 3 |
| Memory pressure from the 8.4 GiB backing file | Medium | lmkd kills | A5 |
| Upstream churn in the files we patch | Medium | Rebase cost | Small seams, CI job that rebases weekly |
| ART signal chain interaction | Medium | Random Java crashes | E2 |

## 6. Decisions I need from you

1. **Translator:** approve FEXCore (recommended in `TRANSLATOR_DECISION.md`).
2. **Vulkan floor:** your brief says 1.1 minimum; shadPS4 asserts 1.3 at startup. Raise our floor to 1.3 (recommended), or fund a 1.1 back-port? Details in `VULKAN_AUDIT.md`.
3. **Process model:** try in-process bionic first with E1 as the gate (recommended, matches your brief), or adopt the prior art's separate-runtime model from day one?
4. **Prior art:** (a) independent port, reading `zenithblue-oss/shadps4-arm64` only as reference; (b) cherry-pick its MIT-licensed bridge files with attribution; (c) fork it and put our effort into a bionic/JNI frontend. Option (b) is my recommendation: least duplicated work while keeping "shadPS4 upstream as submodule with minimal patches".
5. **Storage:** copy-on-import into app-specific storage (recommended), SAF-backed VFS, or all-files access?
6. **Test devices:** which devices do you have? I need at least one Adreno 7xx/8xx device; a 39-bit-VA device and a Mali device would cover the two biggest unknowns.
