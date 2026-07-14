//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2008 David Flater
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
//	System interface for sound.
//

#include "config.h"

#include <string.h>
#include <assert.h>
#include <doom/sounds.h>
#include <z_zone.h>

#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomtype.h"
#include "i_picosound.h"
// pico/audio.h from pico-extras is header-only; we keep it for the
// audio_buffer_t type shape that opl_pico.c expects. We no longer link the
// pico-extras I2S driver — audio is pushed via pico_shared's audio_i2s and
// pico_hdmi_glue's HDMI data-island path.
#include "pico/audio.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"

#include "audio_i2s.h"                 // pico_shared: audio_i2s_setup / enqueue / mute
#include "tlv320dac3100.h"             // enum headphone_toggle_t
#include "pico_hdmi_glue.h"            // hstx_push_audio_sample, doom_audio_sink, doom_poll_headphone
#include "hstx_data_island_queue.h"    // hstx_di_queue_get_level

// Compile-time defs from 3rdparty/pico_shared_drivers/drivers/pico_hdmi/CMakeLists.txt.
// Fall back to upstream defaults if this file is compiled without them.
#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 200
#endif

// Which audio_i2s driver this board carries — set in the board's force-included
// <tag>_cflags.h as a numeric literal (1 = TLV320, 2 = PCM5000A, values from
// audio_i2s.h). Defaults to the TLV320 (Fruit Jam) when a header predates the
// multi-board split.
#ifndef DOOM_AUDIO_I2S_DRIVER
#define DOOM_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_TLV320
#endif

// Headphone-detect is only available on the TLV320 with its INT pin wired
// (Fruit Jam). Boards without it (PCM5100A boards, Feather's TLV320 breakout)
// can't switch sinks at runtime, so they drive HDMI and I2S simultaneously.
#if DOOM_AUDIO_I2S_DRIVER == PICO_AUDIO_I2S_DRIVER_TLV320 && PICO_AUDIO_I2S_INTERRUPT_PIN != -1
#define DOOM_AUDIO_EXCLUSIVE_SINK 1
#else
#define DOOM_AUDIO_EXCLUSIVE_SINK 0
#endif

#define ADPCM_BLOCK_SIZE 128
#define ADPCM_SAMPLES_PER_BLOCK_SIZE 249
#define LOW_PASS_FILTER
#define MIX_MAX_VOLUME 128
typedef struct channel_s channel_t;

static volatile enum {
    FS_NONE,
    FS_FADE_OUT,
    FS_FADE_IN,
    FS_SILENT,
} fade_state;
#define FADE_STEP 8 // must be power of 2
uint16_t fade_level;

struct channel_s
{
    const uint8_t *data;
    const uint8_t *data_end;
    uint32_t offset;
    uint32_t step;
    uint8_t left, right; // 0-255
    uint8_t decompressed_size;
#if SOUND_LOW_PASS
    uint8_t alpha256;
#endif
    int8_t decompressed[ADPCM_SAMPLES_PER_BLOCK_SIZE];
};

// Doom hands the music generator a chunk-sized audio_buffer_t; we mix SFX
// into the same buffer, then push samples to whichever sinks the routing
// state currently selects (see doom_audio_sink in pico_hdmi_glue).
//
// Each I_Pico_UpdateSound() call produces at most MIX_CHUNK_SAMPLES stereo
// samples → MIX_CHUNK_SAMPLES/4 HDMI DI packets (64) per burst, ~5.3 ms of
// audio per call at 48 kHz. Must stay comfortably under both sinks' buffer
// depths: I2S ring 1024 samples, DI ring HSTX_AUDIO_DI_HIGH_WATERMARK=224
// packets (the DI gate below skips the mix cycle when a burst wouldn't fit).
// pd_end_frame() spins on I_UpdateSound() while waiting for
// display_frame_freed, so many calls per frame keep both sinks fed.
#define MIX_CHUNK_SAMPLES 256

static struct audio_format audio_format = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .sample_freq = PICO_SOUND_SAMPLE_FREQ,
        .channel_count = 2,
};

static struct audio_buffer_format producer_format = {
        .format = &audio_format,
        .sample_stride = 4
};

