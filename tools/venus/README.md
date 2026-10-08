# Venus

Vulkan for GPUs Turnip cannot drive (PowerVR, Mali): Mesa's Venus ICD in the rootfs sends Vulkan
over vtest to virglrenderer's server, which runs on Android's own `libvulkan.so`. The app side is
`gpu/Venus.kt`; it is chosen automatically on a non-Adreno GPU, or forced with
`Download/droiddeck-venus` containing `on` or `off`.

`build.sh` regenerates both binaries the apk carries:

| File | Source | License |
|---|---|---|
| `app/src/main/jniLibs/arm64-v8a/libvenusserver.so` | virglrenderer at `VIRGLRENDERER_COMMIT` + `patches/`, NDK, Venus only | MIT |
| `app/src/main/assets/venus/libvulkan_virtio.so` | Arch Linux ARM `vulkan-virtio` (Mesa) | MIT |

Patches:

- `0001` — use the NDK's `android/log.h` and `sys/system_properties.h` in place of AOSP-internal
  `log/log.h` and `cutils/properties.h`.
- `0002` — build the vtest server when only Venus is enabled (upstream ties it to vrend).

Run with `--venus --no-virgl --multi-clients`; the guest sets `VN_DEBUG=vtest` and
`VTEST_SOCKET_NAME`. First verified on a Pixel 10 Pro XL (PowerVR DXT-48-1536): `vulkaninfo` in
the rootfs reports `Virtio-GPU Venus (PowerVR D-Series DXT-48-1536 MC1)`, Vulkan 1.4.
