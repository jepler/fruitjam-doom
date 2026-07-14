//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Main program, simply calls D_DoomMain high level loop.
//

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#if !LIB_PICO_STDLIB
#include "SDL.h"
#else
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/sem.h"
#include "pico/multicore.h"
#if PICO_ON_DEVICE
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/pll.h"
#endif
#endif
#if USE_PICO_NET
#include "piconet.h"
#endif
#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#endif
//
// D_DoomMain()
// Not a globally visible function, just included for source reference,
// calls all startup code, parses command line options.
//

void D_DoomMain (void);

#if defined(ADAFRUIT_FRUIT_JAM)
#include "hardware/i2c.h"
#define DAC_I2C_ADDR 0x18
#define DEBUG_I2C (0)

static void writeRegister(uint8_t reg, uint8_t value) {
  uint8_t buf[2];
  buf[0] = reg;
  buf[1] = value;
  int res = i2c_write_timeout_us(i2c0, DAC_I2C_ADDR, buf, sizeof(buf), /* nostop */ false, 1000);
  if (res != 2) {
    panic("i2c_write_timeout failed: res=%d\n", res);
  }
if(DEBUG_I2C)
  printf("Write Reg: %d = 0x%x\n", reg, value);
}

static uint8_t readRegister(uint8_t reg) {
  uint8_t buf[1];
  buf[0] = reg;
  int res = i2c_write_timeout_us(i2c0, DAC_I2C_ADDR, buf, sizeof(buf), /* nostop */ true, 1000);
  if (res != 1) {
if(DEBUG_I2C)
printf("res=%d\n", res);
    panic("i2c_write_timeout failed: res=%d\n", res);
  }
  res = i2c_read_timeout_us(i2c0, DAC_I2C_ADDR, buf, sizeof(buf), /* nostop */ false, 1000);
  if (res != 1) {
      if(DEBUG_I2C)
        printf("res=%d\n", res);
      panic("i2c_read_timeout failed: res=%d\n", res);
  }
  uint8_t value = buf[0];
  if(DEBUG_I2C)
    printf("Read Reg: %d = 0x%x\n", reg, value);
  return value;
}

static void modifyRegister(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = readRegister(reg);
  if(DEBUG_I2C)
    printf("Modify Reg: %d = [Before: 0x%x] with mask 0x%x and value 0x%x\n", reg, current, mask, value);
  uint8_t new_value = (current & ~mask) | (value & mask);
  writeRegister(reg, new_value);
}

static void setPage(uint8_t page) {
  printf("Set page %d\n", page);
  writeRegister(0x00, page);
}


static void Wire_begin() {
    i2c_init(i2c0, 100000);
    gpio_set_function(20, GPIO_FUNC_I2C);
    gpio_set_function(21, GPIO_FUNC_I2C);
}

