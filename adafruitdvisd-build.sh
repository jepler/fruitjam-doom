#!/bin/sh
# HW_CONFIG 2: Pico 2 + Adafruit DVI Breakout (+ optional PCM5100A DAC) —
# breadboard or PCB. Native USB host: ENABLE_PIO_USB=0 must match the absence
# of HAS_USBPIO in adafruitdvisd_cflags.h (a mismatch fails the build).
#
# Usage:
#   ./adafruitdvisd-build.sh [cmake args...]        build only (UF2/BOOTSEL flow)
#   ./adafruitdvisd-build.sh flash [cmake args...]  build, flash WAD + game over
#                                                   SWD via openocd, reset & run
#   ./adafruitdvisd-build.sh debug [cmake args...]  build, flash WAD if changed,
#                                                   load the game under gdb and
#                                                   halt at reset
# flash/debug need an RP2350-aware openocd and a CMSIS-DAP probe (e.g. the
# Raspberry Pi Debug Probe) on SWD. Overridable: OPENOCD, GDB, ADAPTER_SPEED.
MODE=build
case "$1" in
    flash|debug) MODE=$1; shift ;;
esac

TAG=adafruitdvisd
BUILD=build_${TAG}
WHX_ADDR=0x10080000
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
    ${CMAKE_ARGS} "$@"

make -C $BUILD -j$(nproc) || exit 1

if [ "$MODE" = "build" ]; then
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
    # adafruitdvisd_cflags.h. Fits a genuine 4 MB Pico 2. (The bootloader build
    # uses a different map -- see adafruitdvisd-build-forbootloader.sh.)
    "$PICOTOOL" uf2 convert doom1.whx -t bin $BUILD/src/doom1-whx.uf2 -o $WHX_ADDR --family data
    exit 0
fi

# --- SWD flash/debug (same flash map as the UF2 flow above) ---
ELF=$BUILD/src/doom_tiny.elf
OPENOCD=${OPENOCD:-openocd}
ADAPTER_SPEED=${ADAPTER_SPEED:-5000}
OOCD_CFG="-f interface/cmsis-dap.cfg -f target/rp2350.cfg"

if [ "$MODE" = "flash" ]; then
    # preverify CRC-checks the WHX already in flash and skips the slow 1.8 MB
    # write when it is unchanged, so re-flashing only rewrites the game.
    exec "$OPENOCD" $OOCD_CFG -c "adapter speed $ADAPTER_SPEED" \
        -c "program doom1.whx $WHX_ADDR preverify verify" \
        -c "program $ELF verify reset exit"
fi

# debug: make sure the WHX is in flash, then let gdb flash the ELF itself.
GDB=${GDB:-}
if [ -z "$GDB" ]; then
    for g in arm-none-eabi-gdb gdb-multiarch; do
        if command -v $g >/dev/null 2>&1; then GDB=$g; break; fi
    done
fi
if [ -z "$GDB" ]; then
    echo "no gdb found (looked for arm-none-eabi-gdb and gdb-multiarch; set GDB=...)" >&2
    exit 1
fi

"$OPENOCD" $OOCD_CFG -c "adapter speed $ADAPTER_SPEED" \
    -c "program doom1.whx $WHX_ADDR preverify verify exit" || exit 1

# gdb spawns openocd itself over a pipe, so openocd's lifetime is tied to the
# gdb session and Ctrl-C stays a target-halt, not an openocd kill. openocd's
# log goes to $BUILD/openocd.log to keep the gdb console clean.
echo "Target will be halted at reset after load: set breakpoints, then 'continue'."
exec "$GDB" "$ELF" \
    -ex "target extended-remote | $OPENOCD $OOCD_CFG -c 'adapter speed $ADAPTER_SPEED' -c 'gdb_port pipe; log_output $BUILD/openocd.log'" \
    -ex "monitor reset init" \
    -ex "load" \
    -ex "monitor reset init"
