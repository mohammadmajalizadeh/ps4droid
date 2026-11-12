# Vulkan audit: what shadPS4 needs versus what Android drivers offer

Status: Phase 0 draft, awaiting approval.

Source audited: shadPS4 `09f84a0e`, chiefly `Instance::Instance` and `Instance::CreateDevice` in `src/video_core/renderer_vulkan/vk_instance.cpp`, plus `vk_platform.cpp`, `vk_swapchain.cpp`, `vk_pipeline_cache.cpp` and `src/shader_recompiler/profile.h`.

## How far to trust the driver columns

| Column | Basis |
|---|---|
| Turnip, PanVK | Mesa `docs/features.txt`, `main` branch, read on 2026-11-12 through a GitHub mirror (the canonical GitLab host is not reachable from my sandbox). Reliable for "is the extension implemented", not for individual feature bits. |
| Adreno proprietary, Mali proprietary | **Not verified.** I could not query gpuinfo.org or vendor documentation from here. Entries marked "expected" are my recollection of typical 2024–2026 drivers and must not be relied on. |

**Experiment E4 (Phase 2):** the minimal app dumps `vkGetPhysicalDeviceProperties2`, the full feature chain below and the extension list to a JSON file. One capture per test device replaces every "expected" in this document. Nothing in Phase 5 is scheduled against an unverified cell.

## 1. Hard requirements (startup aborts without them)

These are `ASSERT_MSG` in shadPS4, so a device that lacks one cannot start at all today.

| Requirement | Where | Turnip | PanVK | Adreno prop. | Mali prop. |
|---|---|---|---|---|---|
| Vulkan **1.3** (`TargetVulkanApiVersion`, `vk_platform.h:21`; assert at `vk_instance.cpp:179`) | core | Yes (A6xx+) | Yes | expected on A7xx/A8xx, older A6xx drivers are 1.1 | expected on Valhall-era drivers (G710+), older are 1.1 |
| `VK_KHR_swapchain` (`:258`) | ext | Yes | Yes | Yes | Yes |
| `VK_KHR_push_descriptor` (`:260`) | ext | Yes | Yes | expected yes | **doubtful**: the extension is absent from LunarG's published Android baseline profiles; per-device status [U] |
| `VK_EXT_vertex_attribute_divisor` (`:262`) | ext | Yes | Yes | expected yes | expected yes |
| `VK_EXT_robustness2` (`:265`) | ext | Yes | v10+ only | unknown | unknown |
| `robustBufferAccess2` (`:269`) | feature | [U] | **No**: Mesa's commit advertises the extension "with only the nullDescriptor feature enabled" | unknown | unknown |
| `robustImageAccess2` (`:271`) | feature | [U] | **No** (same commit) | unknown | unknown |
| `nullDescriptor` (`:273`) | feature | [U] | Yes | unknown | unknown |

**Conflict with the brief.** The brief says "Vulkan 1.1 minimum (1.3 preferred)". shadPS4 does not run below 1.3, and it is not only the version check: the renderer is written against 1.3 core (`dynamicRendering`, `synchronization2`, `maintenance4` in the `PhysicalDeviceVulkan13Features` block at `vk_instance.cpp:461-468`; `timelineSemaphore`, `bufferDeviceAddress`, `scalarBlockLayout` from 1.2). Back-porting to 1.1 means re-introducing render passes and framebuffers throughout `vk_rasterizer.cpp` and `vk_scheduler.cpp`, which is a rewrite that would never rebase onto upstream. **Recommendation: make 1.3 the floor.** This is decision 2 in `ARCHITECTURE.md`.

### Fallbacks for the hard requirements