static void fruitjam_init_i2s(void) {
  gpio_init(22);
  gpio_set_dir(22, true);
  gpio_put(22, true); // allow i2s to come out of reset

  Wire_begin();
  sleep_ms(1000);
  
  if(DEBUG_I2C)
    printf("initialize codec\n");

  // Reset codec
  writeRegister(0x01, 0x01);
  sleep_ms(10);

  // Interface Control
  modifyRegister(0x1B, 0xC0, 0x00);
  modifyRegister(0x1B, 0x30, 0x00);

  // Clock MUX and PLL settings
  modifyRegister(0x04, 0x03, 0x03);
  modifyRegister(0x04, 0x0C, 0x04);
  
  writeRegister(0x06, 0x20); // PLL J
  writeRegister(0x08, 0x00); // PLL D LSB
  writeRegister(0x07, 0x00); // PLL D MSB
  
  modifyRegister(0x05, 0x0F, 0x02); // PLL P/R
  modifyRegister(0x05, 0x70, 0x10);

  // DAC/ADC Config
  modifyRegister(0x0B, 0x7F, 0x08); // NDAC
  modifyRegister(0x0B, 0x80, 0x80);
  
  modifyRegister(0x0C, 0x7F, 0x02); // MDAC
  modifyRegister(0x0C, 0x80, 0x80);
  
  modifyRegister(0x12, 0x7F, 0x08); // NADC
  modifyRegister(0x12, 0x80, 0x80);
  
  modifyRegister(0x13, 0x7F, 0x02); // MADC
  modifyRegister(0x13, 0x80, 0x80);

  // PLL Power Up
  modifyRegister(0x05, 0x80, 0x80);

  // Headset and GPIO Config
setPage(1);
modifyRegister(0x2e, 0xFF, 0x0b); 
setPage(0);
  modifyRegister(0x43, 0x80, 0x80); // Headset Detect
  modifyRegister(0x30, 0x80, 0x80); // INT1 Control
  modifyRegister(0x33, 0x3C, 0x14); // GPIO1


  // DAC Setup
  modifyRegister(0x3F, 0xC0, 0xC0);

  // DAC Routing
  setPage(1);
  modifyRegister(0x23, 0xC0, 0x40);
  modifyRegister(0x23, 0x0C, 0x04);

  // DAC Volume Control
  setPage(0);
  modifyRegister(0x40, 0x0C, 0x00);
  writeRegister(0x41, 0x28); // Left DAC Vol
  writeRegister(0x42, 0x28); // Right DAC Vol

  // ADC Setup
  modifyRegister(0x51, 0x80, 0x80);
  modifyRegister(0x52, 0x80, 0x00);
  writeRegister(0x53, 0x68); // ADC Volume

  // Headphone and Speaker Setup
  setPage(1);
  modifyRegister(0x1F, 0xC0, 0xC0); // HP Driver
  modifyRegister(0x28, 0x04, 0x04); // HP Left Gain
  modifyRegister(0x29, 0x04, 0x04); // HP Right Gain
  writeRegister(0x24, 0x0A);  // Left Analog HP
  writeRegister(0x25, 0x0A);  // Right Analog HP
  
  modifyRegister(0x28, 0x78, 0x40); // HP Left Gain
  modifyRegister(0x29, 0x78, 0x40); // HP Right Gain

  // Speaker Amp
  modifyRegister(0x20, 0x80, 0x80);
  modifyRegister(0x2A, 0x04, 0x04);
  modifyRegister(0x2A, 0x18, 0x08);
  writeRegister(0x26, 0x0A);

  // Return to page 0
  setPage(0);

  if(DEBUG_I2C)
    printf("Initialization complete!\n");


  // Read all registers for verification
  if(DEBUG_I2C) {
    printf("Reading all registers for verification:\n");
    
    setPage(0);
    readRegister(0x00);  // AIC31XX_PAGECTL
    readRegister(0x01);  // AIC31XX_RESET
    readRegister(0x03);  // AIC31XX_OT_FLAG
    readRegister(0x04);  // AIC31XX_CLKMUX
    readRegister(0x05);  // AIC31XX_PLLPR
    readRegister(0x06);  // AIC31XX_PLLJ
    readRegister(0x07);  // AIC31XX_PLLDMSB
    readRegister(0x08);  // AIC31XX_PLLDLSB
    readRegister(0x0B);  // AIC31XX_NDAC
    readRegister(0x0C);  // AIC31XX_MDAC
    readRegister(0x0D);  // AIC31XX_DOSRMSB
    readRegister(0x0E);  // AIC31XX_DOSRLSB
    readRegister(0x10);  // AIC31XX_MINI_DSP_INPOL
    readRegister(0x12);  // AIC31XX_NADC
    readRegister(0x13);  // AIC31XX_MADC
    readRegister(0x14);  // AIC31XX_AOSR
    readRegister(0x19);  // AIC31XX_CLKOUTMUX
    readRegister(0x1A);  // AIC31XX_CLKOUTMVAL
    readRegister(0x1B);  // AIC31XX_IFACE1
    readRegister(0x1C);  // AIC31XX_DATA_OFFSET
    readRegister(0x1D);  // AIC31XX_IFACE2
    readRegister(0x1E);  // AIC31XX_BCLKN
    readRegister(0x1F);  // AIC31XX_IFACESEC1
    readRegister(0x20);  // AIC31XX_IFACESEC2
    readRegister(0x21);  // AIC31XX_IFACESEC3
    readRegister(0x22);  // AIC31XX_I2C
    readRegister(0x24);  // AIC31XX_ADCFLAG
    readRegister(0x25);  // AIC31XX_DACFLAG1
    readRegister(0x26);  // AIC31XX_DACFLAG2
    readRegister(0x27);  // AIC31XX_OFFLAG
    readRegister(0x2C);  // AIC31XX_INTRDACFLAG
    readRegister(0x2D);  // AIC31XX_INTRADCFLAG
    readRegister(0x2E);  // AIC31XX_INTRDACFLAG2
    readRegister(0x2F);  // AIC31XX_INTRADCFLAG2
    readRegister(0x30);  // AIC31XX_INT1CTRL
    readRegister(0x31);  // AIC31XX_INT2CTRL
    readRegister(0x33);  // AIC31XX_GPIO1
    readRegister(0x3C);  // AIC31XX_DACPRB
    readRegister(0x3D);  // AIC31XX_ADCPRB
    readRegister(0x3F);  // AIC31XX_DACSETUP
    readRegister(0x40);  // AIC31XX_DACMUTE
    readRegister(0x41);  // AIC31XX_LDACVOL
    readRegister(0x42);  // AIC31XX_RDACVOL
    readRegister(0x43);  // AIC31XX_HSDETECT
    readRegister(0x51);  // AIC31XX_ADCSETUP
    readRegister(0x52);  // AIC31XX_ADCFGA
    readRegister(0x53);  // AIC31XX_ADCVOL

    setPage(1);
    readRegister(0x1F);  // AIC31XX_HPDRIVER
    readRegister(0x20);  // AIC31XX_SPKAMP
    readRegister(0x21);  // AIC31XX_HPPOP
    readRegister(0x22);  // AIC31XX_SPPGARAMP
    readRegister(0x23);  // AIC31XX_DACMIXERROUTE
    readRegister(0x24);  // AIC31XX_LANALOGHPL
    readRegister(0x25);  // AIC31XX_RANALOGHPR
    readRegister(0x26);  // AIC31XX_LANALOGSPL
    readRegister(0x27);  // AIC31XX_RANALOGSPR
    readRegister(0x28);  // AIC31XX_HPLGAIN
    readRegister(0x29);  // AIC31XX_HPRGAIN
    readRegister(0x2A);  // AIC31XX_SPLGAIN
    readRegister(0x2B);  // AIC31XX_SPRGAIN
    readRegister(0x2C);  // AIC31XX_HPCONTROL
    readRegister(0x2E);  // AIC31XX_MICBIAS
    readRegister(0x2F);  // AIC31XX_MICPGA
    readRegister(0x30);  // AIC31XX_MICPGAPI
    readRegister(0x31);  // AIC31XX_MICPGAMI
    readRegister(0x32);  // AIC31XX_MICPGACM

    setPage(3);
    readRegister(0x10);  // AIC31XX_TIMERDIVIDER
   }
}
#endif
#if PICO_ON_DEVICE
#include "pico/binary_info.h"
bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));
#endif


