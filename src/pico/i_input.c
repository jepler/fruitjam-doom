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
//     SDL implementation of system-specific input interface.
//


//#include "SDL.h"
//#include "SDL_keycode.h"
#include <doom/sounds.h>
#include <doom/s_sound.h>
#include "pico.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "d_event.h"
#include "i_input.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "hardware/uart.h"
#include <stdlib.h>
#if USB_SUPPORT
#include "pico/binary_info.h"
#include "tusb.h"
#include "hardware/irq.h"
bi_decl(bi_program_feature("USB keyboard support"));
// implemented in i_usbhid.cpp
extern void pico_usb_hid_poll(void);
#endif

static const int scancode_translate_table[] = SCANCODE_TO_KEYS_ARRAY;

// Lookup table for mapping ASCII characters to their equivalent when
// shift is pressed on a US layout keyboard. This is the original table
// as found in the Doom sources, comments and all.
static const char shiftxform[] =
        {
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                31, ' ', '!', '"', '#', '$', '%', '&',
                '"', // shift-'
                '(', ')', '*', '+',
                '<', // shift-,
                '_', // shift--
                '>', // shift-.
                '?', // shift-/
                ')', // shift-0
                '!', // shift-1
                '@', // shift-2
                '#', // shift-3
                '$', // shift-4
                '%', // shift-5
                '^', // shift-6
                '&', // shift-7
                '*', // shift-8
                '(', // shift-9
                ':',
                ':', // shift-;
                '<',
                '+', // shift-=
                '>', '?', '@',
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                '[', // shift-[
                '!', // shift-backslash - OH MY GOD DOES WATCOM SUCK
                ']', // shift-]
                '"', '_',
                '\'', // shift-`
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                '{', '|', '}', '~', 127
        };

// If true, I_StartTextInput() has been called, and we are populating
// the data3 field of ev_keydown events.
static boolean text_input_enabled = true;

// Disallow mouse and joystick movement to cause forward/backward
// motion.  Specified with the '-novert' command line parameter.
// This is an int to allow saving to config file
int novert = 0;

// If true, keyboard mapping is ignored, like in Vanilla Doom.
// The sensible thing to do is to disable this if you have a non-US
// keyboard.

#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
int vanilla_keyboard_mapping = true;
#endif

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.
#if !NO_USE_MOUSE
float mouse_acceleration = 2.0;
int mouse_threshold = 10;
#endif

enum {
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226, /**< alt, option */
    SDL_SCANCODE_LGUI = 227, /**< windows, command (apple), meta */
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_RALT = 230, /**< alt gr, option */
    SDL_SCANCODE_RGUI = 231, /**< windows, command (apple), meta */
};

// Translates the SDL key to a value of the type found in doomkeys.h
int TranslateKey(int scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return KEY_RCTRL;

        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            return KEY_RSHIFT;

        case SDL_SCANCODE_LALT:
            return KEY_LALT;

        case SDL_SCANCODE_RALT:
            return KEY_RALT;

        default:
            if (scancode >= 0 && scancode < arrlen(scancode_translate_table))
            {
                return scancode_translate_table[scancode];
            }
            else
            {
                return 0;
            }
    }
}

// Get the localized version of the key press. This takes into account the
// keyboard layout, but does not apply any changes due to modifiers, (eg.
// shift-, alt-, etc.)
static int GetLocalizedKey(int scancode)
{
    // When using Vanilla mapping, we just base everything off the scancode
    // and always pretend the user is using a US layout keyboard.
    if (vanilla_keyboard_mapping)
    {
        return TranslateKey(scancode);
    }
    else
    {
        assert(false); return 0;
//        int result = sym->sym;
//
//        if (result < 0 || result >= 128)
//        {
//            result = 0;
//        }
//
//        return sym_<result;
    }
}

