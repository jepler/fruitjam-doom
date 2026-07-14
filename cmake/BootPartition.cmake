# BootPartition.cmake - Relink doom_tiny for the pico-bootLoader app partition.
#
# The pico-bootLoader (https://github.com/FrankHoedemakers/pico-bootLoader)
# owns the first 512 KB of flash and treats everything after it as an
# application partition. To be launchable from the picker, doom_tiny must
# be linked with FLASH ORIGIN=0x10080000 instead of the RP2350 default
# 0x10000000. This file adapts the same relink helper that lives in
# pico-bootLoader's pico_shared/BootPartition.cmake, scoped to what doom
# needs (no slot-mode / no pinned-slot helper).
#
# Flash map assumed here:
#
#   0x10000000  bootloader                    (512 KB)
#   0x10080000  emulator app partition        (3 MB reserved for doom_tiny)
#   0x10400000  doom WHX data (TINY_WAD_ADDR)
#   0x11000000  end of 16 MB Fruit Jam flash
#
# The 3 MB emulator cap keeps a comfortable margin below the WHX; the
# current doom_tiny.bin is ~260 KB.

if(DEFINED _DOOM_BOOTPARTITION_INCLUDED)
    return()
endif()
set(_DOOM_BOOTPARTITION_INCLUDED 1)

# Defaults below are the 16 MB Fruit Jam / 8 MB Feather map (WHX at 0x10400000).
# The 4 MB Pico 2 boards (adafruitdvisd, murmulatorm2) cap the map to their chip
# by overriding DOOM_APP_SIZE (1.5 MB) and DOOM_FLASH_TOTAL (4 MB) via -D on the
# cmake line -- see their *-build-forbootloader.sh. These are CACHE vars, so a
# command-line -D wins over the set() defaults here; don't change the defaults.
set(DOOM_XIP_BASE     "0x10000000" CACHE STRING "RP2350 XIP flash base")
set(DOOM_APP_BASE     "0x10080000" CACHE STRING "Bootloader app-partition base")
set(DOOM_APP_SIZE     "0x300000"   CACHE STRING "Bytes reserved for doom_tiny (3 MB, up to 0x10380000)")
set(DOOM_FLASH_TOTAL  "0x1000000"  CACHE STRING "Total external flash on the board (Fruit Jam = 16 MB)")

# _doom_relink_flash(<target> <origin_hex> <length_hex>)
#
# Take the SDK's default RP2350 memory map, rewrite the FLASH(rx) region so
# ORIGIN/LENGTH point at the bootloader's app partition, and hand the patched
# script to the target. Also force PICO_FLASH_SIZE_BYTES to match the real
# chip so hard_assert in flash_range_erase / flash_range_program can't panic
# on a board header that lies about capacity.
function(_doom_relink_flash TARGET ORIGIN LENGTH)
    set(_candidates
        "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld"
        "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/memmap_default.ld"
    )
    set(_src_ld "")
    foreach(_c IN LISTS _candidates)
        if(EXISTS "${_c}")
            set(_src_ld "${_c}")
            break()
        endif()
    endforeach()
    if(_src_ld STREQUAL "")
        file(GLOB_RECURSE _found
             "${PICO_SDK_PATH}/src/rp2_common/*rp2350*/memmap_default.ld")
        if(_found)
            list(GET _found 0 _src_ld)
        endif()
    endif()
    if(_src_ld STREQUAL "")
        message(FATAL_ERROR
            "BootPartition: could not find an RP2350 memmap_default.ld under "
            "${PICO_SDK_PATH}. Build for the rp2350 platform with SDK >= 2.0.0.")
    endif()

    file(READ "${_src_ld}" _ld_text)

    # Newer SDKs pull the FLASH region in from a generated pico_flash_region.ld.
    # Replace that INCLUDE with an explicit, capped FLASH region.
    string(REPLACE
        "INCLUDE \"pico_flash_region.ld\""
        "FLASH(rx) : ORIGIN = ${ORIGIN}, LENGTH = ${LENGTH}"
        _ld_text "${_ld_text}")
    # Older SDKs spell the region out inline; rewrite that form too (idempotent
    # if the INCLUDE replacement above already produced this line).
    string(REGEX REPLACE
        "FLASH\\(rx\\)[ \t]*:[^\n]*"
        "FLASH(rx) : ORIGIN = ${ORIGIN}, LENGTH = ${LENGTH}"
        _ld_text "${_ld_text}")

    set(_out_ld "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_bootloader_memmap.ld")
    file(WRITE "${_out_ld}" "${_ld_text}")
    message(STATUS "BootPartition: ${TARGET} -> ORIGIN=${ORIGIN} LENGTH=${LENGTH} (from ${_src_ld})")
    pico_set_linker_script(${TARGET} "${_out_ld}")

    target_compile_definitions(${TARGET} PRIVATE
        PICO_FLASH_SIZE_BYTES=${DOOM_FLASH_TOTAL})
endfunction()

# Public entry point: link <target> into the bootloader app partition.
function(frens_offset_for_bootloader TARGET)
    _doom_relink_flash(${TARGET} ${DOOM_APP_BASE} ${DOOM_APP_SIZE})
endfunction()
