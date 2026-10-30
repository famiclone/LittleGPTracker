#!/usr/bin/env bash
# Fetch the cross toolchain needed to build the TG5040 (TrimUI Brick / Smart
# Pro, NextUI) target.
#
# This is the same pair of archives NextUI's own container image uses, just
# unpacked into a plain directory so no container runtime is required. The
# binaries are ordinary x86_64 glibc executables: on NixOS they run as-is if
# programs.nix-ld is enabled, otherwise wrap with steam-run.
#
#   ./tools/setup-toolchain.sh [dest]
#
# Default dest is ../toolchain/aarch64-nextui-linux-gnu, which is where
# shell.nix and projects/Makefile.TG5040 look.
set -euo pipefail

DEST="${1:-$(cd "$(dirname "$0")/.." && pwd)/../toolchain/aarch64-nextui-linux-gnu}"
DEST="$(mkdir -p "$DEST" && cd "$DEST" && pwd)"
SYSROOT="$DEST/aarch64-nextui-linux-gnu/libc"

TC_URL="https://github.com/LoveRetro/gcc-arm-8.3-aarch64-tg5040/releases/download/v8.3.0-20260204-190602-d37268f5/gcc-8.3.0-aarch64-nextui-linux-gnu-x86_64-host.tar.xz"
SDK_URL="https://github.com/trimui/toolchain_sdk_smartpro/releases/download/20231018/SDK_usr_tg5040_a133p.tgz"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> downloading cross toolchain (~222 MB)"
curl -fL --progress-bar -o "$TMP/toolchain.tar.xz" "$TC_URL"
echo "==> downloading TrimUI SDK sysroot (~177 MB)"
curl -fL --progress-bar -o "$TMP/sdk.tgz" "$SDK_URL"

echo "==> unpacking toolchain into $DEST"
tar -xJf "$TMP/toolchain.tar.xz" -C "$DEST" --strip-components=2 --no-same-permissions --no-same-owner

# The toolchain tar sets its sysroot directories read-only (mode 555), so the
# SDK extraction below cannot merge into them until they are writable again.
chmod -R u+w "$DEST"

echo "==> unpacking SDK sysroot into $SYSROOT"
mkdir -p "$SYSROOT"
tar -xzf "$TMP/sdk.tgz" -C "$SYSROOT" --no-same-permissions --delay-directory-restore

GCC="$DEST/bin/aarch64-nextui-linux-gnu-gcc"
echo
echo "==> verifying"
"$GCC" --version | head -1
test -f "$SYSROOT/usr/include/SDL2/SDL.h" && echo "SDL2 headers: ok"
test -f "$SYSROOT/usr/lib/libSDL2.so"     && echo "SDL2 library: ok"
echo
echo "toolchain ready: $DEST"
echo "build with:  nix-shell --run 'cd projects && make PLATFORM=TG5040'"
