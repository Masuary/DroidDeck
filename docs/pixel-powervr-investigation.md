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
