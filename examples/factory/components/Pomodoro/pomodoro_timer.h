/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pomodoro timer that keeps running while its app is closed. Focus phases alternate
 * with short breaks, and every POMO_LONG_BREAK_EVERY focus phases end in a long break.
 * When a phase ends the device vibrates (and chimes if nothing else is playing) and
 * the next phase waits to be started.
 */
#define POMO_LONG_BREAK_EVERY   4

typedef enum {
    POMO_FOCUS = 0,
    POMO_SHORT_BREAK,
    POMO_LONG_BREAK,
    POMO_PHASE_MAX,
} pomo_phase_t;

typedef struct {
    pomo_phase_t phase;
    bool running;
    uint32_t remaining_ms;
    uint32_t total_ms;
    int focus_done;         /*!< Focus phases finished in this round, 0 to POMO_LONG_BREAK_EVERY - 1 */
    uint32_t finished_seq;  /*!< Increments whenever a phase runs out */
    pomo_phase_t finished;  /*!< The phase that ran out last */
} pomo_state_t;

esp_err_t pomo_init(void);
void pomo_get_state(pomo_state_t *state);

void pomo_start(void);
void pomo_pause(void);
/* Back to the full length of the current phase, stopped */
void pomo_reset(void);
/* Move on to the next phase, stopped */
void pomo_skip(void);
/* Choose a phase while stopped */
void pomo_select(pomo_phase_t phase);

/* Phase lengths in seconds */
#define POMO_MIN_SECONDS        5

int pomo_get_seconds(pomo_phase_t phase);
/* Change a phase length while stopped; kept across reboots */
void pomo_set_seconds(pomo_phase_t phase, int seconds);
int pomo_max_seconds(pomo_phase_t phase);

#ifdef __cplusplus
}
#endif
