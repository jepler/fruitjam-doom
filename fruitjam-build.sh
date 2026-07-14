#!/bin/sh
TAG=fruitjam
BUILD=build_${TAG}
export CFLAGS="-include $(pwd)/fruitjam_cflags.h -g3 -ggdb"
export CXXFLAGS="-include $(pwd)/fruitjam_cflags.h -g3 -ggdb"
cmake -S . -B $BUILD \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_SDK_PATH=3rdparty/pico-sdk \
    -DPICO_EXTRAS_PATH=3rdparty/pico-extras \
    -DPICOTOOL_FETCH_FROM_GIT_PATH="$(pwd)/picotool" \
    -DBOARD=adafruit_fruit_jam -DPICO_BOARD=adafruit_fruit_jam \
    -DUSE_HSTX=1 \
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
# Standalone (non-bootloader) build: doom owns flash from 0x10000000 and the
# WHX is flashed at 0x10080000, matching the non-bootloader TINY_WAD_ADDR in
# fruitjam_cflags.h. (The bootloader build uses a different map, with the WHX
# at 0x10400000 -- see fruitjam-build-forbootloader.sh.)
"$PICOTOOL" uf2 convert doom1.whx -t bin $BUILD/src/doom1-whx.uf2 -o 0x10080000 --family data
