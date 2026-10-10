# Pixel 10 Pro XL investigation — 2026-10-09

Status: the experimental Pixel APK reaches Steam's login screen on this phone.
The installed experimental2 build still flickers. The latest screenshots show
old Steam frames and black regions while Android controls remain intact. Stable
presentation and game support are **not yet verified**.

## Source and device

- Upstream checkout: `Droid-Deck/DroidDeck`, `c101a451e3824ddebdc62d2d2f2d743aa96c3a81`.
- Experimental base: `gogetwoo/DroidDeck`, branch `pixel-venus`,
  `410fe031b97d86c5f05015fd41d37df6649d43a6`.
- Local work branch: `pixel-powervr` in `~/DroidDeck-pixel`.
- Vulkan-Headers used to compile the native probes:
  `c46850864f4661461b0f6cb9922c058ffea4915e`.
- Actual test device: Google Pixel 10 Pro XL, Tensor G5, Android 17,
  `PowerVR D-Series DXT-48-1536 MC1` as returned by Vulkan.
- Original app logs: `Download/DroidDeck/2026-10-09-03-steam/`.

## Existing port

The [experimental branch's report](https://github.com/gogetwoo/DroidDeck/blob/410fe031b97d86c5f05015fd41d37df6649d43a6/docs/pixel-venus.md)
describes a Venus/vtest bridge from the Linux guest to Android's vendor Vulkan
driver. Its author reports reaching Steam's sign-in screen, with frozen output
buffers. It also documents missing DMA-BUF import in Venus/vtest and a software
rendering fallback for Steam's UI. These are the author's findings, not results
of a custom APK run in this session.

Related discussion: [upstream issue #165](https://github.com/Droid-Deck/DroidDeck/issues/165).

## Tests run directly on this phone

All tests below ran as Termux native Android processes, outside DroidDeck's Linux
guest. They do not exercise Venus, Wayland, Steam, or the installed app.

| Probe | Observed result |
| --- | --- |
| Existing `vkprobe2.c` | LINEAR RGBA8 DMA-BUF image allocation, export, import and binding succeed. |
| Existing `vkprobe3.c` | RGBA8/BGRA8 plain modifier images are accepted; mutable-format variants return `VK_ERROR_FORMAT_NOT_SUPPORTED` (-11). |
| Existing `vkprobe4.c` | Both optimal and exportable LINEAR images pass three compute fills; all 65,536 pixels match on every frame. |
| New `vkprobe-sharing.c` | Two independent Vulkan instances share one DMA-BUF; all 4,096 pixels match on each of six alternating red/green/blue frames. |
| New probe with `--legacy` | Also passes all six frames. It deliberately models invalid/undefined ownership handling and is not a conformance result. |

The new probe compiles with `-std=c11 -Wall -Wextra -Werror`. The existing
`vkprobe4.c` compiles with one missing-field-initializer warning.

The normal sharing test enables `VK_EXT_queue_family_foreign`, acquires imported
images from GENERAL, and releases them back to GENERAL after reading. The legacy
comparison omits that extension on the consumer, acquires from UNDEFINED and
omits the consumer release. The current compositor uses those patterns, but
**both probe modes pass here, so this does not establish the cause of the frozen
Steam display**. A smaller reproduction involving Venus is still needed.

## Reproduce the sharing test in Termux

Run from this checkout with the Vulkan-Headers checkout at `../vulkan-headers`:

```sh
mkdir -p .local-tests
clang -std=c11 -Wall -Wextra -Werror -I../vulkan-headers/include \
  tools/venus/probes/vkprobe-sharing.c -o .local-tests/vkprobe-sharing \
  -L/system/lib64 -lvulkan
timeout 20 .local-tests/vkprobe-sharing
timeout 20 .local-tests/vkprobe-sharing --legacy
```

The test returns 0 when every pixel matches, 1 on mismatched frames, and 2 on
setup/Vulkan errors. A fence wait times out after five seconds. Producer and
consumer execute sequentially with host fence waits: this intentionally isolates
memory visibility from presentation and cross-process semaphore synchronization.

## Next implementation milestone

1. Build the existing experimental APK and reproduce its Steam display failure
   on this phone, recording the exact APK, runtime, Venus and gamescope versions.
2. Extend the sharing reproduction so the producer runs through Venus/vtest and
   the consumer uses Android's native Vulkan driver. Test repeated export/import
   with correct ownership and synchronization before proposing a compositor fix.
3. Once live Steam frames work, validate accelerated UI and game buffer import.
   Game texture-format compatibility and performance remain separate work.

The repository's complete APK build requires Android SDK/NDK, Java and Linux
cross-compilation tooling. Java, SDK/NDK, Docker and ADB are not available in this
Termux workspace. The user has now created `Masuary/DroidDeck`, including `pixel-venus`.
The `pixel-powervr` branch builds an isolated debug package, `com.droiddeck.launcher.pixel`,
with the display name **DroidDeck Pixel** through `build-pixel.yml`. The workflow
builds the patched gamescope and current DRM preload, stages unchanged dependencies
from the checksum-verified upstream 0.3.1 APK, then builds the APK and runs unit tests.
Venus sessions use forced composition and software client GL as documented by the
original experiment.

## Installed APK and flicker investigation

Build `c4e47bb92b3a3623dc3a44d49b72cb1adeec1d77` passed all 254 unit tests and
APK packaging/signature checks. The downloaded APK's SHA-256 is
`1e5d9630e98b739ad8259f0bec07d22f015b2f68ba512833ba22ef928aeb7285`.
The user installed it and reported reaching Steam's login screen with flicker.

Session `2026-10-09-07-steam` confirms `com.droiddeck.launcher.pixel`, version
`0.3.1-pixel-experimental`, runtime r9, Venus enabled, and the system PowerVR
driver rendering the compositor. `frame.first` and `session.ready` occur at
21:18:48 local time. The compositor continues receiving and presenting frames;
there is no repeated guest exit or window recreation during that interval.

The `pixel-experimental2` candidate fixes three Vulkan ownership errors:

- Enable `VK_EXT_queue_family_foreign`, which the compositor already used.
- Acquire imported client images from GENERAL instead of UNDEFINED, preserving
  their contents.
- Release each distinct imported source back to the foreign queue in GENERAL
  after reading it, before the submission fence and Wayland buffer release.
  Apply this to normal/HDR composition and layer copies; preserve cursor input
  on its existing acquire/release path as well.

These corrections follow the [Vulkan synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
They are necessary correctness fixes, **not proof that the observed flicker is
fixed**. A scaled native image-sharing probe (1280x720 BGRA DMA-BUF to 1920x1080
RGBA via linear blit) also passes all six frames in both corrected and legacy
modes on this phone. That test does not run through Venus or Android's display.
The updated compositor passes the native C syntax check. Install the candidate
over DroidDeck Pixel, stop the old session and start Steam again, and verify both
the version and the new `dma-buf handoff` line in `wayland.log` before assessing
whether login redraws and input are stable.


## Experimental2 installed test and experimental3 candidate — 2026-10-10

Session `2026-10-10-01-steam` identifies version `0.3.1-pixel-experimental2`
(package `com.droiddeck.launcher.pixel`, runtime r9). The new DMA-BUF handoff
message appears in `wayland.log`, so this was the intended compositor build.
Seven screenshots taken between 03:18:59 and 03:19:04 show complete Steam login
frames alternating with partially black frames. An older 03:17 Steam clock
reappears among frames showing 03:18. The on-screen controls stay intact. The
captures support stale or incomplete frame contents; they do not show Android
switching repeatedly between activities.

The compositor logs one Steam window and continues presenting roughly 7–14
frames per second. There is no GPU device-loss error or crash during the capture
interval. Steam later exits at the app's request, with status 0. Steam's GPU
report confirms its UI uses llvmpipe. **The experimental2 ownership corrections
did not fix the visible issue.** Raw screenshots and account/session logs are
kept on the phone and are not included in the repository.

A further native diagnostic, `tools/venus/probes/vkprobe-sharing-compute.c`,
uses two Vulkan instances in one process with a 1280x720 shared image. Six
alternating RGB fills pass (921,600 pixels per frame) both with `--compute`
(direct LINEAR storage writes) and `--copy` (OPTIMAL compute, then a transfer copy
to a LINEAR image without storage usage). A temporary variant also passes with
a mutable-format optimal image. As before, these are native tests, **not a
reproduction through Venus**. Direct LINEAR storage is diagnostic only because
the driver does not advertise the required modifier feature.

Experimental3 tests gamescope patch `0123-venus-copy-optimal-output-to-linear`:
when `BL_VENUS=1` on the nested modifier backend, compose into a private optimal
RGBA image, then copy the full frame into the selected LINEAR shared output image.
For BGRA output, use a nearest-filter blit to convert channel order. PowerVR also
omits storage support for optimal BGRA8 (while accepting image creation), so
keeping BGRA for the private target would still use an unsupported combination.
The native RGBA-to-BGRA sharing test passes all 921,600 pixels on six frames;
RGBA optimal storage support is advertised, and its mutable raw/sRGB views work.
The shared outputs have transfer-destination usage instead of storage usage.
The copy stays in the composition submission; existing barriers release it to
FOREIGN in GENERAL and Wayland waits for completion before committing it. The
private target is recreated with output size/format changes. Other backends and
explicit output overrides retain their existing path.

The next installed test must show `0.3.1-pixel-experimental3` in `device.txt` and
`Venus output: optimal RGBA compute -> LINEAR transfer` in `session.log`.
Then check animation, the clock, and input for stale frames or black rectangles.
This candidate is **not yet verified to solve the flicker**, and accelerated
Steam UI/game support remains unfinished.

Build the full-size native diagnostic from the repository root:

```sh
clang -std=c11 -Wall -Wextra -Werror -I../vulkan-headers/include \
  tools/venus/probes/vkprobe-sharing-compute.c \
  -o .local-tests/vkprobe-sharing-compute -L/system/lib64 -lvulkan
timeout 25 .local-tests/vkprobe-sharing-compute --compute
timeout 25 .local-tests/vkprobe-sharing-compute --copy
timeout 25 .local-tests/vkprobe-sharing-compute --copy --bgra-output
```

## Experimental3 still flickers; experimental4 candidate — 2026-10-10

The user reports that experimental3 (build `5545281`, the optimal-RGBA-then-copy output of
gamescope patch 0123) still flickers. Two gamescope behaviours remain outside everything tested so
far, and neither shows on Adreno, where the host compositor keeps up with gamescope:

- **Forced composition is not sticky.** `--force-composition` only sets gamescope's
  `composite_force` convar. Steam can rewrite it through the `GAMESCOPE_COMPOSITE_FORCE` root
  property (`steamcompmgr.cpp`, property handler), and the nested Wayland backend then stops
  compositing: it presents a black single-pixel backing buffer plus Steam's own textures as
  subsurfaces. Those textures never pass through 0123's copy, so frames switching between the two
  paths would explain complete login frames alternating with partially black ones.
- **Output images ignore `wl_buffer.release`.** `vulkan_composite` cycles `nOutImage` through three
  images unconditionally. The PowerVR session presents only about 7–14 frames per second, so the
  compositor can still be reading the image gamescope writes next.

Experimental4 adds gamescope patch `0124-venus-keep-composition-and-wait-for-output-release`
(Venus sessions only): the Wayland backend composites every frame whatever `composite_force` says,
and waits up to 100 ms for the host's release of the next output image before writing it.

Check in the next installed test (`0.3.1-pixel-experimental4` in `device.txt`). In gamescope's output
in `session.log`:

- `Venus: composite_force was cleared` means Steam did turn composition off, so the first cause was
  real.
- `Venus: next output image still held by the host compositor` shows how often the second race
  happens, and whether a release ever times out.
- No such lines plus continuing flicker rules out both causes. In that case the remaining
  candidate is the visibility of Venus-side writes in the compositor's import of the same dma-buf
  (the "frozen at first import" finding above), which needs a Venus-producer probe.

This candidate is **not yet verified on the phone**.

## Experimental4 installed test and experimental5 probe — 2026-10-10

Session `2026-10-10-03-steam` (experimental4) ran gamescope with patch 0124:

- `Venus: composite_force was cleared` never appears, so Steam did not switch composition off.
- `Venus: next output image still held by the host compositor` appears once (3.8 ms, released).
  The output-image reuse race is real but rare, and not the cause.
- The compositor presented about 11–14 frames per second with no device loss.

The user still sees flicker. In ten screenshots at 12:21, six are the live Steam sign-in screen,
three show the DroidDeck launch logo on black, and one shows the sign-in screen with an
**11:26** Steam clock, nearly an hour before this session started (12:20). Neither of those two
pictures can be in anything gamescope drew in this session. The flicker therefore shows memory
that was never written in this session, which rules out every timing explanation (late,
partial or overwritten frames). Two places can hold such memory:

1. **In:** the dma-buf the compositor imports is not (or not visibly) the memory gamescope's copy
   writes through Venus, so the compositor copies left-over pages to the screen.
2. **Out:** the compositor's screen swapchain on PowerVR, the system driver's Android WSI, which no
   Adreno session uses, presents gralloc buffers whose contents are left over from earlier use.

Experimental5 adds a compositor frame probe, on GPUs other than Adreno only. Once a second, for up
to 90 lines, it reads back the dma-buf a client has just committed. It logs `probe` lines in
`wayland.log` with the buffer's dma-buf inode, a content hash, its black share, and whether that
buffer changed since it was last probed. How to read them:

- Every gamescope buffer (normally three inodes) shows the sign-in screen: about the same black
  share, and hashes that change when the screen changes. Then the input is good and the fault is
  on the way out (case 2).
- Some inode stays mostly black, or keeps a hash that none of the others ever have. Then that
  buffer is stale on the way in (case 1), at the Venus/vtest export.

A static screen keeps its hash, so `UNCHANGED` alone is not proof. Compare the buffers with one
another.

A test needing no new build: the drawer's **Zero-copy presentation** switch moves the frame onto
an Android display layer (an AHardwareBuffer the compositor fills and hands to SurfaceFlinger),
bypassing the screen swapchain. If the flicker stops with it on, case 2 is confirmed.

## Experimental5 probe result and experimental6 candidate — 2026-10-10

Session `2026-10-10-05-steam` (`0.3.1-pixel-experimental5`, zero-copy off). The screen holds the
DroidDeck logo on black while nothing changes, and flickers whenever input makes Steam redraw.
The compositor's probe (app.log, `[probe]`) read back what gamescope committed:

| Hash | Black | Content | Buffers (dma-buf inode) |
| --- | --- | --- | --- |
| `6c4a016d` | 100% | empty frame (startup) | 1217952, 1217963, 1217964, 1217965 |
| `7eeed1b2`, `3d8958e9` | 95% | logo on black | 1217963, 1217965 |
| `a0191819` | 0% | Steam sign-in | 1217963 |

The same gamescope buffer (1217963) carries the sign-in screen at one probe and the logo at the
next. The logo is not left-over memory: it is the last frame of DroidDeck's own Steam startup
movie (`SessionFiles.kt` stages `steam-startup/droiddeck-startup.webm` as Steam's
`bigpicture_startup.webm`). Gamescope composites what Steam's window gives it, so **the
alternation is already in steamwebhelper's output**, before gamescope, Venus frame sharing or the
compositor. Every earlier gamescope and compositor change was downstream of it.

Steam is started with `-cef-force-gpu -cef-use-gl=angle -cef-use-angle=vulkan`. Under Venus,
ANGLE's Vulkan is Venus with `MESA_VK_WSI_DEBUG=sw`; `LIBGL_ALWAYS_SOFTWARE` does not reach it.
That path, Chromium's GPU compositor over Venus and the shared-memory WSI, is the remaining
untested link. It presents frames that alternate between old and new content.

Experimental6 starts Steam with `-cef-disable-gpu -cef-disable-gpu-compositing` when
`BL_VENUS=1`, so Steam's UI is rasterised and composited on the CPU. `session.log` says
`experimental Venus: Steam's UI drawn on the CPU`. `BL_VENUS_CEF_GPU=1` in `Download/droiddeck-env`
restores the ANGLE/Vulkan path for an A/B comparison. Expect a slower UI; check whether the
logo/sign-in alternation is gone. The frame probe stays in for this test. **Not yet verified on
the phone.**

## Experimental6 installed test and experimental7 candidate — 2026-10-10

Session `2026-10-10-07-steam` (`0.3.1-pixel-experimental6`, Steam's UI on the CPU):

- **The flicker is gone.** After the movie (hash `f949d637`, logo on black), every probe of
  every gamescope buffer is that same frame, `UNCHANGED` for over 30 s.
- The screen stays on the logo, though. Steam's UI is alive underneath: `webhelper_js.txt` logs
  the sign-in state (`OnLoginStateChange 1 1 0 0`), `giving focus to keyboard` on the user's
  presses at 14:42:20–27, and a sign-in poll at 14:42:44. Only the startup movie's last frame
  reaches the window.

The startup movie's layer is therefore what never leaves the Big Picture window under Venus. On
the GPU path its last frame alternated with the UI; on the CPU it covers the UI. Experimental7
keeps the CPU UI and sets DroidDeck's startup movie aside for Venus sessions: the session script
renames `uioverrides/movies/{bigpicture,steam_os}_startup.webm` before Steam starts, and the app
stages them again at the next launch. `BL_VENUS_STARTUP_MOVIE=1` keeps the movie. Look for
`experimental Venus: startup movie off` in `session.log`, then for a sign-in screen that stays up.
If Steam falls back to a built-in movie and gets stuck the same way, the problem is video playback
in steamwebhelper itself. **Not yet verified on the phone.**

## Experimental7 installed test, a corrected diagnosis, and experimental8 — 2026-10-10

Session `2026-10-10-09-steam` (`0.3.1-pixel-experimental7`, CPU UI, no startup movie): every
probe is `6c4a016d`, 100% black, while `webhelper_js.txt` shows the sign-in page live and focused.

Steam's GPU report (`webhelper_gpu.txt`) corrects the experimental6 reasoning:

| Sessions | Steam's UI renderer |
| --- | --- |
| up to 14:21 (all flickering builds) | `ANGLE (Mesa, llvmpipe ... OpenGL 4.6)`, i.e. CPU GL, **not** Venus |
| 14:41 and 14:55 (experimental6/7, `-cef-disable-gpu`) | `ANGLE (... Vulkan 1.4.317 (Virtio-GPU Venus ...))` |

So the flickering UI was never rendered through Venus. Under `LIBGL_ALWAYS_SOFTWARE` it is drawn
on the CPU and reaches gamescope as Xwayland **wl_shm** buffers. Experimental6/7 pushed Steam's GPU
process onto Venus instead, and their black and logo-only windows came from that change. Both are
reverted in experimental8.

What changes the picture between Steam and gamescope's output is gamescope's shm import
(`vulkan_create_texture_from_wlr_buffer`). For every commit it allocates a host-visible staging
buffer, `memcpy`s the window into the mapping, and has the GPU copy it into a texture. Under Venus
that mapping is PowerVR's dma-buf mapped into the guest, the GPU does not snoop the CPU caches,
and nothing on the vtest path does dma-buf cache maintenance (`DMA_BUF_IOCTL_SYNC`). The GPU can
then copy whatever the recycled pages last held in memory. That explains every symptom: stale
whole frames, the startup movie's last frame long after it ended, Steam frames from an earlier
session, and new frames only "sometimes".

Experimental8 adds gamescope patch 0125. With `BL_VENUS=1` on aarch64 it cleans each CPU-written
range to the point of coherency (`dc civac`, `dsb sy`; Linux allows this from EL0) after the shm
staging copy, the per-frame constants, LUT uploads and texture uploads. `session.log` shows
`Venus: cleaning CPU writes to mapped memory to the point of coherency` once. The frame probe
stays in: a fixed build shows the sign-in screen (low black share) on every probe, and the startup
movie plays and then gives way to it. **Not yet verified on the phone.** The same missing cache
maintenance would affect any other CPU-written host-visible memory under Venus (games' uniform
buffers, for example). If 0125 confirms the cause, the general fix belongs in the vtest/Venus
layer.

## Experimental8 installed test and experimental9 X-level probe — 2026-10-10

Session `2026-10-10-11-steam` (`0.3.1-pixel-experimental8`): gamescope logs `Venus: cleaning CPU
writes to mapped memory to the point of coherency (64-byte lines)`, so patch 0125 was active. The
user's 68 s screen recording (scene luminance sampled at 10 Hz) shows:

- 0–46 s: the startup movie's last frame (logo), steady.
- From the first input, 46–50 s: logo and sign-in alternate every 0.1–0.3 s. Some sign-in frames
  are incomplete, with black blocks stepping in from the bottom-right.
- 50–58 s: the sign-in screen and the blurred Steam-menu view alternate.

The cache cleaning did not change the symptom. The stale pictures are not explained by CPU→GPU
coherency in gamescope's upload. Every probe so far sat at or after gamescope; none has looked at
what Steam itself puts into its X window.

Experimental9 adds an X-level probe to the session script (Venus only, `BL_XPROBE=0` turns it
off). Once Steam's window is mapped, it reads it from Xwayland with `XGetImage` ten times a second
for 90 s and writes `== xprobe` lines to `session.log`: each new frame with its id and brightness,
then one line per second of frame ids, e.g. `t=47s frames 3 1 3 1 3 3 1 ...`. If those ids
already alternate, the fault is in how Steam's renderer (ANGLE on llvmpipe GL) presents to X. If
they are steady while the screen flickers, it is between Xwayland and gamescope's output.

Experimental9 also adds `BL_STEAM_CEF_ARGS` (one line in `Download/droiddeck-env`), which replaces
Steam's renderer flags (`-cef-force-gpu -cef-ozone-platform=x11 -cef-use-gl=angle
-cef-use-angle=vulkan` by default), so renderer experiments need no build. Patch 0125 stays in.

## Experimental9 X-probe result and experimental10 — 2026-10-10

Session `2026-10-10-13-steam` (`0.3.1-pixel-experimental9`). Steam's Big Picture output window is
`0x2200034` (`CCompositorGLThread::CreateOutputWindow`, an OpenGL window composited by Steam on
llvmpipe). Read from Xwayland, from 12 s until the probe ended at 76 s, every sample is a new
frame, all dark (mean 8–9, the startup-movie logo). **The sign-in screen never appears in that
window**, through the user's input from about 15:45:28. Steam keeps compositing the movie layer
into its output window, and the UI's sign-in page never takes over. The sign-in frames that do
reach the screen therefore come from another X window, i.e. gamescope alternates between two
windows' contents.

Other evidence: `AcquirePixmap: failed to create glx pixmap for window: 0x2400016` (Composite
`BadMatch`, GLX `BadDrawable`) occurs ten times, only for the 1x1 notification popup at its
creation. It is not the main UI.

Experimental10 extends the X probe: once a second for the first 60 s it lists every visible window
(at least 300x200) with its id, geometry, `WM_NAME`, content hash and brightness. That shows which
window holds the sign-in picture, and the next step is why gamescope shows both.

## Experimental10 result: Steam's windows are live, gamescope's shared buffers read stale — experimental11

Session `2026-10-10-15-steam` (`0.3.1-pixel-experimental10`).

**Xwayland** (`== xprobe`): from t=20 s Steam's three 1280x720 windows (`0x2200034`, `0x2400005`,
`0x2400006`) show the sign-in screen (brightness 40–42) with a new hash on every 0.1 s sample. No
dark or stale frame appears.

**Compositor** (`[probe]`, gamescope's three output dma-bufs as the compositor's own GPU import
reads them): each buffer holds one picture for many seconds and goes back to older ones. For
example, buffer 1226766 shows the logo (`7eeed1b2`) for 11 s, then sign-in frames, then from
16:01:41 to 16:02:04 the **all-black first frame** (`6c4a016d`), while 1226767 and 1226768 each
hold a different fixed sign-in frame. Rotating through the three is the flicker.

So gamescope composites live content (patch 0123 copies it into the LINEAR shared images), but
the compositor's PowerVR import of those dma-bufs returns old contents. That is the original
"frozen at first import" finding, now isolated: producer through Venus (the vtest server's
PowerVR device), consumer the compositor's PowerVR device in the app process, on the same
dma-buf. The exact layer that holds the stale data (GPU caches on either side, or the import) is
still open.

Experimental11 changes the compositor on GPUs other than Adreno: client dma-bufs are not imported
for the GPU. Each is `mmap`ed, synced for CPU reads with `DMA_BUF_IOCTL_SYNC` (the exporter's own
cache maintenance), and copied into a host-memory image at every commit, the way `wl_shm` frames
are. `wayland.log` says `CPU import on`. The frame probe now prints both views of the same buffer:
`CPU copy <hash>` and `GPU import <hash>`, from a separate import kept only for the probe. If the
CPU copy follows the screen and the GPU import lags, the fault is in the GPU import path. If both
lag, the producer's writes are not reaching memory. This costs one 3.7 MB copy per frame at
about 15 fps.

## Experimental11 result and experimental12 — 2026-10-10

Session `2026-10-10-17-steam` (`0.3.1-pixel-experimental11`): still flickers. The CPU import never
ran: `CPU import: mmap of a 1280x720 dma-buf failed (Permission denied); importing it for the GPU`.
Android does not let the app map gamescope's output dma-bufs, so the compositor fell back to its
GPU import, and the probe again shows each of the three buffers frozen on an old picture.
Experimental12 reverts that compositor change.

Experimental12 instead stops sharing GPU memory between the two devices. Gamescope patch 0126
(Venus, default on, `BL_VENUS_SHM=0` to disable) copies every composite into a host-visible
buffer on gamescope's own (Venus) device and sends it to the compositor as a `wl_shm` frame, which
the compositor copies at commit. It costs a 3.7 MB readback and copy per frame at 1280x720.
`session.log` shows `Venus output: frames go to the host compositor as wl_shm` and `Venus:
presenting 1280x720 frames as wl_shm`. If the screen is stable with it, the stale data lived in the
cross-device dma-buf import. If it still flickers, the stale data is already in gamescope's own
composite (its inputs), and the next probe belongs there.