static int16_t mix_samples[MIX_CHUNK_SAMPLES * 2];  // stereo interleaved
static mem_buffer_t mix_mem_buffer = {
        .size = sizeof(mix_samples),
        .bytes = (uint8_t *)mix_samples,
        .flags = 0,
};
static audio_buffer_t mix_audio_buffer = {
        .buffer = &mix_mem_buffer,
        .format = &producer_format,
        .sample_count = 0,
        .max_sample_count = MIX_CHUNK_SAMPLES,
        .user_data = 0,
        .next = NULL,
};

// ====== FROM ADPCM-LIB =====
#define CLIP(data, min, max) \
if ((data) > (max)) data = max; \
else if ((data) < (min)) data = min;

/* step table */
static const uint16_t step_table[89] = {
        7, 8, 9, 10, 11, 12, 13, 14,
        16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
        3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767
};

/* step index tables */
static const int index_table[] = {
        /* adpcm data size is 4 */
        -1, -1, -1, -1, 2, 4, 6, 8
};
// =============================

static void (*music_generator)(audio_buffer_t *buffer);

static boolean sound_initialized = false;
static channel_t channels[NUM_SOUND_CHANNELS];

static boolean use_sfx_prefix;

static inline bool is_channel_playing(int channel) {
    return channels[channel].decompressed_size != 0;
}

static inline void stop_channel(int channel) {
    channels[channel].decompressed_size = 0;
}

static bool check_and_init_channel(int channel) {
    return sound_initialized && ((uint)channel) < NUM_SOUND_CHANNELS;
}

int adpcm_decode_block_s8(int8_t *outbuf, const uint8_t *inbuf, int inbufsize)
{
#if 1
    int samples = 1, chunks;

    if (inbufsize < 4)
        return 0;

    int32_t pcmdata = (int16_t) (inbuf [0] | (inbuf [1] << 8));
    *outbuf++ = pcmdata>>8u;
    int index = inbuf[2];

    if (index < 0 || index > 88 || inbuf [3])     // sanitize the input a little...
        return 0;

    inbufsize -= 4;
    inbuf += 4;

    chunks = inbufsize / 4;
    samples += chunks * 8;

    while (chunks--) {
        for (int i = 0; i < 4; ++i) {
            int step = step_table[index], delta = step >> 3;

            if (*inbuf & 1) delta += (step >> 2);
            if (*inbuf & 2) delta += (step >> 1);
            if (*inbuf & 4) delta += step;
            if (*inbuf & 8) delta = -delta;

            pcmdata += delta;
            index += index_table [*inbuf & 0x7];
            CLIP(index, 0, 88);
            CLIP(pcmdata, -32768, 32767);
            outbuf [i * 2] = pcmdata>>8u;

            step = step_table[index], delta = step >> 3;

            if (*inbuf & 0x10) delta += (step >> 2);
            if (*inbuf & 0x20) delta += (step >> 1);
            if (*inbuf & 0x40) delta += step;
            if (*inbuf & 0x80) delta = -delta;

            pcmdata += delta;
            index += index_table[(*inbuf >> 4) & 0x7];
            CLIP(index, 0, 88);
            CLIP(pcmdata, -32768, 32767);
            outbuf [i * 2 + 1] = pcmdata>>8u;
            inbuf++;
        }

        outbuf += 8;
    }

    return samples;
#else
    extern int adpcm_decode_block (int16_t *outbuf, const uint8_t *inbuf, size_t inbufsize, int channels);
    static int16_t tmp[ADPCM_SAMPLES_PER_BLOCK_SIZE];
    int samples = adpcm_decode_block(tmp, inbuf, inbufsize, 1);
    for(int s=0;s<samples;s++) {
        outbuf[s] = tmp[s] / 256;
    }
    return samples;
#endif
}

static void decompress_buffer(channel_t *channel) {
    if (channel->data == channel->data_end) {
        channel->decompressed_size = 0;
    } else {
        int block_size = MIN(ADPCM_BLOCK_SIZE, channel->data_end - channel->data);
        channel->decompressed_size = adpcm_decode_block_s8(channel->decompressed, channel->data, block_size);
        assert(channel->decompressed_size && channel->decompressed_size <= sizeof(channel->decompressed));
        channel->data += block_size;
    }
}

static boolean init_channel_for_sfx(channel_t *ch, const sfxinfo_t *sfxinfo, int pitch)
{
    int lumpnum = sfx_mut(sfxinfo)->lumpnum;
    int lumplen = W_LumpLength(lumpnum);

    const uint8_t *data = W_CacheLumpNum(lumpnum, PU_STATIC); // we don't track because we assume in ROWAD anyway

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x80) // note 0x80 i.e. only support compressed right now
    {
        return false;
    }

    // 16 bit sample rate field, 32 bit length field