int NO_MUSIC, NO_SFX;
int main(int argc, char **argv)
{
#if defined(ADAFRUIT_FRUIT_JAM)
  // let's poll the hard buttons... (Fruit Jam buttons 2/3 on GPIO 4/5 —
  // other boards use those GPIOs for SD/controller wiring, so keep this
  // strictly Fruit Jam: a low-driven pin would silently disable sound.)
  gpio_init_mask((1<<4) | (1<<5));
  gpio_pull_up(4);
  gpio_pull_up(5);
#endif
    // save arguments
#if !NO_USE_ARGS
    myargc = argc;
    myargv = argv;
#endif
#if PICO_ON_DEVICE
#if PICO_RP2350
#if defined(ADAFRUIT_FRUIT_JAM)
    fruitjam_init_i2s();
#endif
#endif
#if 1
    // 378 MHz @ 1.60 V — the proven pico-infonesPlus overclock for this
    // board. Sequence ported from pico_shared's FrensHelpers.cpp
    // setClocksAndStartStdio() (378 MHz / PIO-USB path); keep it in step
    // with that helper.
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    // Relax XIP timing BEFORE raising clk_sys: 4x flash divisor (378/4 =
    // 94.5 MHz) plus rxdelay/cooldown margins. Whole-register value is the
    // hardware-proven profile from FrensHelpers.cpp — without it, XIP
    // fetches fault at this frequency.
    qmi_hw->m[0].timing = 0x60007304;
    sleep_ms(100);
    // (378 is not a multiple of 12 MHz; Pico-PIO-USB runs on a fractional
    // divider here — confirmed working in pico-infonesPlus on this board.)
    set_sys_clock_khz(378000, true);
    sleep_ms(100);
#ifdef HAS_USBPIO
    // clk_hstx: deliberately NOT derived from clk_sys even though
    // 378/3 = 126 is an integer division — at 378 MHz PLL_SYS jitter
    // propagates into the TMDS bit clock and strict HDMI sinks show
    // dots/sparkles (eye-pattern closure; see FrensHelpers.cpp). Instead
    // retask PLL_USB as a dedicated, fixed 126 MHz HSTX source. Safe on
    // PIO-USB builds only — the USB controller (which needs PLL_USB at
    // 48 MHz) is unused when the host port runs on Pico-PIO-USB.
    pll_deinit(pll_usb);
    pll_init(pll_usb, 1, 756000000, 6, 1); // 756 / (6*1) = 126 MHz
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    126000000u,
                    126000000u);
