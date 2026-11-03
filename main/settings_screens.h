/*
 * settings_screens.h
 *
 * Two LVGL screens driven only by the rotary encoder:
 *   1. Access-code screen: dial a 4-digit code (turn = digit, click = next).
 *   2. Settings screen: adjust four spindle calibration trims live (readout
 *      low/high for the ADC, output low/high for the DAC); saved to NVS.
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

/* Parameter indices (order on screen). */
typedef enum
{
    SETTING_READ_LOW = 0, // readout (ADC) trim at RPM_MIN, rpm
    SETTING_READ_HIGH,    // readout (ADC) trim at RPM_MAX, rpm
    SETTING_OUT_LOW,      // output (DAC) trim at RPM_MIN, rpm
    SETTING_OUT_HIGH,     // output (DAC) trim at RPM_MAX, rpm
    SETTINGS_PARAM_COUNT
} settings_index_t;

/* Called whenever a parameter value changes (while adjusting, on cancel, and
 * once per parameter at startup from settings_init). Runs in the encoder task,
 * NOT in the LVGL task, and without the LVGL lock held. */
typedef void (*settings_change_cb_t)(int index, int32_t value);

/* Initialise NVS, load saved values (or defaults) and report them via on_change.
 * Call once from app_main before the encoder task starts. */
esp_err_t settings_init(settings_change_cb_t on_change);

/* Current value / display name of a parameter (settings_index_t). */
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
