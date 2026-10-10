#!/usr/bin/bash
# Runs INSIDE an Arch Linux ARM container (menci/archlinuxarm:base-devel) on an arm64 runner.
# Builds Arch's own wlroots0.20 package - the wlroots the desktop package's labwc links - with the
# patches in ./patches added, keeps only the library, and packs it as wlroots.tzst
# (usr/local/lib/droiddeck-wlroots/libwlroots-0.20.so) for the apk to stage. The desktop launcher
# puts that directory first on labwc's library path for the GPU renderers only.
set -euxo pipefail
VERSION=${WLROOTS_VERSION:-0.20.2}
WORK=/work
cd "$WORK"
# Same container workarounds as tools/gamescope/build-in-arch.sh.
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed git sudo zstd binutils
id builder >/dev/null 2>&1 || useradd -m builder
echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder
rm -rf pkg && mkdir pkg && cp -r tools/wlroots/patches pkg/ && chown -R builder pkg
sudo -u builder bash -c "
set -euxo pipefail
cd pkg
# gitlab.archlinux.org refuses CI runners now and then (two of four builds on 2026-10-10):
# retry the whole search with a growing pause, and show git's reason.
for attempt in 1 2 3 4 5; do
  for rel in 1 2 3 4; do
    if git clone -q --depth 1 --branch ${VERSION}-\$rel https://gitlab.archlinux.org/archlinux/packaging/packages/wlroots0.20.git arch 2>clone.err; then break 2; fi
    rm -rf arch
  done
  echo \"Arch packaging not cloned (attempt \$attempt of 5): \$(tail -1 clone.err)\"
  [ \$attempt = 5 ] || sleep \$((attempt * 30))
done
test -f arch/PKGBUILD
cd arch
grep -q '^pkgver=${VERSION}$' PKGBUILD
cp ../patches/*.patch .
PATCHES=\$(ls *.patch | sort)
{ echo; echo 'source+=('; for p in \$PATCHES; do echo \"  \$p\"; done; echo ')'; for p in \$PATCHES; do echo \"sha256sums+=('SKIP')\"; done; } >> PKGBUILD
# Arch's PKGBUILD has no prepare(); ours applies the patches to the checkout.
cat >> PKGBUILD <<'PREP'

prepare() {
  cd \"\$srcdir/\$pkgname\"
  for p in \$(ls \"\$srcdir\"/*.patch | sort); do
    echo \"applying \$(basename \"\$p\")\"
    patch -p1 --no-backup-if-mismatch < \"\$p\"
  done
}
PREP
makepkg -A -s --noconfirm --skipchecksums --skippgpcheck
ls -l *.pkg.tar.*
"
cd "$WORK"
rm -rf out && mkdir -p out/usr/local/lib/droiddeck-wlroots
PKG=$(ls pkg/arch/wlroots0.20-*.pkg.tar.* | grep -v -- '-debug-' | head -1)
mkdir -p x && tar --use-compress-program=unzstd -xf "$PKG" -C x
SO=x/usr/lib/libwlroots-0.20.so
test -f "$SO"
cp -L "$SO" out/usr/local/lib/droiddeck-wlroots/libwlroots-0.20.so
strip --strip-unneeded out/usr/local/lib/droiddeck-wlroots/libwlroots-0.20.so || true
# The patch is in: the allocator's symbol and its log line.
grep -q 'Created Vulkan DMA-BUF allocator' out/usr/local/lib/droiddeck-wlroots/libwlroots-0.20.so
readelf -d out/usr/local/lib/droiddeck-wlroots/libwlroots-0.20.so | grep -E 'NEEDED|SONAME'
(cd out && tar --use-compress-program='zstd -19' -cf ../wlroots.tzst usr)
sha256sum wlroots.tzst | tee wlroots.tzst.sha256
ls -l wlroots.tzst out/usr/local/lib/droiddeck-wlroots/
