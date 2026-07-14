//
// Copyright(C) 2026 Frank Hoedemakers
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
//     Bridge between the pico_shared USB HID host driver (hid_app.cpp,
//     vendored under 3rdparty/pico_shared_drivers/usb_hid) and Doom's
//     event queue. hid_app.cpp owns the TinyUSB host callbacks and keeps
//     the latest keyboard / mouse / gamepad state; pico_usb_hid_poll()
//     is called from I_GetEvent() right after tuh_task() and translates
//     state changes into Doom events via the pico_usb_* functions
//     exported by i_input.c.
//

#if USB_SUPPORT

#include "tusb.h"
#include "gamepad.h"

// Legacy NES/SNES pads on PIO (pico_shared nespad, vendored under
// 3rdparty/pico_shared_drivers/nespad). Pin macros come from the board's
// force-included cflags header; -1 (or absent) disables a port. Their state
// is merged into the same joystick event as the USB pads in pollGamePads().
#if defined(NES_PIN_CLK) && NES_PIN_CLK != -1
#define DOOM_NESPAD 1
#include "nespad.h"
#include "hardware/clocks.h"
#include "pico/time.h"
#include <stdio.h>
#else
#define DOOM_NESPAD 0
#endif
#if DOOM_NESPAD && defined(NES_PIN_CLK_1) && NES_PIN_CLK_1 != -1
#define DOOM_NESPAD_1 1
#else
#define DOOM_NESPAD_1 0
#endif

extern "C" {
void pico_usb_key_down(int scancode, int shift);
void pico_usb_key_up(int scancode, int shift);
void pico_usb_post_joystick(int buttons, int xmove, int ymove, int strafemove);
#if !NO_USE_MOUSE
void pico_usb_post_mouse(int buttons, int dx, int dy, int wheel);
#endif
void pico_usb_hid_poll(void);
}

namespace
{
    bool findKey(const io::KeyboardState &st, uint8_t code)
    {
        for (int i = 0; i < 6; i++)
        {
            if (st.keycode[i] == code)
                return true;
        }
        return false;
    }

    // Diff the current boot-keyboard state against the previously seen one
    // and synthesize key down/up events. HID usage codes double as SDL
    // scancodes, which is what TranslateKey() in i_input.c expects.
    void pollKeyboard()
    {
        static io::KeyboardState prev;
        const io::KeyboardState cur = io::getCurrentKeyboardState();
        const int shift = (cur.modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                                           KEYBOARD_MODIFIER_RIGHTSHIFT)) ? 1 : 0;

        for (int i = 0; i < 6; i++)
        {
            uint8_t code = cur.keycode[i];
            if (code && !findKey(prev, code))
                pico_usb_key_down(code, shift);
            code = prev.keycode[i];
            if (code && !findKey(cur, code))
                pico_usb_key_up(code, shift);
        }

        // Modifier bit n corresponds to HID usage 0xE0 + n
        // (LeftControl, LeftShift, LeftAlt, LeftGUI, Right...).
        const uint8_t changed = cur.modifier ^ prev.modifier;
        for (int i = 0; i < 8; i++)
        {
            const uint8_t mask = 1u << i;
            if (changed & mask)
            {
                if (cur.modifier & mask)
                    pico_usb_key_down(0xE0 + i, shift);
                else
                    pico_usb_key_up(0xE0 + i, shift);
            }
        }
        prev = cur;
    }

#if DOOM_NESPAD
    // Lazy-init on first poll, then harvest the PIO read started on the
    // previous poll and immediately kick off the next one (~200 µs per
    // transfer, one poll cycle of input latency — negligible).
    //
    // Two hard-won robustness rules, learned on the adafruitdvisd bring-up:
    //  - After nespad_begin() the SM runs at a 1 MHz PIO clock, so its
    //    first instruction — "irq wait 0", set-flag-then-park — lands ~1 µs
    //    after enable. The 378 MHz core reaches nespad_read_start() first,
    //    the clear outruns the SM's set, and the release is lost: the SM
    //    parks forever and the next finish blocks for good. Wait out the
    //    race before the first start.
    //  - Never call nespad_read_finish() (blocking FIFO reads) unless
    //    nespad_read_ready() says data is waiting — a controller port must
    //    not be able to hang the game loop.
    uint16_t nesButtons()
    {
        static bool inited = false, dead = false;
        static uint16_t last = 0;
        static uint64_t not_ready_since = 0;
        if (dead)
            return 0;
        if (!inited)
        {
            inited = true;
            const uint32_t cpu_khz = clock_get_hz(clk_sys) / 1000;
            bool ok = nespad_begin(0, cpu_khz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT, NES_PIO);
#if DOOM_NESPAD_1
            ok = nespad_begin(1, cpu_khz, NES_PIN_CLK_1, NES_PIN_DATA_1, NES_PIN_LAT_1, NES_PIO_1) && ok;
#endif
            if (!ok)
            {
                printf("!!! nespad init failed - NES/SNES pads disabled\n");
                dead = true;
                return 0;
            }
            // Let both SMs (1 MHz PIO clock) reach their irq-wait park
            // before the first release — see race note above.
            busy_wait_us(100);
            nespad_read_start();
            return 0;
        }
        if (!nespad_read_ready())
        {
            // Reads complete in ~200 µs; polls are further apart than that.
            // Transiently not-ready: keep the previous state. Never-ready
            // means a dead state machine — give up loudly after a second.
            const uint64_t now = time_us_64();
            if (not_ready_since == 0)
                not_ready_since = now;
            else if (now - not_ready_since > 1000000)
            {
                printf("!!! nespad read never completed - NES/SNES pads disabled\n");
                dead = true;
            }
            return last;
        }
        not_ready_since = 0;
        nespad_read_finish();
        last = nespad_states_ext[0] | nespad_states_ext[1];
        nespad_read_start();
        return last;
    }
