#ifndef AUDIO_MONO_MIX_H
#define AUDIO_MONO_MIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_STEREO_MIX_LEFT_DUPLICATE,
    AUDIO_STEREO_MIX_RIGHT_DUPLICATE,
    AUDIO_STEREO_MIX_MONO_AVERAGE,
    AUDIO_STEREO_MIX_STEREO_PASSTHROUGH,
} audio_stereo_mix_mode_t;

/**
 * @brief Reset the low-frequency compensation filter state.
 */
void audio_mono_mix_reset(void);

/**
 * @brief Select how interleaved stereo PCM is mapped to the board playback path.
 */
void audio_mono_mix_set_mode(audio_stereo_mix_mode_t mode);

/**
 * @brief Return the current stereo mix mode name for logs.
 */
const char *audio_mono_mix_mode_name(void);

/**
 * @brief Downmix and condition interleaved signed 16-bit little-endian stereo PCM in place.
 *
 * Each complete L/R frame is replaced with mono on both channels. The mono
 * stream is lightly low-shelf compensated for the small single-speaker output
 * path and saturated to 16-bit range. Any trailing bytes that do not form a
 * full stereo frame are left untouched.
 */
void audio_mono_mix_stereo16le_in_place(uint8_t *pcm, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MONO_MIX_H */
