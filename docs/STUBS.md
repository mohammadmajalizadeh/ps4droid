# Stubs and compile status

Last updated: 2026-11-12 (Phase 1, partial).

## How this was measured, and what it does not show

The Android NDK could not be downloaded in the environment where this work was done (`dl.google.com` is blocked there). Instead, shadPS4 at `09f84a0e` was configured for **generic AArch64 Linux (glibc)** with a stand-in cross-compiler (zig 0.17.1, which is clang 22.1.8) and every translation unit under `src/` was compiled at `-O0` with the objects discarded:

```sh
cmake -S core/shadPS4 -B build/standin -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/tools/standin/aarch64-linux-standin.cmake \
  -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF -DSDL_UNIX_CONSOLE_BUILD=ON \
  -DFETCHCONTENT_SOURCE_DIR_FMT=$PWD/core/shadPS4/externals/fmt \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 tools/standin/syntax_check.py build/standin build/sweep 2
```

What that proves: which source files have **CPU-architecture** problems on ARM64.

What it does not prove:

- Nothing about **bionic** (missing libc functions, headers, `pthread_*_np`, etc.).
- Nothing about the **NDK's compiler**. NDK r27 ships clang 18; the stand-in is clang 22, so C++23 library features could still be missing under the NDK.
- Nothing about **linking**. No objects were kept and third-party libraries were not built, so undefined references are unknown.
- Nothing at **run time**. No ARM64 code was executed.

The `ndk-core` job in `.github/workflows/android.yml` repeats the same sweep with the real NDK. That workflow has not been run.

## Result: 430 translation units

| Outcome | Count |
|---|---|
| Compiled unmodified | 409 |
| Failed: real source problem | 4 |
| Failed: file generated during a normal build was absent | 16 |
| Failed: host header absent from the stand-in sysroot | 1 |

After the patches in `core/patches/`, the 4 real failures compile (3 confirmed with the stand-in; 1 is an Android-only branch, see S4).

### Real source problems (patched)

| ID | File | Problem | Patch | What the patch does | Removed in |
|---|---|---|---|---|---|
| S1 | `src/common/signal_context.cpp` | `#error "Unsupported architecture"` in `GetRip`, `IsWriteError`, `IsExecuteError` | 0001 | Real implementation, not a stub: reads `pc` and the ESR record from the Linux AArch64 signal frame. Compiles; **not run**. | n/a (keep); verify on device in Phase 2 |
| S2 | `src/core/libraries/kernel/threads/exception.cpp` | `#error "ucontext_t conversion not implemented"` in `Ucontext::Ucontext` and `Ucontext::SyncHostFromGuest` | 0002 | **Stub.** Guest registers are left zero; only the fault address is filled. Guest exception handlers would see an empty context. | Phase 3 (state comes from the translator) |
| S3 | `src/core/aerolib/stubs.cpp` | Uses Xbyak 64-bit registers to emit x86 host code that jumps to a host function | 0003 | **Reduced behaviour.** Unresolved imports resolve to a 3-byte guest routine (`xor eax, eax; ret`). The per-call "stub called" log is lost; one warning is logged at resolve time instead. | Phase 3 (use the HLE call bridge so the log returns) |
| S4 | `src/core/libraries/kernel/kernel.cpp` | Includes `<uuid/uuid.h>`; bionic has no libuuid | 0004 | Real implementation under `__ANDROID__`: version 4 UUID from `arc4random_buf`. The logic was run on a Linux host. The `__ANDROID__` branch **has not been compiled** (the stand-in is not Android). | n/a (keep); confirm in the NDK job |

### Not source problems: generated files that a full build would create

These failed only because the sweep does not run build steps. They are listed because each generator is a **cross-compilation problem for the Android build**: upstream builds the generator for the target and then runs it, which cannot work when the target is ARM64 Android and the build machine is x86-64.

| Generated file | Needed by | Generator | Android build needs |
|---|---|---|---|
| `shadnet.pb.h` | 7 files in `src/core/libraries/np/` and `src/shadnet/client.cpp` | `protobuf::protoc` (`CMakeLists.txt:1318`) | A host-built `protoc` of the same version |
| `video_core/host_shaders/*.h` | `blit_helper.cpp`, `tile_manager.cpp`, `fault_manager.cpp`, `fsr_pass.cpp`, `pp_pass.cpp` | `glslang-standalone` target (`src/video_core/host_shaders/CMakeLists.txt:4-7`) | A host-built `glslang` |
| `imgui_fonts/*.ttf.g.cpp` | `src/imgui/renderer/font_data.cpp` | font embedding step | A host-built embedder |
| `pnglibconf.h` | `src/core/libraries/libpng/pngdec.cpp`, `pngenc.cpp` | libpng's own build | Nothing special; produced when libpng is built |

