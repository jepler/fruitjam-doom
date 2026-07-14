#ifndef FEATHERRP2350_CFLAGS_H
#define FEATHERRP2350_CFLAGS_H 1
// Board config for pico_shared HW_CONFIG 14: Adafruit Feather RP2350 (HSTX
// port) with a TLV320DAC3100 I2S DAC breakout and PIO-USB host on GPIO 24/25.
// Build with -DPICO_BOARD=adafruit_feather_rp2350 (8 MB flash, LED on 7),
// see featherrp2350-build.sh.
// Pin values mirror pico-infonesPlus/pico_shared/BoardConfigs.cmake HW_CONFIG 14.
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
#define SD_TX 23
#define SD_RX 4
#define SD_SCK 6
#define SD_CS 5
#define USE_SD 1
#define USE_HSTX 1
// I2S audio: TLV320DAC3100 breakout. Driver 1 = PICO_AUDIO_I2S_DRIVER_TLV320
// (see audio_i2s.h). No headphone-detect INT pin is wired, so the build
// drives HDMI and the DAC simultaneously (see i_picosound.c) instead of the
// Fruit Jam's jack-based exclusive routing.
#define DOOM_AUDIO_I2S_DRIVER 1
#define PICO_AUDIO_I2S_DATA_PIN 11
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 9
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN 26
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// I2C bus for the TLV320DAC3100 codec (macros consumed by tlv320dac3100.c).
// Note: i2c1 is also piconet's i2c_default — harmless, piconet's I2C IRQ
// never enables on HSTX builds (its pins are taken over by the HSTX lanes).
#define WIIPAD_I2C i2c1
#define WII_PIN_SDA 2
#define WII_PIN_SCL 3
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
// HSTX lanes for the Adafruit Feather RP2350 / Metro RP2350 wiring
// (BoardConfigs HW_CONFIG 14). pico_hdmi (video_output.c) reads these.
#define GPIOHSTXCK 14
#define GPIOHSTXD0 18
#define GPIOHSTXD1 16
#define GPIOHSTXD2 12
#define GPIOHSTXINVERTED 0

// PIO-USB host on GPIO 24 (D+) / 25 (D-), e.g. the Adafruit USB Host
// FeatherWing. No VBUS-enable GPIO on this wiring. Keep in sync with the
// ENABLE_PIO_USB default (ON) in src/pico/CMakeLists.txt.
#define HAS_USBPIO
#define PIN_USB_HOST_DP (24u)
#define PIN_USB_HOST_DM (25u)

// pico_shared BoardConfigs.cmake identity of this board. Consumed by the
// vendored nespad.cpp to pick its PIO program variant.
#define HW_CONFIG 14
// No legacy NES/SNES controller ports on this board; -1 disables the nespad
// code paths in i_usbhid.cpp.
#define NES_PIN_CLK -1
#define NES_PIN_DATA -1
#define NES_PIN_LAT -1
#define NES_PIO pio1
#define NES_PIN_CLK_1 -1
#define NES_PIN_DATA_1 -1
#define NES_PIN_LAT_1 -1
#define NES_PIO_1 pio1

// Move the WAD base address way up since we have plenty of flash.
// Standalone build: 0x10080000 (image at 0x10000000, WHX right after the
// 512 KB slot).
// pico-bootLoader build (BUILD_FOR_BOOTLOADER=1): 0x10400000, past the
// emulator app partition so the WAD survives flashing other emulators —
// fits the Feather's 8 MB flash (0x10400000 + 1.8 MB < 8 MB).
// See cmake/BootPartition.cmake for the full flash-map rationale.
#undef TINY_WAD_ADDR
#if BUILD_FOR_BOOTLOADER
#define TINY_WAD_ADDR 0x10400000
#else
#define TINY_WAD_ADDR 0x10080000
#endif

#endif