// Get the equivalent ASCII (Unicode?) character for a keypress.
int GetTypedChar(int scancode, boolean shiftdown)
{
    // We only return typed characters when entering text, after
    // I_StartTextInput() has been called. Otherwise we return nothing.
    if (!text_input_enabled)
    {
        return 0;
    }

    // If we're strictly emulating Vanilla, we should always act like
    // we're using a US layout keyboard (in ev_keydown, data1=data2).
    // Otherwise we should use the native key mapping.
    if (vanilla_keyboard_mapping)
    {
        int result = TranslateKey(scancode);

        // If shift is held down, apply the original uppercase
        // translation table used under DOS.
        if (shiftdown
            && result >= 0 && result < arrlen(shiftxform))
        {
            result = shiftxform[result];
        }

        return result;
    }
    else
    {
#if 0
        SDL_Event next_event;

        // Special cases, where we always return a fixed value.
        switch (sym->sym)
        {
            case SDLK_BACKSPACE: return KEY_BACKSPACE;
            case SDLK_RETURN:    return KEY_ENTER;
            default:
                break;
        }

        // The following is a gross hack, but I don't see an easier way
        // of doing this within the SDL2 API (in SDL1 it was easier).
        // We want to get the fully transformed input character associated
        // with this keypress - correct keyboard layout, appropriately
        // transformed by any modifier keys, etc. So peek ahead in the SDL
        // event queue and see if the key press is immediately followed by
        // an SDL_TEXTINPUT event. If it is, it's reasonable to assume the
        // key press and the text input are connected. Technically the SDL
        // API does not guarantee anything of the sort, but in practice this
        // is what happens and I've verified it through manual inspect of
        // the SDL source code.
        //
        // In an ideal world we'd split out ev_keydown into a separate
        // ev_textinput event, as SDL2 has done. But this doesn't work
        // (I experimented with the idea), because lots of Doom's code is
        // based around different responders "eating" events to stop them
        // being passed on to another responder. If code is listening for
        // a text input, it cannot block the corresponding keydown events
        // which can affect other responders.
        //
        // So we're stuck with this as a rather fragile alternative.

        if (SDL_PeepEvents(&next_event, 1, SDL_PEEKEVENT,
                           SDL_FIRSTEVENT, SDL_LASTEVENT) == 1
            && next_event.type == SDL_TEXTINPUT)
        {
            // If an SDL_TEXTINPUT event is found, we always assume it
            // matches the key press. The input text must be a single
            // ASCII character - if it isn't, it's possible the input
            // char is a Unicode value instead; better to send a null
            // character than the unshifted key.
            if (strlen(next_event.text.text) == 1
                && (next_event.text.text[0] & 0x80) == 0)
            {
                return next_event.text.text[0];
            }
        }
#else
        assert(false);
#endif

        // Failed to find anything :/
        return 0;
    }
}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    text_input_enabled = true;

    if (!vanilla_keyboard_mapping)
    {
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
        // SDL2-TODO: SDL_SetTextInputRect(...);
        SDL_StartTextInput();
#endif
    }
}

void I_StopTextInput(void)
{
    text_input_enabled = false;

    if (!vanilla_keyboard_mapping)
    {
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
        SDL_StopTextInput();
#endif
    }
}

#if !NO_USE_MOUSE

static void MapMouseWheelToButtons(int mouse_button_state, int delta)
{
    // SDL2 distinguishes button events from mouse wheel events.
    // We want to treat the mouse wheel as two buttons, as per
    // SDL1
    static event_t up, down;
    int button;

    if (!delta) return;
    if (delta <= 0)
    {   // scroll down
        button = 4;
    }
    else
    {   // scroll up
        button = 3;
    }

    // post a button down event
    mouse_button_state |= (1 << button);
    down.type = ev_mouse;
    down.data1 = mouse_button_state;
    down.data2 = down.data3 = 0;
    D_PostEvent(&down);

    // post a button up event
    mouse_button_state &= ~(1 << button);
    up.type = ev_mouse;
    up.data1 = mouse_button_state;
    up.data2 = up.data3 = 0;
    D_PostEvent(&up);
}

static int AccelerateMouse(int val)
{
    if (val < 0)
        return -AccelerateMouse(-val);

    if (val > mouse_threshold)
    {
        return (int)((val - mouse_threshold) * mouse_acceleration + mouse_threshold);
    }
    else
    {
        return val;
    }
}
#endif

// Bind all variables controlling input options.
void I_BindInputVariables(void)
{
#if !NO_USE_MOUSE
    M_BindFloatVariable("mouse_acceleration",      &mouse_acceleration);
    M_BindIntVariable("mouse_threshold",           &mouse_threshold);
#endif
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
    M_BindIntVariable("vanilla_keyboard_mapping",  &vanilla_keyboard_mapping);
#endif
    M_BindIntVariable("novert",                    &novert);
}