Planned fix (not done): a stage-0 native build of the three host tools, imported into the cross build when `CMAKE_CROSSCOMPILING` is set. This needs a patch to upstream CMake files and is the next piece of Phase 1.

### Host header absent from the stand-in

| File | Missing | Expected on Android |
|---|---|---|
| `src/video_core/renderer_vulkan/vk_platform.cpp` | `X11/Xlib.h` (the file selects Xlib/Wayland on Linux) | Should not occur: line 5 selects `VK_USE_PLATFORM_ANDROID_KHR` when `ANDROID` is defined. Unverified. The surface-creation code for Android is still missing (Phase 2). |

## Code that is excluded from ARM64 builds by upstream

These are not compiled on ARM64 at all (`CMakeLists.txt:1037-1042`), so the sweep says nothing about them and anything that calls into them will fail at link time or misbehave.

| Component | Files | Consequence | Phase |
|---|---|---|---|
| CPU patcher | `src/core/cpu_patches.cpp`, `.h` | No TCB-access rewriting, no SSE4a patching; replaced by the translator | 3 |
| Fibers | `src/core/libraries/fiber/` (`FIBER_LIB`), unregistered at `libs.cpp:207` | `libSceFiber` unavailable to games | 3 or later |
| Program entry | `RunMainEntry`, `src/core/linker.cpp:40` | Compiles, but is `UNREACHABLE` on ARM64: nothing can start | 3 |
| Module trampolines and static patching | `src/core/module.cpp:131, 182, 215, 345, 381` | Disabled on ARM64 | 3 |
| Guest varargs | `src/common/va_ctx.h:10` (`// FIXME`, `__m128` faked as `__int128`) | `printf`-family HLE functions read wrong arguments | 3 |

## Build-system problems found while configuring (not yet fixed)

| ID | Problem | Evidence | Plan |
|---|---|---|---|
| B1 | FFmpeg is a prebuilt download, selected as `ffmpeg-linux-arm64.zip` for any `UNIX` ARM64 target | `externals/ffmpeg-core/CMakeLists.txt:10-33, 42-48` | Those binaries target glibc Linux. Build FFmpeg with the NDK ourselves, or find out whether upstream publishes an Android artifact. |
| B2 | spdlog downloads fmt at configure time because it is added before the vendored fmt | `externals/CMakeLists.txt:24-36`, `externals/spdlog/cmake/fmtlib.cmake` | Worked around with `FETCHCONTENT_SOURCE_DIR_FMT` in our top-level `CMakeLists.txt`. Verified with the stand-in. |
| B3 | Host tools are built for the target | see table above | Stage-0 host tools build |
| B4 | SDL3 is unconditionally part of the build; on generic Linux it refuses to configure without X11/Wayland | `externals/sdl3/cmake/macros.cmake:431` | Stand-in only: `-DSDL_UNIX_CONSOLE_BUILD=ON`. For Android, SDL is to be replaced by `platform/android` (Phase 2/5); until then its Android backend expects a Java `SDLActivity`. |
| B5 | `externals/aacdec/fdk-aac` is fetched from `android.googlesource.com`, which may be unreachable | `.gitmodules:103-105` | `tools/fetch_core.sh` falls back to `github.com/mstorsjo/fdk-aac`, which has the identical commit `ee76460e` (same hash, so same content). |
| B6 | The link step adds `uuid` only when `CMAKE_SYSTEM_NAME` is `Linux` | `CMakeLists.txt:1383-1385` | Android's system name is `Android`, so this is skipped; consistent with S4. Unverified with the NDK. |
| B7 | Output is the `shadps4` executable | `CMakeLists.txt` | Add a shared-library target for JNI (Phase 2). |

## Open before Phase 1 can be called complete

1. Run the sweep with the real NDK r27 (CI job or a developer machine) and add any bionic failures here.
2. Host-tools stage (B3), then a full build including third-party libraries.
3. A link step, to surface undefined references from the excluded components.
4. NDK-built FFmpeg (B1).
