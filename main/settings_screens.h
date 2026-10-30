/*
 * settings_screens.h
 *
 * Two LVGL screens driven only by the rotary encoder:
 *   1. Access-code screen: dial a 4-digit code (turn = digit, click = next).
 *   2. Settings screen: adjust two parameters live (the spindle speed low-end
 *      and high-end trims); values are saved to NVS.
 *
 * Encoder controls
 *   Code screen:     turn = change digit, click = accept digit, hold = cancel
 *   Settings screen: turn = move between rows / adjust value,
 *                    click = start/stop editing, or "Save & Exit",
 *                    hold = cancel (restore values from entry) and exit
 *
 * Edit the access code and the parameter table at the top of settings_screens.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PARAM_COUNT 2

/* Called whenever a parameter value changes (while adjusting, on cancel, and
 * once per parameter at startup from settings_init). Runs in the encoder task,
 * NOT in the LVGL task, and without the LVGL lock held. */
typedef void (*settings_change_cb_t)(int index, int32_t value);

/* Initialise NVS, load saved values (or defaults) and report them via on_change.
 * Call once from app_main before the encoder task starts. */
esp_err_t settings_init(settings_change_cb_t on_change);

/* Current value / display name of parameter 0 or 1. */
int32_t settings_get(int index);
const char *settings_name(int index);

/* True while the code or settings screen is showing. While true, route all
 * encoder events to settings_ui_handle_event() instead of the main screen. */
bool settings_ui_active(void);

/* Replace the settings screen's title line, e.g. with the live spindle speed.
 * Call from an LVGL timer callback (LVGL task, lock already held). Ignored
 * unless the settings screen is showing. */
void settings_ui_set_info(const char *text);

/* Show the access-code screen. Call from the encoder task (takes the LVGL lock). */
void settings_ui_open(void);

/* Feed an encoder event to the active screen. Call from the encoder task. */
void settings_ui_handle_event(const rotary_encoder_event_t *e);

#ifdef __cplusplus
}
#endif