#define WITH_SHIFT 0x8000

static void pico_key_down(int scancode, int keysym, int modifiers) {
    event_t event;
    event.type = ev_keydown;
    event.data1 = TranslateKey(scancode);
    event.data2 = GetLocalizedKey(scancode);
    event.data3 = GetTypedChar(scancode, modifiers & WITH_SHIFT ? 1 : 0);

    if (at_exit_screen) {
        handle_exit_key_down(scancode, modifiers & WITH_SHIFT ? 1 : 0, exit_screen_kb_buffer_80, 80);
        return;
    }
    if (event.data1 != 0)
    {
        D_PostEvent(&event);
    }
}

static void pico_key_up(int scancode, int keysym, int modifiers) {
    event_t event;
    event.type = ev_keyup;
    event.data1 = TranslateKey(scancode);
    // data2/data3 are initialized to zero for ev_keyup.
    // For ev_keydown it's the shifted Unicode character
    // that was typed, but if something wants to detect
    // key releases it should do so based on data1
    // (key ID), not the printable char.
    event.data2 = 0;
    event.data3 = 0;
    if (event.data1 != 0)
    {
        D_PostEvent(&event);
    }
}

#if PICO_NO_HARDWARE
static void pico_quit(void) {
    exit(0);
}
#endif

void I_InputInit(void) {
#if PICO_NO_HARDWARE
    platform_key_down = pico_key_down;
    platform_key_up = pico_key_up;
    platform_quit = pico_quit;
#elif USB_SUPPORT
    irq_set_priority(USBCTRL_IRQ, 0xc0);
#endif
}

void I_GetEvent() {
#if USB_SUPPORT
    tuh_task();
    pico_usb_hid_poll();
#endif
    return I_GetEventTimeout(50);
}

void I_GetEventTimeout(int key_timeout) {
#if PICO_ON_DEVICE && !NO_USE_UART
    if (uart_is_readable(uart_default)) {
        char c = uart_getc(uart_default);
        if (c == 26 && uart_is_readable_within_us(uart_default, key_timeout)) {
            c = uart_getc(uart_default);
            static int modifiers = 0;
            switch (c) {
                case 0:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint scancode = (uint8_t) uart_getc(uart_default);
                        if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT) {
                            modifiers |= WITH_SHIFT;
                        }
                        pico_key_down(scancode, 0, modifiers);
                    }
                    return;
                case 1:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint scancode = (uint8_t) uart_getc(uart_default);
                        if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT) {
                            modifiers &= ~WITH_SHIFT;
                        }
                        pico_key_up(scancode, 0, modifiers);
                    }
                    return;
                case 2:
                case 3:
                case 5:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    return;
                case 4:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    return;
            }
        }
    }
#endif
}

#if USB_SUPPORT

// The TinyUSB HID host callbacks live in the vendored pico_shared usb_hid
// driver (3rdparty/pico_shared_drivers/usb_hid/hid_app.cpp), which supports
// keyboards, mice and a range of USB gamepads (DualShock 4/5, PSClassic,
// Genesis Mini, MantaPad NES/SNES clones, XInput/Xbox, ...). i_usbhid.cpp
// polls the state it maintains and reports back through the functions below.

void pico_usb_key_down(int scancode, int shift) {
    pico_key_down(scancode, 0, shift ? WITH_SHIFT : 0);
}

void pico_usb_key_up(int scancode, int shift) {
    pico_key_up(scancode, 0, shift ? WITH_SHIFT : 0);
}

void pico_usb_post_joystick(int buttons, int xmove, int ymove, int strafemove) {
    static event_t ev;

    ev.type = ev_joystick;
    ev.data1 = buttons;
    ev.data2 = xmove;
    ev.data3 = ymove;
    ev.data4 = strafemove;
    D_PostEvent(&ev);
}

#if !NO_USE_MOUSE
void pico_usb_post_mouse(int buttons, int dx, int dy, int wheel) {
    static event_t ev;

    ev.type = ev_mouse;
    ev.data1 = buttons;
    ev.data2 = AccelerateMouse(dx);
    ev.data3 = AccelerateMouse(-dy);
    D_PostEvent(&ev);

    MapMouseWheelToButtons(buttons, wheel);
}
#endif

#endif
