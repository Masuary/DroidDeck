# gamescope patches carried by the app

Built by `.github/workflows/build-gamescope.yml` on top of the exact gamescope the Linux runtime
ships (3.16.29, Arch Linux ARM's package, same build options), and staged from the apk over
`/usr/local/bin/gamescope` at each session start - the hosted runtime image is never touched.
The binary's shared-library needs are checked against `runtime-sonames.txt`, the runtime's own
library list, before anything is published.

- `0002-steamcompmgr-fallback-appid-focus.patch` - Armada (armada-os/armada), verbatim.
- `0009-fix-arm64-steam-night-mode.patch` - Armada, verbatim: the ARM64 client packs the
  night-mode property differently; the slider did nothing.
- `0019-steamcompmgr-arm64-virtual-white.patch` - Armada, verbatim: the colour-temperature
  slider's (x, y) arrives as one 64-bit element from the ARM64 client; y is recovered from x.
- `0020-color-p3-red-is-wide-gamut.patch` - Armada, verbatim.
- `0100-realtime-queue-and-gamepad-cursor.patch` - this app, two of Armada's ported by hand onto
  3.16.29: realtime-priority Vulkan queues on request (`GAMESCOPE_FORCE_VULKAN_REALTIME=1`)
  without CAP_SYS_NICE, which proot can never have (a no-op on KGSL Turnip, which has a single
  submit-queue priority); and the gamepad-driven cursor sprite following the X pointer that XTest
  moves (it sat frozen). The X pointer is asked for only while a cursor image is drawn - every
  vblank while shown, every 50 ms while hidden for inactivity - since each ask is a blocking round
  trip to Xwayland on the paint thread; with no image, wlserver's position is used as upstream does.
- `0110-wayland-backend-touch.patch` - this app: the nested Wayland backend bound only the host's
  pointer and keyboard, so a finger on the phone's screen never reached Steam. It now binds
  `wl_touch` too and hands each finger to wlserver's touch path (`wlserver_touchdown` / `motion` /
  `up`) - the one a Steam Deck's touchscreen drives - so what a touch does follows the client's
  touch mode (Steam's Big Picture sets Passthrough: a real touch, rows scroll under a finger).
  Finger ids are offset by one, since the nested pointer already moves wlserver's touch 0.
- `0111-wayland-pointer-warps-in-passthrough.patch` - this app: the nested pointer's motion goes to
  wlserver as touch 0, and in Passthrough (Big Picture's touch mode) a motion for a touch that is not
  down moves nothing - so in the app's touchpad mode the Steam client saw no hover and a click landed
  wherever the pointer had last been. The motion now always warps the real pointer as well
  (`bAlwaysWarpCursor`), which the other touch modes did already.
- `0112-restore-iconified-game-on-resume.patch` - this app: the Steam menu is an overlay that
  takes input without changing the focus window, and a fullscreen wine game minimizes itself when
  it loses input. gamescope only takes a window out of iconic when the focus window changes, and
  wine will not activate a window it believes iconic, so after Resume the game stayed minimized: a
  black screen with its small caption in the top-left corner (Titanfall 2, GE-Proton 11). The
  iconify request is remembered and the window goes back to NormalState before input returns to it,
  then focus is handed over again. `GAMESCOPE_RESTORE_FOCUS_WINDOW` on the root window asks for the
  same restore from outside (the session script's resume watcher).

- `0113-steam-overlay-keeps-game-keyboard-focus.patch` - this app: the Steam client opens the Quick
  Access Menu and Steam menu over a game with `STEAM_INPUT_FOCUS` 1, which moves X keyboard focus to
  its overlay; wine then deactivates the game and a fullscreen game minimizes itself, freezing
  behind the menu instead of running on as on a Steam Deck. Steam's own overlay taking input now
  keeps keyboard focus on the game, as mode 2 does; its input comes from the controller through
  Steam Input, not the X keyboard. The pointer warps gamescope makes as input moves to Steam and
  back are skipped around it, since the game - still taking input - saw them as a mouse jump.

Sixteen more of Armada's patches are DRM/lease/HDR-on-KMS work for a native display, which this
app's Wayland-hosted gamescope never reaches, or need a newer gamescope than the runtime has.
- `0120-nested-backend-without-drm-nodes.patch` - this fork: gamescope required a DRM primary node
  (unless presenting through a Vulkan swapchain) and a render node. Venus over vtest - Android's
  PowerVR driver behind it - reports neither, so the nested Wayland backend died with `physical
  device has no primary node`, then `no render node`. Only the DRM backend scans out through the
  primary node, and a nested gamescope runs without a render node exactly as it does on a driver
  with no VK_EXT_physical_device_drm; both checks now apply to session-based backends alone.
  Explicit sync, which imports client syncobjs through the render node and shares their semaphores
  as fds, is not offered without one or without VK_KHR_external_semaphore_fd (which Venus over vtest
  also lacks); that extension is the only thing that needed it, so it is now optional.
- `0121-shared-images-without-mutable-format.patch` - this fork: gamescope makes every flippable
  (exported) image mutable with an [UNORM, sRGB] view list. Android's PowerVR D-Series driver only
  offers the LINEAR modifier for shared images and refuses MUTABLE_FORMAT on it with or without a
  view list (`VK_ERROR_FORMAT_NOT_SUPPORTED`; tools/venus/probes/vkprobe3.c), so no modifier
  qualified and `CVulkanTexture::BInit` asserted `modifiers.size() > 0`. When no exportable
  modifier allows a mutable image, the image is made without MUTABLE_FORMAT and its sRGB-format
  ("linear") view uses the raw format instead. Composition writes through the raw view anyway; what
  is sampled from these images (cursor, blank texture) loses hardware sRGB decode.
- `0122-log-failing-pipeline-variant.patch` - this fork, diagnostic: when vkCreateComputePipelines
  fails, log the shader type and specialization values, so a driver that refuses only some
  composite variants (PowerVR through Venus: VK_ERROR_UNKNOWN) shows which.
- `0123-venus-copy-optimal-output-to-linear.patch` - this fork: with `BL_VENUS=1` on the nested
  modifier backend, compose into a private optimal RGBA image (PowerVR has no STORAGE_IMAGE on the
  LINEAR shared images, nor on optimal BGRA8) and copy, or blit to convert BGRA, the finished frame
  into the LINEAR output image the host compositor imports.
- `0124-venus-keep-composition-and-wait-for-output-release.patch` - this fork, `BL_VENUS=1` only:
  (1) the Wayland backend composites every frame. `--force-composition` only sets `composite_force`,
  which Steam can reset through the `GAMESCOPE_COMPOSITE_FORCE` root property; gamescope then hands
  the host Steam's own textures over a black backing plane instead, and those never got 0123's
  output copy. The first time that happens is logged. (2) The three output images rotate without
  regard to `wl_buffer.release`; before composing into one the host compositor still holds, wait up
  to 100 ms (dispatching Wayland events) for its release, so a host presenting more slowly than
  gamescope composites never reads a frame being overwritten. Waits and timeouts are logged.
- `0125-venus-clean-cpu-writes-to-mapped-memory.patch` - this fork, `BL_VENUS=1` on aarch64 only:
  host-visible memory under Venus is the vendor driver's dma-buf mapped into gamescope, and the
  GPU does not snoop the CPU caches for it, while nothing on the vtest path does dma-buf cache
  maintenance. Every CPU write gamescope makes into mapped memory before a GPU read (the per-frame
  staging copy of each wl_shm window, which is how Xwayland hands over Steam's UI under Venus, plus
  constants, LUTs and texture uploads) is now cleaned to the point of coherency (`dc civac` per
  line, `dsb sy`). Without it the GPU copied stale pages: older frames, the startup movie's last
  frame, and left-overs from earlier sessions, i.e. the Pixel flicker.
- `0126-venus-present-frames-as-wl-shm.patch` - this fork, `BL_VENUS=1` (opt out with
  `BL_VENUS_SHM=0`): the host compositor's PowerVR import of gamescope's shared output dma-bufs
  returned stale contents (each of the three buffers held an old picture for seconds, while
  Steam's X windows were live), and Android refuses to let the compositor mmap them. Each composite
  is now also copied, on gamescope's own device, into a host-visible buffer (`CmdCopyImageToBuffer`
  plus a host-read barrier), invalidated after the submission's wait, and sent to the compositor as
  one of three `wl_shm` XRGB8888 buffers with release tracking. The compositor copies a wl_shm frame
  at commit, so no cross-device GPU sharing is involved.
- `0127-venus-check-readback-frame-marker.patch` - this fork, `BL_VENUS=1` with 0126's wl_shm output:
  experimental12 still showed pictures well behind Steam's X windows (the startup logo for 16 s
  after the sign-in screen was up), plus frames missing the dialog or torn at a horizontal line,
  which a copy read part-way through would give. After each composite's readback copy the GPU
  now writes the frame's number into a marker after the image (`CmdFillBuffer`, ordered after the
  copy by a transfer barrier). Once the submission's wait returns, the host compares it with the
  number it expects. If the wait came back before the GPU finished, gamescope polls the marker
  for up to 100 ms and drops the cache lines it read too early before sending the frame. Every
  5 s with a late frame (and for the first three windows regardless) it logs `Venus readback: N of
  M frames read before the GPU finished them (worst K frames behind ...)`.
  `BL_VENUS_READBACK_WAIT=0` only measures.
- `0128-venus-confirm-waits-with-sequence-marker.patch` - this fork, `BL_VENUS=1` (opt out with
  `BL_VENUS_SEQ_MARKER=0`): 0127 showed that waits on gamescope's timeline semaphore return before
  the GPU has finished the submission under Venus over vtest (85-96% of frames). gamescope relies on
  those waits to reset command buffers, drop texture references and reuse its upload buffer, so
  every wait is now confirmed: each submission ends by writing its sequence number into its own
  slot of a 64-slot host-visible ring (after an all-commands barrier), and `wait()`, the descriptor
  set wait, `completedSeqNo()` and `garbageCollect()` count a submission done only once it and every
  earlier one have written theirs (polling up to 1 s). Every 5 s with a late wait (and for the first
  three windows regardless) it logs `Venus: N of M waits returned before the GPU finished`.
- `0129-venus-read-back-private-composite.patch` - this fork, `BL_VENUS=1` with 0126's wl_shm output:
  with every wait confirmed (0128) and 0 late readbacks, the screen still cycled through three
  fixed pictures in a strict 1-2-3 order, one per frame: gamescope's three LINEAR shared output
  images each kept an old picture (taken at different moments) when read back on gamescope's own
  Venus device. The readback now copies the private optimal composite image (0123) instead, which
  is written every frame and never exported, and the CPU swaps red and blue while filling the
  XRGB8888 wl_shm buffer. `session.log` says `Venus readback: from the private composite image`.
  `BL_VENUS_READBACK_FROM_SHARED=1` reads the shared image again, for an A/B comparison.
