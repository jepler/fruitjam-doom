#ifndef FRUITJAM_CFLAGS_H
#define FRUITJAM_CFLAGS_H 1
#define PICO_SCANVIDEO_COLOR_PIN_BASE 8
#define PICO_SCANVIDEO_PIXEL_RCOUNT 5
#define PICO_SCANVIDEO_PIXEL_GCOUNT 5
#define PICO_SCANVIDEO_PIXEL_BCOUNT 5
// also affects palette conversion for hstx! pico_hdmi expects RGB555.
#define PICO_SCANVIDEO_PIXEL_RSHIFT 10
#define PICO_SCANVIDEO_PIXEL_GSHIFT 5
#define PICO_SCANVIDEO_PIXEL_BSHIFT 0
#define PICO_SCANVIDEO_SYNC_PIN_BASE 6
#define SD_TX 35
#define SD_RX 36
#define SD_SCK 34
#define SD_CS 39
#define USE_SD 1
#define USE_HSTX 1
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 44
#define PICO_DEFAULT_UART_RX_PIN 45
#define PICO_AUDIO_I2S_DATA_PIN 24
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 26
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN 22
#define PICO_AUDIO_I2S_INTERRUPT_PIN 23
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// Which audio_i2s driver this board carries, as a numeric literal (values
// from audio_i2s.h: 1 = TLV320, 2 = PCM5000A). Consumed by i_picosound.c.
#define DOOM_AUDIO_I2S_DRIVER 1
// I2C bus for the TLV320DAC3100 codec (macros consumed by tlv320dac3100.c).
#define WIIPAD_I2C i2c0
#define WII_PIN_SDA 20
#define WII_PIN_SCL 21
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
#define HSTX_CKP 13
#define HSTX_D0P 15
#define HSTX_D1P 17
#define HSTX_D2P 19
// pico_hdmi (video_output.c) reads these to program the HSTX bit lanes.
#define GPIOHSTXCK HSTX_CKP
#define GPIOHSTXD0 HSTX_D0P
#define GPIOHSTXD1 HSTX_D1P
#define GPIOHSTXD2 HSTX_D2P
#define GPIOHSTXINVERTED 1

#define HAS_USBPIO
#define PIN_USB_HOST_DP (1u)
#define PIN_USB_HOST_DM (2u)
#define PIN_USB_HOST_VBUS (11u)

// pico_shared BoardConfigs.cmake identity of this board (Fruit Jam = HW_CONFIG
// 8). Consumed by the vendored nespad.cpp to pick its PIO program variant.
#define HW_CONFIG 8
// No legacy NES/SNES controller ports wired for Doom on the Fruit Jam; -1
// disables the nespad code paths in i_usbhid.cpp.
#define NES_PIN_CLK -1
#define NES_PIN_DATA -1
#define NES_PIN_LAT -1
#define NES_PIO pio1
#define NES_PIN_CLK_1 -1
#define NES_PIN_DATA_1 -1
#define NES_PIN_LAT_1 -1
#define NES_PIO_1 pio1

// Move the WAD base address way up since we have plenty of flash.
// Standalone build: 0x10080000 (as documented in README.md, "fruit jam
// ALWAYS uses the offset of 0x10080000").
// pico-bootLoader build (BUILD_FOR_BOOTLOADER=1): 0x10400000, past the
// emulator app partition so the WAD survives flashing other emulators.
// See cmake/BootPartition.cmake for the full flash-map rationale.
#undef TINY_WAD_ADDR
#if BUILD_FOR_BOOTLOADER
#define TINY_WAD_ADDR 0x10400000
#else
#define TINY_WAD_ADDR 0x10080000
#endif

#endif