| Missing | Fallback design | Cost |
|---|---|---|
| `VK_KHR_push_descriptor` | 10 call sites use `pushDescriptorSetKHR` (`vk_pipeline_common.cpp:37` is the main draw path; the rest are host passes: `fsr_pass.cpp`, `pp_pass.cpp`, `blit_helper.cpp`, `tile_manager.cpp`, `fault_manager.cpp`). Add a `DescriptorWriter` helper: with the extension it pushes; without it, it allocates a set from a per-frame linear pool (`vk_resource_pool.cpp` already has pool infrastructure), calls `updateDescriptorSets`, then binds. Layouts lose the `ePushDescriptorKHR` flag in that mode. | Moderate CPU cost per draw; contained in one helper. |
| `nullDescriptor` | Bind 1×1 dummy image views and a small zeroed buffer wherever the code currently writes `VK_NULL_HANDLE`. A null index buffer already exists (`buffer_cache.h:69`). Vertex-buffer null bindings need a dummy buffer too. | Low. |
| `robustBufferAccess2` / `robustImageAccess2` | Enable core `robustBufferAccess` (already requested when available) and `robustImageAccess` (1.3). The "2" variants add exact zero-on-out-of-bounds semantics. Where a title depends on that, the shader recompiler would have to emit explicit bounds checks (a `Profile` flag, as it does for other gaps). Start by downgrading the three asserts to warnings on Android and collecting which titles misrender. | Unknown until tested; correctness risk, not a startup blocker. |
| `VK_EXT_vertex_attribute_divisor` | Only needed when a divisor other than 0/1 is used (`vk_graphics_pipeline.cpp:75`). Without it, reject such pipelines with a logged warning. | Low; rare. |

All four are changes to upstream files, isolated behind `Instance` capability getters so the diff stays small.

## 2. Optional extensions (already degrade gracefully in shadPS4)

Each is probed with `add_extension` at `vk_instance.cpp:277-363` and exposed through a getter in `vk_instance.h`. The last column cites the consumer that checks the getter, which is the evidence that absence is tolerated. I verified that each guard exists; I did **not** verify how good the result looks when the guard takes the fallback branch.

| Extension | Turnip | PanVK | Guarded consumer (evidence that absence is handled) |
|---|---|---|---|
| `VK_KHR_maintenance5` | Yes | Yes | `vk_rasterizer.cpp:600` |
| `VK_KHR_maintenance6`, `VK_KHR_maintenance8` | Yes | Yes | Feature structs unlinked when absent (`vk_instance.cpp:575-583`); consumers not audited |
| `VK_EXT_attachment_feedback_loop_layout` + `_dynamic_state` (both or neither, `:280-288`) | Yes | Yes | `texture_cache/image.cpp:32`, `vk_scheduler.cpp:431`, `vk_rasterizer.cpp:984` |
| `VK_EXT_depth_range_unrestricted` | **No** | **No** | `vk_rasterizer.cpp:1420` |
| `VK_EXT_extended_dynamic_state3` (colour write mask) | Yes | Yes | `vk_scheduler.cpp:423`, `vk_graphics_pipeline.cpp:159`, `:324` |
| `VK_EXT_custom_border_color` | Yes | Yes | `texture_cache/sampler.cpp:22` |
| `VK_EXT_depth_clip_control` | Yes | Yes | `vk_pipeline_cache.cpp:163`, `vk_graphics_pipeline.cpp:140` |
| `VK_EXT_depth_clip_enable` | Yes | Yes | `vk_graphics_pipeline.cpp:102`, `:120` |
| `VK_EXT_vertex_input_dynamic_state` | Yes | Yes | `vk_pipeline_cache.cpp:645`, `vk_rasterizer.cpp:474`, `:556` |
| `VK_EXT_primitive_topology_list_restart` | Yes | Yes | `vk_rasterizer.cpp:1592` |
| `VK_AMD_shader_explicit_vertex_parameter`, else `VK_KHR_fragment_shader_barycentric` (`:313-318`) | **No** (neither listed for `tu`) | No | `Shader::Profile` flags set at `vk_pipeline_cache.cpp:359-361` |
| `VK_EXT_provoking_vertex` | Yes | Yes | `vk_graphics_pipeline.cpp:117` |
| `VK_EXT_shader_stencil_export` | Yes | Yes | `Profile::supports_shader_stencil_export` |
| `VK_AMD_shader_image_load_store_lod`, `VK_AMD_gcn_shader`, `VK_AMD_shader_trinary_minmax` | AMD vendor extensions; not checked, assume absent | same | `Profile::supports_image_load_store_lod`, `supports_native_cube_calc`, `supports_trinary_minmax` |
| `VK_NV_framebuffer_mixed_samples`, `VK_AMD_mixed_attachment_samples` | not checked, assume absent | same | `IsMixedDepthSamplesSupported()`, `IsMixedAnySamplesSupported()` |
| `VK_EXT_shader_atomic_float` | Yes | Yes | `Profile::supports_*_fp32_atomic_min_max` |
| `VK_EXT_shader_atomic_float2` | **No** | **No** | same Profile flags |
| `VK_KHR_workgroup_memory_explicit_layout` | Yes | Yes | `Profile::supports_workgroup_explicit_memory_layout` |
| `VK_EXT_image_2d_view_of_3d` | Yes | Yes | `texture_cache/image.cpp:146` |
| `VK_EXT_image_view_min_lod` | Yes | Yes | `texture_cache/image_view.cpp:101` |
| `VK_EXT_memory_budget` | Yes | Yes | `CanReportMemoryUsage()` |
| `VK_KHR_shader_clock` | Yes | Yes | `Profile::supports_shader_subgroup_clock` |
| `VK_EXT_calibrated_timestamps` | n/a | n/a | Tracy builds only (`:369`) |