//    int length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
//    length -= 40; // 8 for header, 32 because we didn't updated it in lump converter (which cuts of unused 16 bit leading/leadout)
//    if (length <= 0) {
//        return false;
//    }
    int length = lumplen - 8;
//    printf("channel %d lump %d size %d at %p len2 %d\n", (int)(ch-channels), lumpnum, lumplen, data, length);

    ch->data = data + 8;
    ch->data_end = ch->data + length;

    uint32_t sample_freq = (data[3] << 8) | data[2];
    if (pitch == NORM_PITCH)
        ch->step = sample_freq * 65536 / PICO_SOUND_SAMPLE_FREQ;
    else
        ch->step = (uint32_t)((sample_freq * pitch) * 65536ull / (PICO_SOUND_SAMPLE_FREQ * pitch));

    decompress_buffer(ch); // we need non-zero decompressed size if playing
    ch->offset = 0;

#if SOUND_LOW_PASS
//    const float dt = 1.0f / PICO_SOUND_SAMPLE_FREQ;
//    const float rc = 1.0f / (3.14f * sample_freq);
//    const float alpha = dt / (rc + dt);
//    ch->alpha256 = (int)(256*alpha);
    ch->alpha256 = 256u * 201u * sample_freq / (201u * sample_freq + 64u * (uint)PICO_SOUND_SAMPLE_FREQ);
#endif
    return true;
}

