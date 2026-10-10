# DroidDeck on non-Adreno GPUs through Venus (Pixel 10 Pro XL, PowerVR)

**Masuary fork, experimental3 candidate (2026-10-10):** the installed
experimental2 still shows stale Steam frames and black regions despite its
compositor ownership corrections. Experimental3 tests composing into a private
optimal RGBA image and copying/converting each completed frame to the LINEAR shared output,
avoiding storage writes to shared images. Its effect still needs an installed
app test. See [the phone investigation](pixel-powervr-investigation.md) for this
fork's build, screenshot and native probe results.
Experimental3 still flickers; experimental4 adds gamescope patch 0124, which keeps Venus sessions
on full composition even when Steam clears `GAMESCOPE_COMPOSITE_FORCE`, and stops gamescope
overwriting an output image the host compositor has not yet released.
Experimental4 still flickers, now showing pictures from before the session (the app's launch logo,
an older Steam frame): left-over memory, not late frames. Experimental5 adds a compositor frame
probe (`probe` lines in `wayland.log`) to tell whether the stale pictures arrive from gamescope or
come from the screen swapchain.

Status: **work in progress, not playable.** Steam's Big Picture sign-in screen renders on the Pixel
(gamescope on Venus/PowerVR, Steam UI GL on llvmpipe), but the display shows frozen snapshots of
gamescope's output buffers instead of live frames. This page tracks what works, what
does not, and why. Branch: [`pixel-venus`](https://github.com/gogetwoo/DroidDeck/tree/pixel-venus).

Test device: Google Pixel 10 Pro XL (Tensor G5, **PowerVR D-Series DXT-48-1536**, driver 25.3,
Vulkan 1.4, kernel driver `pvrsrvkm`), Android 17.

## Why stock DroidDeck fails here

DroidDeck's rootfs only has Turnip, which drives Adreno through KGSL. On anything else gamescope
logs `vulkan: failed to find physical device` and the session exits (`GUEST_EXIT`). The PowerVR
kernel driver is Imagination's proprietary DDK (`pvrsrvkm`), not upstream `drm/imagination`, so
Mesa's own PowerVR driver cannot be used either: the vendor driver has to be reached from the
glibc side somehow (see [#165](https://github.com/Droid-Deck/DroidDeck/issues/165) for the options).

## Approach: Venus over vtest (option 1 in #165)

- **Guest**: Mesa's Venus ICD (`libvulkan_virtio.so`, Arch Linux ARM `vulkan-virtio`, matched to the
  runtime's Mesa) serialises Vulkan over a unix socket (`VN_DEBUG=vtest`, `VTEST_SOCKET_NAME`).
- **Host**: virglrenderer's vtest server, built Venus-only with the NDK as `libvenusserver.so`,
  started by the app as a session part; it dlopens Android's `libvulkan.so` (the vendor driver).
- **App** (`gpu/Venus.kt`): chosen automatically on a non-Adreno GPU (`Download/droiddeck-venus`
  with `on`/`off` forces it); the ICD goes through the session script's `BL_VK_DRIVER` path. The
  compositor's Auto driver now picks the system Vulkan driver on a non-Adreno GPU (it used to fall
  through to `turnip-sdk36`). Build scripts and pinned sources: `tools/venus/`.

## Progress

| Step | Result |
|---|---|
| `vulkaninfo` in the rootfs | ✅ `Virtio-GPU Venus (PowerVR D-Series DXT-48-1536 MC1)`, Vulkan 1.4.317 |
| Compositor on the system driver | ✅ renders on PowerVR, dmabuf feedback on `226:128` |
| gamescope device selection | ✅ after gamescope patch 0120 |
| Mappable memory over vtest | ✅ after venus patch 0003 |
| gamescope Wayland backend + wlserver | ✅ |
| gamescope flippable (output) image | ✅ after gamescope patch 0121 (no mutable format on shared images) |
| Client buffers (Steam/Xwayland) into gamescope | ⚠️ via CPU copies: `DISABLE_GAMESCOPE_WSI=1`, `MESA_VK_WSI_DEBUG=sw` (no dma_buf import over vtest) |
| Steam webhelper on Zink/Venus | ❌ exits silently a few seconds after Big Picture shows; stays up on llvmpipe |
| Steam Big Picture sign-in screen | ✅ (llvmpipe GL, `--force-composition`) |
| Live frames on screen | ❌ compositor shows each shared buffer frozen at first import (3 output images → 3 snapshots) |
| Games (DXVK) | ❌ not attempted; PowerVR has no BC texture compression |

## Findings

**Venus reports no DRM nodes over vtest** (`hasPrimary`/`hasRender` false). gamescope required both.
Nested on Wayland it needs neither, as on a driver with no `VK_EXT_physical_device_drm`; patch
`tools/gamescope/patches/0120-nested-backend-without-drm-nodes.patch` limits those checks to the
session-based (DRM) backend.

**No `VK_KHR_external_semaphore_fd` through Venus/vtest.** Only gamescope's explicit sync uses it;
0120 makes it optional and offers explicit sync only when the render node and the extension exist.
Every other device extension gamescope requires is present through Venus.

**The PowerVR driver under-reports dma_buf export for buffers.**
`vkGetPhysicalDeviceExternalBufferProperties(DMA_BUF)` returns no features, so virglrenderer forced
opaque-fd export for host-visible memory, and vtest (which can only hand the guest a dma_buf or shm)
failed every mappable blob (`virgl_renderer_resource_export_blob` -22), hanging gamescope at start.
A probe ([`tools/venus/probes`](#probes)) shows every export, opaque included, is a mmap-able `/dmabuf:` fd.
`tools/venus/patches/0003` passes such fds on as dma_buf.

```
buffer dma_buf  : features [] exportFrom 0x200 compatible 0x200
buffer opaque_fd: features [exportable importable ] exportFrom 0x1 compatible 0x1
type 1 (flags 0x7) export dma_buf  : ret 0 fd -> '/dmabuf:' size 65536 mmap ok
type 1 (flags 0x7) export opaque_fd: ret 0 fd -> '/dmabuf:' size 65536 mmap ok
```

For images the driver reports dma_buf export and import correctly (features 0x6), and both work.

**PowerVR exposes only the LINEAR modifier for RGBA8, without storage.** Format features for
`R8G8B8A8_UNORM` + `DRM_FORMAT_MOD_LINEAR` are `0xcd81` (sampled, colour attachment, blend, blit,
transfer — no `STORAGE_IMAGE`). gamescope composites with compute into its flippable image, so no
modifier qualifies and `CVulkanTexture::BInit` asserts.

**Venus over vtest cannot import dma_bufs.** Mesa sets `has_dma_buf_import = false` for vtest and the
vtest protocol has no import command. gamescope has to import the buffers Xwayland's clients
(Steam's UI through Zink, games through DXVK) render into, so this is required.

**The vtest server dies on client disconnect** in thread render-server mode
(`FORTIFY: pthread_mutex_lock called on a destroyed mutex` in `virgl-1-gpu_ren`).

## Session 2026-10-08 (night)

- **venus 0004**: `vkr_device_destroy` destroyed `dev->object_mutex` before destroying the device's
  remaining objects, which each lock it. bionic aborts on a destroyed mutex, so any client destroying a
  device with live objects lost its renderer. Fixed; server aborts went from 9 per session to 0.
- **gamescope 0122** (diagnostic): the only compute pipelines that fail (`VK_ERROR_UNKNOWN`) are RCAS
  (FSR sharpening) with a YCbCr layer, 3-8 layers. Not on the normal path.
- `vkprobe4.c`: compute storage writes into a LINEAR, dma_buf-exportable RGBA8 image land correctly
  (three successive fills), natively and through Venus.
- The flicker: screenshot bursts show exactly three distinct, unchanging frames - gamescope's three
  output images - each frozen at the content it had when DroidDeck's compositor first imported it.
  Updates to a buffer exported from the Venus side are not visible to the compositor after import.


1. vtest server: one process per client (no `--multi-clients`), so one client's exit cannot take
   down the rest.
2. gamescope: composite into an optimal-tiling image and copy to the linear flippable image when
   the exportable modifiers lack storage.
3. dma_buf import: a vtest import command (server) plus a Mesa Venus build with vtest import — or
   reaching the vendor driver directly instead (option 2 in #165, as ARLinux does with libhybris),
   which has no vtest in the way.
4. BC texture decode for DXVK.

## Probes

`tools/venus/probes/vkprobe.c` (buffers) and `vkprobe2.c` (images) are standalone programs that
ask the device's own Vulkan driver what Venus and gamescope need to know. With a debug build of the
app installed:

```
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang -o vkprobe vkprobe.c -lvulkan
adb push vkprobe /data/local/tmp/
adb shell 'run-as com.droiddeck.launcher sh -c "cp /data/local/tmp/vkprobe files/ && ./files/vkprobe"'
```

Run them as the app: adb's shell domain is not allowed to create the unix sockets the server needs,
so whole-path tests have to run there anyway. Output from other non-Adreno devices is welcome.
