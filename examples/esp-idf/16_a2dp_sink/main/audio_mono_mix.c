#include "audio_mono_mix.h"

#include <stdbool.h>
#include <string.h>

/*
 * A2DP music is normally stereo while this board drives one small speaker path.
 * After L/R downmix, add a conservative low-shelf lift before the codec. The
 * shelf is intentionally mild and clipped, so it helps thin bass without
 * relying on ES8389 register changes inside managed_components.
 */
#define BASS_LP_ALPHA_Q15 900
#define MONO_DRY_GAIN_Q15 24576
#define MONO_BASS_GAIN_Q15 24576

static int32_t s_low_q0;
static bool s_low_initialized;
static audio_stereo_mix_mode_t s_mix_mode = AUDIO_STEREO_MIX_LEFT_DUPLICATE;

static int16_t saturate_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

void audio_mono_mix_reset(void)
{
    s_low_q0 = 0;
    s_low_initialized = false;
}

void audio_mono_mix_set_mode(audio_stereo_mix_mode_t mode)
{
    s_mix_mode = mode;
    audio_mono_mix_reset();
}

const char *audio_mono_mix_mode_name(void)
{
    switch (s_mix_mode) {
    case AUDIO_STEREO_MIX_LEFT_DUPLICATE:
        return "left-duplicate";
    case AUDIO_STEREO_MIX_RIGHT_DUPLICATE:
        return "right-duplicate";
    case AUDIO_STEREO_MIX_MONO_AVERAGE:
        return "mono-average";
    case AUDIO_STEREO_MIX_STEREO_PASSTHROUGH:
        return "stereo-passthrough";
    default:
        return "unknown";
    }
}

void audio_mono_mix_stereo16le_in_place(uint8_t *pcm, size_t len)
{
    if (pcm == NULL) {
        return;
    }

    const size_t frame_size = sizeof(int16_t) * 2;
    const size_t frame_count = len / frame_size;

    for (size_t i = 0; i < frame_count; ++i) {
        uint8_t *frame = pcm + (i * frame_size);
        int16_t left = 0;
        int16_t right = 0;

        memcpy(&left, frame, sizeof(left));
        memcpy(&right, frame + sizeof(left), sizeof(right));

        int32_t mono = 0;
        switch (s_mix_mode) {
        case AUDIO_STEREO_MIX_LEFT_DUPLICATE:
            mono = left;
            break;
        case AUDIO_STEREO_MIX_RIGHT_DUPLICATE:
            mono = right;
            break;
        case AUDIO_STEREO_MIX_MONO_AVERAGE:
            mono = ((int32_t)left + (int32_t)right) / 2;
            break;
        case AUDIO_STEREO_MIX_STEREO_PASSTHROUGH:
            continue;
        default:
            mono = left;
            break;
        }

        if (s_mix_mode == AUDIO_STEREO_MIX_LEFT_DUPLICATE ||
            s_mix_mode == AUDIO_STEREO_MIX_RIGHT_DUPLICATE) {
            int16_t out = saturate_i16(mono);
            memcpy(frame, &out, sizeof(out));
            memcpy(frame + sizeof(out), &out, sizeof(out));
            continue;
        }

        if (!s_low_initialized) {
            s_low_q0 = mono;
            s_low_initialized = true;
        } else {
            s_low_q0 += ((mono - s_low_q0) * BASS_LP_ALPHA_Q15) >> 15;
        }

        int32_t conditioned = ((mono * MONO_DRY_GAIN_Q15) + (s_low_q0 * MONO_BASS_GAIN_Q15)) >> 15;
        int16_t out = saturate_i16(conditioned);

        memcpy(frame, &out, sizeof(out));
        memcpy(frame + sizeof(out), &out, sizeof(out));
    }
}