#else
    // Native-USB boards (adafruitdvisd, murmulatorm2): the USB controller
    // needs PLL_USB at its stock 48 MHz, so PLL_USB cannot be retasked as
    // the HSTX source. Derive clk_hstx from clk_sys instead — 378/3 = 126
    // exactly, which keeps the 25.2 MHz pixel clock and satisfies the
    // 126 MHz assert in pico_hdmi_glue.c. This mirrors the proven
    // native-USB path in pico_shared's FrensHelpers.cpp
    // setClocksAndStartStdio() verbatim (CLK_SYS aux tap; do NOT switch to
    // the CLKSRC_PLL_SYS tap — the ecosystem has only ever shipped the
    // CLK_SYS form on these boards). Trade-off: PLL_SYS jitter rides on the
    // TMDS bit clock (possible sparkles on strict HDMI sinks); unavoidable
    // here since PLL_USB is spoken for.
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    378000000u,
                    126000000u);
#endif
    // set_sys_clock_khz parked clk_peri on PLL_USB@48 MHz (which the PIO-USB
    // build just retasked to 126) — put clk_peri on PLL_SYS at full speed on
    // both variants (helper-proven order; stdio_init_all below derives UART
    // dividers from whatever clk_peri reads then).
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    378000000u,
                    378000000u);
#endif
#if !USE_PICO_NET
    // debug ?
//    gpio_debug_pins_init();
#endif
#ifdef PICO_SMPS_MODE_PIN
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, 1);
#endif
#endif
#if LIB_PICO_STDIO
    stdio_init_all();
#endif
#if PICO_BUILD
    I_Init();
#endif
#if USE_PICO_NET
    // do init early to set pulls
    piconet_init();
#endif
//!
    // Print the program version and exit.
    //
    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts(PACKAGE_STRING);
        exit(0);
    }
#if defined(ADAFRUIT_FRUIT_JAM)
        printf("About to poll buttons %d %d\n", gpio_get(4), gpio_get(5));

  if (!gpio_get(4)) {
        NO_MUSIC = 1;
        NO_SFX = 1;
  } else if (!gpio_get(5)) {
        NO_MUSIC = 1;
    }
#endif

#if !NO_USE_ARGS
    M_FindResponseFile();
#endif

    #ifdef SDL_HINT_NO_SIGNAL_HANDLERS
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    #endif

    // start doom

    D_DoomMain ();

    return 0;
}

