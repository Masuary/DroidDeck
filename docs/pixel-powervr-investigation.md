# Pixel 10 Pro XL investigation — 2026-10-09

Status: feasibility established for native Vulkan rendering and image sharing;
**DroidDeck/Steam support is not implemented or verified by this investigation.**
The installed app has not been changed.

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
original experiment. The live-frame failure remains unverified and unresolved.