static void GetSfxLumpName(const sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sfx lumps? Get the lump number for the sound linked to.
    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    // Doom adds a DS* prefix to sound lumps; Heretic and Hexen don't
    // do this.

    if (use_sfx_prefix)
    {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

static void I_Pico_PrecacheSounds(should_be_const sfxinfo_t *sounds, int num_sounds)
{
    // no-op
}

static int I_Pico_GetSfxLumpNum(should_be_const sfxinfo_t *sfx)
{
    char namebuf[9];
    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void I_Pico_UpdateSoundParams(int handle, int vol, int sep)
{
    int left, right;

    if (!sound_initialized || handle < 0 || handle >= NUM_SOUND_CHANNELS)
    {
        return;
    }

    // todo graham seems unnecessary
    left = ((254 - sep) * vol) / 127;
    right = ((sep) * vol) / 127;

    if (left < 0) left = 0;
    else if ( left > 255) left = 255;
    if (right < 0) right = 0;
    else if (right > 255) right = 255;

    channels[handle].left = left;
    channels[handle].right = right;
}

static int I_Pico_StartSound(should_be_const sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
    if (!check_and_init_channel(channel)) return -1;

    stop_channel(channel);
    channel_t *ch = &channels[channel];
    if (!init_channel_for_sfx(ch, sfxinfo, pitch)) {
        assert(!is_channel_playing(channel)); // don't expect to have to mark it sotpped
    }
    I_Pico_UpdateSoundParams(channel, vol, sep);
    return channel;
}

static void I_Pico_StopSound(int channel)
{
    if (check_and_init_channel(channel)) {

    }
}

static boolean I_Pico_SoundIsPlaying(int channel)
{
    if (!check_and_init_channel(channel)) return false;
    return is_channel_playing(channel);
}

static void I_Pico_UpdateSound(void)
{
    if (!sound_initialized) return;

    // Poll headphone-detect (cheap; short-circuits if no IRQ latched). Runs
    // here rather than from a timer so it stays on core0, where the TLV320
    // register writes and speaker-mute path are safe to do.
    doom_poll_headphone();

    // Back-pressure: don't mix if the I2S ring is too full — the extra
    // samples would just drop in audio_i2s_enqueue_sample(). Keep ~half the
    // ring free so a burst of writes doesn't cause underflow if we get
    // preempted.
    if (audio_i2s_get_freebuffer_size() < MIX_CHUNK_SAMPLES) {
        return;
    }

    // HDMI DI-ring back-pressure: pd_end_frame() busy-loops calling
    // I_UpdateSound(), each call packs MIX_CHUNK_SAMPLES/4 packets in one
    // burst — much faster than the DI drain (~12 kpkt/s at 49716 Hz). If we
    // push past HSTX_AUDIO_DI_HIGH_WATERMARK, hstx_push_audio_sample drops
    // packets, which sounds like static on the HDMI sink. Skip this mix
    // cycle when the DI ring can't absorb another full burst; the next call
    // (microseconds later) retries after some drain.
    if (doom_audio_sink == DOOM_SINK_HDMI) {
        const uint32_t di_burst_packets = MIX_CHUNK_SAMPLES / 4;
        if (hstx_di_queue_get_level() + di_burst_packets > HSTX_AUDIO_DI_HIGH_WATERMARK) {
            return;
        }
    }

    audio_buffer_t *buffer = &mix_audio_buffer;
    if (music_generator) {
        // todo think about volume; this already has a (<< 3) in it
        music_generator(buffer);
    } else {
        memset(buffer->buffer->bytes, 0, buffer->buffer->size);
    }
    for(int ch=0; ch < NUM_SOUND_CHANNELS; ch++) {
        if (is_channel_playing(ch)) {
            channel_t *channel = &channels[ch];
            assert(channel->decompressed_size);
            int voll = channel->left/2;
            int volr = channel->right/2;
            uint offset_end = channel->decompressed_size * 65536;
            assert(channel->offset < offset_end);
            int16_t *samples = (int16_t *)buffer->buffer->bytes;
#if SOUND_LOW_PASS
            int alpha256 = channel->alpha256;
            int beta256 = 256 - alpha256;
            int sample = channel->decompressed[channel->offset >> 16];
#endif
            for(int s=0;s<buffer->max_sample_count;s++) {
#if !SOUND_LOW_PASS
                int sample = channel->decompressed[channel->offset >> 16];
#else
                sample = (beta256 * sample + alpha256 * channel->decompressed[channel->offset >> 16]) / 256;
#endif
                *samples++ += sample * voll;
                *samples++ += sample * volr;
                channel->offset += channel->step;
                if (channel->offset >= offset_end) {
                    channel->offset -= offset_end;
                    decompress_buffer(channel);
                    offset_end = channel->decompressed_size * 65536;
                    if (channel->offset >= offset_end) {
                        stop_channel(ch);
                        break;
                    }
                }
            }
        }
    }
    buffer->sample_count = buffer->max_sample_count;
    if (fade_state == FS_SILENT) {
        memset(buffer->buffer->bytes, 0, buffer->buffer->size);
    } else if (fade_state != FS_NONE) {
        int16_t *samples = (int16_t *)buffer->buffer->bytes;
        int fade_step = fade_state == FS_FADE_IN ? FADE_STEP : -FADE_STEP;
        int i;
        for(i=0;i<buffer->sample_count * 2 && fade_level;i+=2) {
            samples[i] = (samples[i] * (int)fade_level) >> 16;
            samples[i+1] = (samples[i+1] * (int)fade_level) >> 16;
            fade_level += fade_step;
        }
        if (!fade_level) {
            if (fade_state == FS_FADE_OUT) {
                for(;i<buffer->sample_count * 2;i++) {
                    samples[i] = 0;
                }
                fade_state = FS_SILENT;
            } else {
                fade_state = FS_NONE;
            }
        }
    }

    // Fan out per-sample to the currently-selected sink. Both the HDMI DI
    // queue and the I2S ring accept int16 stereo; pack (L << 16) | R for
    // pico_shared's audio_i2s.
    //
    // Design invariants (see plan / DC-offset section):
    //   - HDMI sink active: we still enqueue zeros to I2S so BCLK keeps
    //     toggling and the TLV320's PLL stays locked — otherwise the DAC
    //     would pop on the next transition.
    //   - I2S sink active: we stop pushing to HDMI. The DI queue falls back
    //     to pre-encoded silence packets on underrun (hstx_data_island_queue),
    //     which keeps ACR/AVI/audio-infoframe alive without our samples.
    //
    // Raw pass-through on both paths — DC-blocker + gain on the HDMI side
    // was tried (mirroring pico-infonesPlus) and made no measurable
    // difference to the observed broadband HDMI hiss. If HDMI needs it back,
    // apply to the DAC's audio_i2s_setVolume() gain instead of the sample
    // pipeline so both sinks stay bit-identical.
    const int16_t *sp = (const int16_t *)buffer->buffer->bytes;
    const uint32_t n = buffer->sample_count;
#if DOOM_AUDIO_EXCLUSIVE_SINK
    if (doom_audio_sink == DOOM_SINK_HEADPHONES) {
        for (uint32_t s = 0; s < n; s++) {
            int16_t l = *sp++;
            int16_t r = *sp++;
            audio_i2s_enqueue_sample(((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
        }
    } else {
        for (uint32_t s = 0; s < n; s++) {
            int16_t l = *sp++;
            int16_t r = *sp++;
            hstx_push_audio_sample((int)l, (int)r);
            audio_i2s_enqueue_sample(0);
        }
    }
#else
    // No headphone detect on this board: feed real samples to both sinks.
    // doom_audio_sink stays at its DOOM_SINK_HDMI default, so the DI-ring
    // back-pressure gate above still applies, and the I2S ring keeps its
    // role as the mixer's pacing clock.
    for (uint32_t s = 0; s < n; s++) {
        int16_t l = *sp++;
        int16_t r = *sp++;
        hstx_push_audio_sample((int)l, (int)r);
        audio_i2s_enqueue_sample(((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
    }
#endif
}

static void I_Pico_ShutdownSound(void)
{
    if (!sound_initialized)
    {
        return;
    }
    sound_initialized = false;
}

static boolean I_Pico_InitSound(boolean _use_sfx_prefix)
{
    use_sfx_prefix = _use_sfx_prefix;

    // pico_shared: DAC register program (TLV320 boards) + I2S PIO/DMA +
    // immediate silence pre-fill so BCLK is up and the DAC PLL locks before
    // we push any real audio. The driver id comes from the board's cflags
    // header. DMA channel must be >= 4 (see pico_hdmi_glue.h) so audio uses
    // DMA_IRQ_1, keeping DMA_IRQ_0 exclusively for pico_hdmi.
    audio_i2s_hw_t *i2s = audio_i2s_setup(DOOM_AUDIO_I2S_DRIVER,
                                          PICO_SOUND_SAMPLE_FREQ,
                                          /*dmachan=*/ 6);
    if (!i2s) {
        panic("PicoAudio: I2S setup failed (driver %d).\n", DOOM_AUDIO_I2S_DRIVER);
    }

    // Speaker stays muted forever on Fruit Jam per project spec: with
    // headphones out, HDMI is the sink; with headphones in, the DAC's
    // headphone jack is the sink. The tlv320 driver defaults to speaker
    // *unmuted* on cold boot, and toggles it on jack events. Force muted
    // now so first-frame audio doesn't blast the internal speaker.
    audio_i2s_muteInternalSpeaker(true);

    // Boost DAC digital gain. tlv320_program_registers ships +5 dB (0x0A);
    // Doom's mixer produces relatively low peak levels (SFX only reaches
    // ~half full-scale before clipping, OPL is quieter still), so
    // headphones read as too quiet. +14 dB (0x1C = 28 × 0.5 dB) gives
    // comfortable listening without clipping on max-volume SFX.
    audio_i2s_setVolume(14);

#if INCREASE_I2S_DRIVE_STRENGTH
    bi_decl(bi_program_feature("12mA I2S"));
    gpio_set_drive_strength(PICO_AUDIO_I2S_DATA_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(PICO_AUDIO_I2S_CLOCK_PIN_BASE, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, GPIO_DRIVE_STRENGTH_12MA);
#endif

    sound_initialized = true;
    return true;
}

static snddevice_t sound_pico_devices[] =
{
    SNDDEVICE_SB,
};

sound_module_t sound_pico_module =
{
    sound_pico_devices,
    arrlen(sound_pico_devices),
    I_Pico_InitSound,
    I_Pico_ShutdownSound,
    I_Pico_GetSfxLumpNum,
    I_Pico_UpdateSound,
    I_Pico_UpdateSoundParams,
    I_Pico_StartSound,
    I_Pico_StopSound,
    I_Pico_SoundIsPlaying,
    I_Pico_PrecacheSounds,
};

bool I_PicoSoundIsInitialized(void) {
    return sound_initialized;
}

void I_PicoSoundSetMusicGenerator(void (*generator)(audio_buffer_t *buffer)) {
    music_generator = generator;
}

#if PICO_ON_DEVICE
void I_PicoSoundFade(bool in) {
    fade_state = in ? FS_FADE_IN : FS_FADE_OUT;
    fade_level = in ? FADE_STEP : 0x10000 - FADE_STEP;
}

bool I_PicoSoundFading(void) {
    return fade_state == FS_FADE_IN || fade_state == FS_FADE_OUT;
}
#endif
