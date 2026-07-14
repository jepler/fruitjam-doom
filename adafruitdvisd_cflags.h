#ifndef ADAFRUITDVISD_CFLAGS_H
#define ADAFRUITDVISD_CFLAGS_H 1
// Board config for pico_shared HW_CONFIG 2: Raspberry Pi Pico 2 (RP2350) on a
// breadboard or Frank's printed circuit board, with the Adafruit DVI Breakout
// (product 4984) on HSTX and an optional PCM5100A I2S DAC. USB host runs on
// the RP2350's NATIVE USB controller (tinyusb; gamepads via OTG adapter) —
// build with -DENABLE_PIO_USB=0, see adafruitdvisd-build.sh.
// Pin values mirror pico-infonesPlus/pico_shared/BoardConfigs.cmake HW_CONFIG 2.
#define PICO_SCANVIDEO_COLOR_PIN_BASE 8
#define PICO_SCANVIDEO_PIXEL_RCOUNT 5
#define PICO_SCANVIDEO_PIXEL_GCOUNT 5
#define PICO_SCANVIDEO_PIXEL_BCOUNT 5
// also affects palette conversion for hstx! pico_hdmi expects RGB555.
#define PICO_SCANVIDEO_PIXEL_RSHIFT 10
#define PICO_SCANVIDEO_PIXEL_GSHIFT 5
#define PICO_SCANVIDEO_PIXEL_BSHIFT 0
#define PICO_SCANVIDEO_SYNC_PIN_BASE 6
// SD pins (vestigial — no SD driver in this port; kept as wiring documentation)
#define SD_TX 3
#define SD_RX 4
#define SD_SCK 2
#define SD_CS 5
#define USE_SD 1
#define USE_HSTX 1
// I2S audio: optional PCM5100A DAC module on the PCB (harmless with none
// fitted). Driver 2 = PICO_AUDIO_I2S_DRIVER_PCM5000A (see audio_i2s.h).
// Audio always ALSO goes out over HDMI; without a headphone-detect pin the
// build drives both sinks simultaneously (see i_picosound.c).
#define DOOM_AUDIO_I2S_DRIVER 2
#define PICO_AUDIO_I2S_DATA_PIN 26
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// PCM510x has no volume register and benefits from the DC blocker in
// audio_i2s.c (see the I2S_AUDIO_COMPENSATE_DC_OFFSET note in audio_i2s.h).
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1
// No TLV320 codec on this board. The pins are unused (-1 is fine, the codec
// init never runs for driver 2), but WIIPAD_I2C must stay a valid i2c
// instance: tlv320dac3100.c uses it as an i2c_inst_t* in always-compiled code.
#define WIIPAD_I2C i2c1
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1
// Pin the pico_hdmi audio Data-Island ring at 0x20076000. That address is
// SHORTPTR_BASE + 0x40000 — the top of Doom's zone heap (see i_system.c's
// I_ZoneBase()) — and __HeapLimit is 0x20080000, so the 36 KB ring lives in
// the 40 KB SRAM gap between the zone and the SCRATCH banks. Neither Doom's
// zone allocator nor the pico-sdk's stack region touches this address, so
// the ring costs zero zone bytes AND zero .bss bytes. The vendored
// hstx_data_island_queue.c honors this at compile time; without it, it
// would malloc from the zone (its historical behaviour).
#define HSTX_DI_RING_ADDRESS 0x20076000u

// Override the emu8950 OPL native rate (49716 Hz) with a standard HDMI-audio
// rate. pico_hdmi's ACR N/CTS table only covers 32k/44.1k/48k/88.2k/96k/etc.,
// and 49716 falls through to the 48 kHz default — sink plays at 48000 while
// we push at 49716, causing constant FIFO overruns that sound like static.
//
// 48000 Hz is the exact-lock rate for a 25.2 MHz pixel clock (N=6144,
// CTS=25200) AND matches the sample-frequency code baked into
// hstx_packet.c's channel_status_bit[] table. Strict HDMI monitors require
// the IEC 60958 channel status sample-rate field to agree with the actual
// stream rate, so both must be 48 kHz.
#undef PICO_SOUND_SAMPLE_FREQ
#define PICO_SOUND_SAMPLE_FREQ 48000
// HSTX lanes for the Adafruit DVI Breakout wiring of BoardConfigs HW_CONFIG 2.
// pico_hdmi (video_output.c) reads these to program the HSTX bit lanes.
#define GPIOHSTXCK 14
#define GPIOHSTXD0 12
#define GPIOHSTXD1 18
#define GPIOHSTXD2 16
#define GPIOHSTXINVERTED 0

// Native USB host: no HAS_USBPIO / PIN_USB_HOST_* here. tusb_config.h sees
// HAS_USBPIO undefined and selects the native RP2350 controller (rhport 0).

// pico_shared BoardConfigs.cmake identity of this board. Consumed by the
// vendored nespad.cpp to pick its PIO program variant.
#define HW_CONFIG 2
// Legacy NES/SNES controller ports (SNES auto-detected), polled via PIO by
// the vendored pico_shared nespad driver — see i_usbhid.cpp.
#define NES_PIN_CLK 6
#define NES_PIN_DATA 7
#define NES_PIN_LAT 8
#define NES_PIO pio1
#define NES_PIN_CLK_1 9
#define NES_PIN_DATA_1 10
#define NES_PIN_LAT_1 11
#define NES_PIO_1 pio1

// Move the WAD base address up so it lives past the bootloader app slot.
// Standalone build: 0x10080000 (image at 0x10000000, WHX right after the
// 512 KB slot — fits a genuine 4 MB Pico 2).
// pico-bootLoader build (BUILD_FOR_BOOTLOADER=1): 0x10200000. This is a Pico 2
// board, so the whole bootloader map is capped to the 4 MB chip:
//   0x10000000 bootloader (512 KB)
//   0x10080000 emulator app slot (1.5 MB, DOOM_APP_SIZE=0x180000 in the build)
//   0x10200000 doom WHX -> ends 0x103B7018 (~296 KB spare below the 4 MB mark)
// The WHX sits above the app slot so it survives re-flashing doom (or a smaller
// emulator). Fits a genuine 4 MB Pico 2. The build script passes matching
// -DDOOM_APP_SIZE / -DDOOM_FLASH_TOTAL / picotool -o; keep all four in sync.
// See cmake/BootPartition.cmake for the full flash-map rationale.
#undef TINY_WAD_ADDR
#if BUILD_FOR_BOOTLOADER
#define TINY_WAD_ADDR 0x10200000
#else
#define TINY_WAD_ADDR 0x10080000
#endif

#endif
