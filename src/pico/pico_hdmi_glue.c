/*
 *  pico_hdmi_glue.c — Doom side of pico_shared's pico_hdmi driver.
 *
 *  We don't compile pico_shared/pico_hdmi/hstx.c because its static 154 KB
 *  FRAMEBUFFER is dead weight when we render directly out of Doom's 8bpp
 *  frame_buffer[] via a custom scanline callback. Instead this file:
 *   - orchestrates hstx_di_queue_init + video_output_init + core1 launch,
 *   - reproduces hstx_push_audio_sample verbatim,
 *   - owns the HP-detect state and internal-speaker mute policy.
 */

#include "pico_hdmi_glue.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "hstx_data_island_queue.h"
#include "hstx_packet.h"
#include "video_output.h"

#include "audio_i2s.h"       // pico_shared: audio_i2s_muteInternalSpeaker
#include "tlv320dac3100.h"   // pico_shared: tlv320_poll_headphone

#include <stdio.h>
#include <assert.h>

// Runtime audio-sink routing. Poll code in doom_poll_headphone() and the
// per-sample fanout in i_picosound.c read this. Default = HDMI so audio comes
// out of the TV until the user plugs headphones in.
volatile doom_audio_sink_t doom_audio_sink = DOOM_SINK_HDMI;

// core1 owns HSTX servicing; give it a dedicated stack rather than sharing
// pico-sdk's default core1 stack region (which is fixed at 4 KB, same as the
// pico_shared/pico_hdmi upstream default).
static uint32_t doom_hdmi_core1_stack[1024] __attribute__((aligned(8)));

void doom_hdmi_init(video_output_scanline_cb_t scanline_cb,
                    video_output_vsync_cb_t vsync_cb,
                    video_output_task_fn bg_task,
                    uint32_t audio_sample_rate)
{
    // pico_hdmi is built with NOHSTXCLOCK, so its video_output_init() doesn't
    // touch clk_hstx — and neither do we anymore. clk_hstx is configured once
    // in i_main.c: a fixed 126 MHz from a retasked PLL_USB, deliberately
    // DECOUPLED from PLL_SYS (at 378 MHz, PLL_SYS jitter on the TMDS bit
    // clock shows as dots/sparkles on strict HDMI sinks — see the FrensHelpers
    // port in i_main.c). Reconfiguring from clk_sys here would put that
    // jitter back on the link. The 25.2 MHz pixel clock (CSR_CLKDIV=5) and
    // pico_hdmi's DI_LINE_RATE_HZ / ACR N=6144/CTS=25200 assumptions are
    // unchanged.
    assert(clock_get_hz(clk_hstx) == 126000000u);

    // HDMI mode (not DVI-only) so data-island audio packets are emitted.
    video_output_set_dvi_mode(false);
    hstx_di_queue_init();
    if (vsync_cb) {
        video_output_set_vsync_callback(vsync_cb);
    }
    if (bg_task) {
        video_output_set_background_task(bg_task);
    }
    video_output_init(640, 480);
    pico_hdmi_set_audio_sample_rate(audio_sample_rate);
    if (scanline_cb) {
        video_output_set_scanline_callback(scanline_cb);
    }
    multicore_launch_core1_with_stack(video_output_core1_run,
                                      doom_hdmi_core1_stack,
                                      sizeof(doom_hdmi_core1_stack));
    printf("Doom HSTX video + HDMI audio initialised (clk_hstx=%lu Hz).\n",
           (unsigned long)clock_get_hz(clk_hstx));
}

// Verbatim from pico_shared/pico_hdmi/hstx.c:272-296. Accumulates 4 samples
// per HDMI audio data-island, encodes with hstx_packet_set_audio_samples,
// pushes into the DI ring. Drops on high-watermark so we never block the
// audio mixer running on core0.
#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 200
#endif

void __not_in_flash_func(hstx_push_audio_sample)(int left, int right)
{
    static int g_hdmi_audio_frame_counter = 0;
    static audio_sample_t acc_buf[4];
    static int acc_count = 0;
    acc_buf[acc_count].left = (int16_t)left;
    acc_buf[acc_count].right = (int16_t)right;
    acc_count++;
    if (acc_count == 4) {
        if (hstx_di_queue_get_level() >= HSTX_AUDIO_DI_HIGH_WATERMARK) {
            acc_count = 0;
            return;
        }
        hstx_packet_t packet;
        // Use the _cs variant: writes IEC 60958 channel status bits with a
        // valid sample-frequency code. Without this, strict HDMI receivers
        // (many computer monitors) refuse to unmute the audio stream even
        // though Audio InfoFrame carries the format info.
        g_hdmi_audio_frame_counter =
            hstx_packet_set_audio_samples_cs(&packet, acc_buf, 4, g_hdmi_audio_frame_counter);

        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, true);
        (void)hstx_di_queue_push(&island);
        acc_count = 0;
    }
}

void doom_poll_headphone(void)
{
    enum headphone_toggle_t ev = tlv320_poll_headphone();
    if (ev == HP_TOGGLE_CONNECT) {
        doom_audio_sink = DOOM_SINK_HEADPHONES;
        // Speaker is already muted by tlv320_poll_headphone's default handling.
    } else if (ev == HP_TOGGLE_DISCONNECT) {
        doom_audio_sink = DOOM_SINK_HDMI;
        // TLV320's default is to unmute the speaker on HP removal. Override:
        // per project spec, when headphones are unplugged audio routes to HDMI
        // only — the internal speaker must never play.
        audio_i2s_muteInternalSpeaker(true);
    }
}