Reading of this table: **on Turnip, every optional extension shadPS4 probes is either implemented by the driver or sits behind an upstream capability check.** The prior art's public compatibility reports are all Snapdragon + Turnip, which is consistent.

## 3. Core features that are requested but not asserted

`CreateDevice` copies these from the device (`vk_instance.cpp:413-468`) without checking them. Some consumers are guarded, some are not.

| Feature | Guarded? | Mobile concern |
|---|---|---|
| `geometryShader` | Yes: draw skipped with a warning (`vk_pipeline_cache.cpp:582`, `:615`) | Mali has historically not exposed geometry shaders in Vulkan [U]. Affected draws vanish. |
| `tessellationShader` | Yes (`:598`, `:612`) | Same class of issue. |
| `depthBounds` | Yes (`vk_scheduler.cpp:304`, `vk_graphics_pipeline.cpp:155`) | Often absent on mobile. |
| `logicOp` | Yes (`vk_graphics_pipeline.cpp:390`) | Often absent on tilers. |
| `shaderStorageImageMultisample` | Yes (`texture_cache/image.cpp:39`) | |
| `samplerAnisotropy`, `shaderClipDistance`, `shaderFloat64/16`, `shaderInt64/16/8` | Yes, via getters and `Shader::Profile` | `shaderFloat64` is commonly missing on mobile; the profile flag exists. |
| `drawIndirectCount` (1.2) | **No**: used directly at `vk_rasterizer.cpp:314` | Add a guard and a CPU-side loop fallback. |
| `dualSrcBlend`, `independentBlend`, `multiViewport`, `imageCubeArray`, `sampleRateShading`, `multiDrawIndirect`, `depthClamp`, `depthBiasClamp`, `fillModeNonSolid`, `wideLines` | Not individually audited | Phase 5: grep each consumer and add a capability gate or a logged degrade. Listed as open work, not as done. |
| `bufferDeviceAddress` (1.2) | Yes: logs "direct memory access cannot be used" (`:402`), `Profile::support_bda` | DMA is a performance feature; fine without. |
| `sparseBinding`, `sparseResidencyBuffer` | Used by `buffer_cache` / `fault_manager.cpp` | Sparse support on mobile is poor [U]; confirm the non-sparse path is complete. |
| `textureCompressionBC` | **Not requested and no software decoder in the tree** (formats mapped directly in `liverpool_to_vk.cpp:506-512`) | PS4 titles use BC1–BC7 everywhere. Turnip supports BC; Adreno proprietary is expected to; Mali proprietary is expected **not** to [U]. Without it we need a CPU or compute-shader BCn → RGBA8 decode in the texture cache. This is the largest single piece of graphics work for non-Adreno devices. |

## 4. Instance, surface and swapchain

