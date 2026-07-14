#!/bin/sh
# HW_CONFIG 13 (Murmulator M2) built for the pico-bootLoader.
# Flash map is capped to the 4 MB Pico 2: bootloader (512 KB) at 0x10000000,
# a 1.5 MB emulator app slot at 0x10080000 (DOOM_APP_SIZE=0x180000), and the
# WHX at 0x10200000 (WHX_ADDR below), which ends ~0x103B7018 -- under the 4 MB
# mark. Fits a genuine 4 MB Pico 2. WHX_ADDR must match the BUILD_FOR_BOOTLOADER
# TINY_WAD_ADDR in murmulatorm2_cflags.h.
TAG=murmulatorm2
BUILD=build_bl_${TAG}
WHX_ADDR=0x10200000
export CFLAGS="-include $(pwd)/${TAG}_cflags.h -g3 -ggdb"
export CXXFLAGS="-include $(pwd)/${TAG}_cflags.h -g3 -ggdb"
cmake -S . -B $BUILD \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_SDK_PATH=3rdparty/pico-sdk \
    -DPICO_EXTRAS_PATH=3rdparty/pico-extras \
    -DPICOTOOL_FETCH_FROM_GIT_PATH="$(pwd)/picotool" \
    -DPICO_BOARD=pico2 \
    -DUSE_HSTX=1 \
    -DENABLE_PIO_USB=0 \
    -DDOOM_NO_STDIO_UART=1 \
    -DBUILD_FOR_BOOTLOADER=ON \
    -DDOOM_APP_SIZE=0x180000 \
    -DDOOM_FLASH_TOTAL=0x400000 \
    ${CMAKE_ARGS} "$@"

make -C $BUILD -j$(nproc)

PICOTOOL=./picotool/picotool-build/picotool
if [ ! -x "$PICOTOOL" ]; then
    PICOTOOL=$(command -v picotool)
fi
if [ -z "$PICOTOOL" ]; then
    echo "picotool not found (looked at ./picotool/picotool-build/picotool and PATH)" >&2
    exit 1
fi
# pico-bootLoader flash map: WHX lives at 0x10200000 (past the 1.5 MB app slot
# reserved for the doom_tiny image at 0x10080000). Must match TINY_WAD_ADDR
# in murmulatorm2_cflags.h (the BUILD_FOR_BOOTLOADER branch) and WHX_ADDR above.
"$PICOTOOL" uf2 convert doom1.whx -t bin $BUILD/src/doom1-whx.uf2 -o "$WHX_ADDR" --family data
