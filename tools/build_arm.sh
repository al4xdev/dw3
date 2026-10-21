#!/bin/bash
# build_arm.sh: cross-compile this port for the ARM 32-bit handhelds (RK3326: XF35H, R36S and their clones) and assemble
# the package the card runs. The toolchain is a Docker image built from tools/Dockerfile.armhf, so nothing host-wide is
# installed; the output goes to build/armhf-sdl2/dw2003 and the package to ../portmaster (the device mirror).
#
#   tools/build_arm.sh                 build and package
#   DW3_PACKAGE_DIR=... tools/build_arm.sh   assemble the package somewhere else
set -euo pipefail

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$TOOLS_DIR/.." && pwd)"
BUILD_DIR="$REPO/build/armhf-sdl2"
PACKAGE_DIR="${DW3_PACKAGE_DIR:-$REPO/../portmaster}"
IMAGE="dw3-armhf-builder:latest"

echo "=== Building Digimon World 2003 for the RK3326 handhelds (armhf) ==="

if ! command -v docker &> /dev/null; then
    echo "Error: docker is required for the cross-compilation toolchain." >&2
    exit 1
fi

if ! docker image inspect "$IMAGE" &> /dev/null; then
    echo "Building the toolchain image $IMAGE from tools/Dockerfile.armhf..."
    docker build -t "$IMAGE" -f "$TOOLS_DIR/Dockerfile.armhf" "$TOOLS_DIR"
fi

echo "Configuring and compiling with CMake + Ninja inside Docker..."
docker run --rm -u "$(id -u):$(id -g)" \
    -v "$REPO:/work" \
    -w /work \
    "$IMAGE" \
    bash -c "
        cmake -S port -B build/armhf-sdl2 -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=/work/cmake/armhf-linux.cmake \
            -DPSXSTACK_PYTHON=/usr/bin/python3 \
            -DPSXSTACK_SDL2=ON -DPSXSTACK_EGL=ON && \
        ninja -C build/armhf-sdl2 -j\$(nproc)
    "

echo "Build complete: $BUILD_DIR/dw2003"

# The package is what the card holds: the binary plus everything in handheld/ (the launcher and the menu are versioned
# there, so the package comes from the checkout and not from whatever is on the card). The player's own files -
# settings.json, pad.map, the cards and the disc - are never touched.
echo "Assembling the package in $PACKAGE_DIR..."
mkdir -p "$PACKAGE_DIR/dw3/game" "$PACKAGE_DIR/dw3/mods"

cp "$BUILD_DIR/dw2003" "$PACKAGE_DIR/dw3/dw2003"
chmod +x "$PACKAGE_DIR/dw3/dw2003"

# Retired files: the package is a mirror, so a file that left handheld/ has to leave it too (the copy loop only adds).
rm -f "$PACKAGE_DIR/dw3/dw3-tui.sh"   # the terminal menu, replaced by nothing: the console has one UI, dw3-menu.py

cp "$REPO/handheld/Digimon World 3.sh" "$PACKAGE_DIR/Digimon World 3.sh"
chmod +x "$PACKAGE_DIR/Digimon World 3.sh"

# Every file in handheld/ goes into dw3/, by rule and not by list: a list drops the next file somebody adds (that is how
# the LED daemon nearly stayed behind). README.md is the repository's own document and the launcher sits above.
for f in "$REPO"/handheld/*; do
    case "$(basename "$f")" in
        README.md|"Digimon World 3.sh"|__pycache__) continue ;;
    esac
    cp "$f" "$PACKAGE_DIR/dw3/"
done
chmod -x "$PACKAGE_DIR/dw3/README.txt" 2>/dev/null || true
chmod +x "$PACKAGE_DIR/dw3/dw3.sh" "$PACKAGE_DIR/dw3/dw3-menu.py" 2>/dev/null || true
chmod +x "$PACKAGE_DIR/dw3/ledd.py" 2>/dev/null || true

if [ -d "$BUILD_DIR/mods" ]; then
    cp -r "$BUILD_DIR/mods/"* "$PACKAGE_DIR/dw3/mods/" || true
fi

echo "=== Done ==="
echo "  binary:  $BUILD_DIR/dw2003"
echo "  package: $PACKAGE_DIR ($(md5sum "$PACKAGE_DIR/dw3/dw2003" | cut -d' ' -f1))"
