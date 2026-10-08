#!/usr/bin/env bash
# Builds the two halves of Venus (see app/src/main/java/com/droiddeck/launcher/gpu/Venus.kt):
#   jniLibs/arm64-v8a/libvenusserver.so - virglrenderer's vtest server, Venus only, for Android
#     (bionic, NDK), dlopening the system libvulkan.so
#   assets/venus/libvulkan_virtio.so - Mesa's Venus ICD for the rootfs, from Arch Linux ARM's
#     vulkan-virtio (glibc aarch64), its version matched to the runtime's Mesa
# Needs NDK (or ANDROID_HOME with ndk/<version>), meson, ninja, git, curl.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/release.env"

ndk=${NDK:-${ANDROID_HOME:?set NDK or ANDROID_HOME}/ndk/${DROIDDECK_NDK_VERSION:-27.3.13750724}}
tc=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
api=28
work=${VENUS_WORK:-${TMPDIR:-/tmp}/droiddeck-venus}
mkdir -p "$work"

# Server
src=$work/virglrenderer
if [[ ! -d $src/.git ]]; then
    git clone https://gitlab.freedesktop.org/virgl/virglrenderer.git "$src"
fi
git -C "$src" fetch -q origin "$VIRGLRENDERER_COMMIT" 2>/dev/null || true
git -C "$src" checkout -q -f "$VIRGLRENDERER_COMMIT"
for p in "$here"/patches/*.patch; do git -C "$src" apply "$p"; done
cat > "$work/android-arm64.cross" <<CROSS
[binaries]
c = '$tc/aarch64-linux-android$api-clang'
cpp = '$tc/aarch64-linux-android$api-clang++'
ar = '$tc/llvm-ar'
strip = '$tc/llvm-strip'
pkg-config = 'false'

[built-in options]
c_link_args = ['-llog']
cpp_link_args = ['-llog']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
CROSS
rm -rf "$work/build"
meson setup "$work/build" "$src" --cross-file "$work/android-arm64.cross" \
    --buildtype=release --default-library=static \
    -Dvrend=false -Dvenus=true -Dvulkan-dload=true -Dplatforms=[] \
    -Drender-server-mode=thread -Dtests=false -Dvideo=false -Dtracing=none
ninja -C "$work/build" vtest/virgl_test_server
"$tc/llvm-strip" -o "$repo/app/src/main/jniLibs/arm64-v8a/libvenusserver.so" "$work/build/vtest/virgl_test_server"

# Guest ICD
pkg=$work/vulkan-virtio.pkg.tar.xz
curl -fsSLo "$pkg" "$VULKAN_VIRTIO_URL"
echo "$VULKAN_VIRTIO_SHA256  $pkg" | sha256sum -c -
rm -rf "$work/pkg" && mkdir -p "$work/pkg"
tar -xJf "$pkg" -C "$work/pkg" usr/lib/libvulkan_virtio.so
install -Dm644 "$work/pkg/usr/lib/libvulkan_virtio.so" "$repo/app/src/main/assets/venus/libvulkan_virtio.so"

ls -l "$repo/app/src/main/jniLibs/arm64-v8a/libvenusserver.so" "$repo/app/src/main/assets/venus/libvulkan_virtio.so"