| Item | Upstream | Android change |
|---|---|---|
| Surface | `CreateSurface` handles Win32, Xlib, Wayland, Metal (`vk_platform.cpp:67-112`). `VK_USE_PLATFORM_ANDROID_KHR` is defined at line 5 but unused. | Add `WindowSystemType::Android`, `VK_KHR_android_surface` in `GetInstanceExtensions`, `createAndroidSurfaceKHR(ANativeWindow*)`. |
| Loader | Bundled `externals/vulkan-loader` on some platforms | Link NDK `libvulkan.so`. For custom drivers, obtain `vkGetInstanceProcAddr` from libadrenotools and hand it to vulkan-hpp's dynamic dispatcher. |
| Pre-rotation | `preTransform = capabilities.currentTransform` (`vk_swapchain.cpp:66`, `:246`) with no rotation of the rendered image | On a landscape activity over a portrait panel `currentTransform` is 90°/270°, so the picture would come out rotated. First version: request `eIdentity` when `supportedTransforms` allows and let the compositor rotate. Later: rotate in the final present pass (`host_passes/pp_pass.cpp`) for the power saving. |
| Colour space / HDR | Requests `eHdr10St2084EXT` when HDR is on (`vk_swapchain.cpp:18`); needs `VK_EXT_swapchain_colorspace` | Keep SDR (`eSrgbNonlinear`, `:172`) by default; HDR later. |
| Present modes | FIFO / Mailbox / Immediate selection (`:197-216`) with FIFO fallback | Works; Android usually offers FIFO and Mailbox. |
| Surface loss | Acquire handles `eErrorSurfaceLostKHR` and `eErrorOutOfDateKHR` (`:115-116`) | The `VkSurfaceKHR` itself must be destroyed and recreated when Android destroys the `Surface` (app backgrounded). Add `Presenter::OnSurfaceDestroyed/Created`. |
| Validation | `VK_EXT_debug_utils`, layer settings | Ship validation layers only in debug APKs (`jniLibs`). |
| Driver workarounds | Keyed on `vk::DriverId` (Nvidia, Intel, AMD, KosmicKrisp: `vk_instance.cpp:199`, `:781`; `vk_pipeline_cache.cpp:362-367`) | Add `eQualcommProprietary`, `eMesaTurnip`, `eArmProprietary`, `eSamsungProprietary` rows as real bugs are found. No speculative workarounds. |

## 5. Custom driver loading

- Use libadrenotools to load a user-supplied Turnip build from app-private storage (it redirects the loader inside our process and supports a file-redirect dir). The driver is **user-supplied or downloaded by the user's explicit action**; we do not bundle it without checking its licence terms (Mesa is MIT, so bundling is permissible, but binary provenance matters).
- Qualcomm only. There is no equivalent for Mali or Xclipse, so those devices are limited to the system driver.
- libadrenotools needs the app process to be a normal bionic process. This is one reason to prefer the in-process model in `ARCHITECTURE.md` decision 3. Its licence must be confirmed when it is added. [U]

## 6. Device support tiers (proposal)

| Tier | Devices | Expectation |
|---|---|---|
| 1 | Snapdragon with Adreno 7xx/8xx + Turnip | Primary development and test target. Everything in sections 1–2 is present or has an upstream fallback. |
| 2 | Adreno 7xx/8xx on the stock Qualcomm driver | Should start if E4 confirms section 1. Expect driver-specific bugs. |
| 3 | Mali (Immortalis-G715+), Xclipse | Requires the push-descriptor fallback, probably the robustness2 fallback, probably BCn decode, and loses geometry-shader draws. Supported only after tier 1 works, and only if E4 on a real device says it is feasible. |
| Unsupported | Anything without Vulkan 1.3 | Clear message in the app at first launch, not a crash. |

The app runs the E4 probe at startup and shows the tier with the specific missing items.

## 7. Open items

1. E4 captures for every device we own (blocks all "expected"/"unknown" cells).
2. Confirm Turnip reports all three robustness2 features as true.
3. Audit the unguarded core features in section 3.
4. Decide whether BCn decode is CPU (simple, slow uploads) or compute (fast, more code).
5. Decide the Vulkan floor (decision 2).
