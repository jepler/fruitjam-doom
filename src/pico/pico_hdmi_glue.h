/*
 *  pico_hdmi_glue.h
 *
 *  Doom-specific glue over pico_shared's pico_hdmi driver. We bypass the
 *  driver's own 154 KB static FRAMEBUFFER and install our own scanline
 *  callback (defined in i_video.c) that reads Doom's 8bpp frame_buffer[]
 *  directly.
 *
 *  This module also owns the runtime audio-sink routing (SINK_HDMI when
 *  headphones are unplugged, SINK_HEADPHONES when plugged) and exposes the
 *  hstx_push_audio_sample() function normally provided by pico_hdmi/hstx.c
 *  (which we don't compile — its FRAMEBUFFER would waste RAM).
 */

#ifndef PICO_HDMI_GLUE_H
#define PICO_HDMI_GLUE_H 1

#include <stdbool.h>
#include <stdint.h>

#include "video_output.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOOM_SINK_HDMI = 0,
    DOOM_SINK_HEADPHONES = 1,
} doom_audio_sink_t;

extern volatile doom_audio_sink_t doom_audio_sink;

// Bring up HSTX video + HDMI audio data-island stream. Registers
// `scanline_cb` and `vsync_cb` (both invoked from DMA_IRQ_0 on core1) plus
// `bg_task` (invoked from core1's main loop between DMA IRQs — safe to do
// heavy per-frame work like palette conversion here). Sets the HDMI audio
// sample rate then launches core1 running the HSTX servicing loop. Call
// once from I_InitGraphics on core0.
void doom_hdmi_init(video_output_scanline_cb_t scanline_cb,
                    video_output_vsync_cb_t vsync_cb,
                    video_output_task_fn bg_task,
                    uint32_t audio_sample_rate);

// Push one stereo sample to the HDMI data-island audio stream. Accumulates
// four samples then encodes+queues one packet; drops on high watermark.
// Lifted verbatim from pico_shared/pico_hdmi/hstx.c (which we don't compile).
void hstx_push_audio_sample(int left, int right);

// Poll the TLV320 headphone-detect state. Call once per frame from core0.
// Flips doom_audio_sink on plug / unplug and re-asserts speaker mute so the
// internal speaker never plays.
void doom_poll_headphone(void);

#ifdef __cplusplus
}
#endif

#endif
