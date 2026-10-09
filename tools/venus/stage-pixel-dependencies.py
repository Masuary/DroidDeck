"""Stage unchanged runtime dependencies from the checksum-pinned upstream 0.3.1 APK.

The Pixel branch's proot and x86 preload sources match that release. The workflow
rebuilds libblsession (including the Pixel DRM changes), gamescope and audio sinks.
Gradle replaces the guest scripts with this branch's source before packaging.
"""
import hashlib
from pathlib import Path, PurePosixPath
import sys
from zipfile import ZipFile

DONOR_SHA256 = "ed257d66c546e78036ceedb4c6fb6886014e69020044fd41cfe217858d8eb328"
REPO = Path(__file__).resolve().parents[2]
EXECUTABLES = {"libproot.so", "libproot-loader.so"}


def stage(apk):
    if hashlib.sha256(apk.read_bytes()).hexdigest() != DONOR_SHA256:
        raise ValueError("Upstream 0.3.1 APK checksum does not match")
    count = 0
    with ZipFile(apk) as archive:
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename)
            if entry.is_dir():
                continue
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("Unsafe APK member: " + entry.filename)
            if entry.filename.startswith(("assets/linuxfs/", "assets/droiddeck-esync/")):
                dest = REPO / "app/src/main" / path
            elif str(path.parent) == "lib/arm64-v8a" and path.name in EXECUTABLES:
                dest = REPO / "app/src/main/jniLibs/arm64-v8a" / path.name
            else:
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(archive.read(entry))
            count += 1
    required = [
        "assets/linuxfs/libfakeinput.so", "assets/linuxfs/libblsession.so",
        "assets/linuxfs/usr/local/bin/gamescope",
        "assets/linuxfs/usr/local/lib/droiddeck-wlroots/libwlroots-0.20.so",
        "jniLibs/arm64-v8a/libproot.so", "jniLibs/arm64-v8a/libproot-loader.so",
    ]
    for relative in required:
        if not (REPO / "app/src/main" / relative).is_file():
            raise ValueError("Missing dependency: " + relative)
    print(f"Staged {count} dependency files from verified upstream 0.3.1 APK")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: stage-pixel-dependencies.py DroidDeck-0.3.1.apk")
    stage(Path(sys.argv[1]))