#endif

    // Merge all connected pads into one joystick event whose button bit
    // layout matches the joyb* defaults in m_controls.c:
    // 0 fire, 1 strafe, 2 speed/run, 3 use, 4 prev weapon, 5 next weapon.
    void pollGamePads()
    {
        using Button = io::GamePadState::Button;
        int buttons = 0, xmove = 0, ymove = 0, strafemove = 0;

        for (int p = 0; p < 2; p++)
        {
            const io::GamePadState &gp = io::getCurrentGamePadState(p);
            if (!gp.isConnected())
                continue;
            if (gp.buttons & Button::X) buttons |= 1 << 0;
            if (gp.buttons & Button::A) buttons |= 1 << 1;
            if (gp.buttons & Button::B) buttons |= 1 << 2;
            if (gp.buttons & Button::Y) buttons |= 1 << 3;
            if (gp.buttons & Button::SELECT) buttons |= 1 << 4;
            if (gp.buttons & Button::START) buttons |= 1 << 5;
            if (gp.buttons & Button::LEFT) xmove = -127;
            else if (gp.buttons & Button::RIGHT) xmove = 127;
            if (gp.buttons & Button::UP) ymove = -127;
            else if (gp.buttons & Button::DOWN) ymove = 127;
            if (gp.buttons & Button::L) strafemove = -127;
            else if (gp.buttons & Button::R) strafemove = 127;
        }

#if DOOM_NESPAD
        // nespad_states_ext is in SNES serial order: bit0=B, 1=Y, 2=Select,
        // 3=Start, 4=Up, 5=Down, 6=Left, 7=Right, 8=A, 9=X, 10=L, 11=R.
        // Plain NES pads only populate bits 0-7 (as A,B,Select,Start,dpad),
        // so a NES pad gets A=fire / B=run. Mapping mirrors the USB block
        // above: primary=fire, secondary=run, SNES A/X=strafe/use.
        const uint16_t nes = nesButtons();
        if (nes & 0x0001) buttons |= 1 << 0;      // SNES B / NES A -> fire
        if (nes & 0x0100) buttons |= 1 << 1;      // SNES A         -> strafe
        if (nes & 0x0002) buttons |= 1 << 2;      // SNES Y / NES B -> speed/run
        if (nes & 0x0200) buttons |= 1 << 3;      // SNES X         -> use
        if (nes & 0x0004) buttons |= 1 << 4;      // Select         -> prev weapon
        if (nes & 0x0008) buttons |= 1 << 5;      // Start          -> next weapon
        if (nes & 0x0040) xmove = -127;           // Left
        else if (nes & 0x0080) xmove = 127;       // Right
        if (nes & 0x0010) ymove = -127;           // Up
        else if (nes & 0x0020) ymove = 127;       // Down
        if (nes & 0x0400) strafemove = -127;      // SNES L
        else if (nes & 0x0800) strafemove = 127;  // SNES R
#endif

        static int pb, px, py, ps;
        if (buttons != pb || xmove != px || ymove != py || strafemove != ps)
        {
            pico_usb_post_joystick(buttons, xmove, ymove, strafemove);
            pb = buttons; px = xmove; py = ymove; ps = strafemove;
        }
    }

#if !NO_USE_MOUSE
    void pollMouse()
    {
        io::MouseState &m = io::getCurrentMouseState();
        static uint8_t prevButtons;

        const int dx = m.dx, dy = m.dy, wheel = m.wheel;
        m.dx = m.dy = m.wheel = 0;
        if (dx || dy || wheel || m.buttons != prevButtons)
        {
            pico_usb_post_mouse(m.buttons, dx, dy, wheel);
            prevButtons = m.buttons;
        }
    }
#endif
}

void pico_usb_hid_poll(void)
{
    pollKeyboard();
    pollGamePads();
#if !NO_USE_MOUSE
    pollMouse();
#endif
}

#endif // USB_SUPPORT
