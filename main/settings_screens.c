/*
 * settings_screens.c
 *
 * Access-code screen + two-parameter settings screen for the CNC display.
 * Rotary encoder with push button is the only input. See settings_screens.h.
 *
 * Threading: every LVGL call here happens either under lvgl_port_lock()
 * (functions called from the encoder task) or inside an LVGL timer callback
 * (which already runs in the LVGL task with the lock held).
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "UI/ui.h" // ui_Primary
#include "colorShifters.h"
#include "settings_screens.h"

static const char *TAG = "SETTINGS";

/*==========================================================*/
/*================ configuration: edit these ===============*/
/*==========================================================*/

static const uint8_t ACCESS_CODE[] = {1, 2, 3, 4}; // four digits, 0-9
#define CODE_LEN ((int)sizeof(ACCESS_CODE))
#define CODE_MAX_TRIES 3             // wrong attempts before returning to main
#define CODE_IDLE_TIMEOUT_MS 30000   // code screen gives up after 30 s idle

typedef struct
{
    const char *name;    // shown on screen
    const char *unit;    // appended to the value, e.g. " ms", "%", "" for none
    int32_t min, max;    // allowed range
    int32_t step;        // change per encoder detent
    int32_t def;         // default when nothing is saved yet
    const char *nvs_key; // NVS key, max 15 characters, must be unique
} param_def_t;

/* Spindle speed calibration, applied in main.c. Each pair is a two-point trim
 * in rpm: the "low" value applies fully at RPM_MIN, the "high" value fully at
 * RPM_MAX, with a straight-line blend in between.
 *
 *   Readout  - added to the speed measured from the VFD's analog output (ADC),
 *              so "Actual" matches the speed the VFD itself reports.
 *              Positive = the display was reading low.
 *   Output   - added to the commanded speed before it becomes the DAC voltage.
 *              Positive = the spindle was running slow.
 *
 * Calibrate Readout first: Output is judged against "Actual".
 * Order here must match settings_index_t in settings_screens.h. */
static const param_def_t PARAMS[SETTINGS_PARAM_COUNT] = {
    [SETTING_READ_LOW] = {"Readout low", " rpm", -2000, 2000, 10, 0, "read_low"},
    [SETTING_READ_HIGH] = {"Readout high", " rpm", -2000, 2000, 10, 0, "read_high"},
    [SETTING_OUT_LOW] = {"Output low", " rpm", -2000, 2000, 10, 0, "trim_low"},   // keys kept from the
    [SETTING_OUT_HIGH] = {"Output high", " rpm", -2000, 2000, 10, 0, "trim_high"}, // 2-trim version
};

#define NVS_NAMESPACE "settings"

/*==========================================================*/
/*======================== look ============================*/
/*==========================================================*/

/* This panel shows green and blue swapped, so colours go through rgb2rbg()
 * the same way ui_Primary.c does. Greys are unaffected. */
#define RBG(hex) lv_color_hex(rgb2rbg(hex))
#define COLOR_BG lv_color_hex(0x0a000a) // same as ui_Primary
#define COLOR_BOX lv_color_hex(0x1c181c)
#define COLOR_BORDER lv_color_hex(0x404040)
#define COLOR_DIM lv_color_hex(0x606060)
#define COLOR_HINT lv_color_hex(0x9a9a9a)
#define COLOR_WHITE lv_color_white()
#define COLOR_YELLOW RBG(0xffff00)
#define COLOR_CYAN RBG(0x00faff)
#define COLOR_ORANGE RBG(0xff9900)
#define COLOR_RED RBG(0xff3b3b)
#define COLOR_GREEN RBG(0x3bd16f)

/* Screens switch instantly. LVGL 8.4 deletes the wrong screen if a new
 * lv_scr_load_anim() starts while a previous slide animation is still running
 * (e.g. a quick hold-to-cancel right after the settings screen appears), so
 * animated transitions are deliberately not used here. */
static void show_screen(lv_obj_t *scr, bool delete_old)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, delete_old);
}

/*==========================================================*/
/*======================== state ===========================*/
/*==========================================================*/

typedef enum
{
    UI_OFF = 0,
    UI_CODE,
    UI_SETTINGS,
} ui_mode_t;

static volatile ui_mode_t s_mode = UI_OFF;
static settings_change_cb_t s_on_change = NULL;

static int32_t s_values[SETTINGS_PARAM_COUNT];
static int32_t s_entry_values[SETTINGS_PARAM_COUNT]; // restored on cancel

/* code screen */
static lv_obj_t *s_code_scr;
static lv_obj_t *s_code_row;
static lv_obj_t *s_code_box[sizeof(ACCESS_CODE)];
static lv_obj_t *s_code_digit[sizeof(ACCESS_CODE)];
static lv_obj_t *s_code_msg;
static uint8_t s_digits[sizeof(ACCESS_CODE)];
static int s_pos;   // digit being dialed (CODE_LEN = all entered)
static int s_tries; // wrong attempts so far
static bool s_input_blocked;
static uint32_t s_last_activity;
static lv_timer_t *s_idle_timer;

/* settings screen */
#define FOCUS_SAVE SETTINGS_PARAM_COUNT // focus index of the Save & Exit row
static lv_obj_t *s_set_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_row[SETTINGS_PARAM_COUNT];
static lv_obj_t *s_row_value[SETTINGS_PARAM_COUNT];
static lv_obj_t *s_save_box;
static lv_obj_t *s_save_lbl;
static lv_obj_t *s_hint;
static int s_focus;
static bool s_editing;

/*==========================================================*/
/*======================= helpers ==========================*/
/*==========================================================*/

static int32_t clamp_param(int index, int32_t v)
{
    if (v < PARAMS[index].min)
        return PARAMS[index].min;
    if (v > PARAMS[index].max)
        return PARAMS[index].max;
    return v;
}

static int dir_of(const rotary_encoder_event_t *e)
{
    return (e->diff > 0) - (e->diff < 0); // ignore acceleration, one step per event
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *make_box(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(o, COLOR_BOX, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, COLOR_BORDER, 0);
    return o;
}

static void set_box_focus(lv_obj_t *box, bool focused, lv_color_t color)
{
    lv_obj_set_style_border_color(box, focused ? color : COLOR_BORDER, 0);
    lv_obj_set_style_border_width(box, focused ? 3 : 2, 0);
}

/* Return to the spindle screen and delete whichever settings screen is showing. */
static void go_to_main(void)
{
    if (s_idle_timer)
    {
        lv_timer_del(s_idle_timer);
        s_idle_timer = NULL;
    }
    s_mode = UI_OFF;
    show_screen(ui_Primary, true);
    s_code_scr = NULL;
    s_set_scr = NULL;
    s_title = NULL;
}

/*==========================================================*/
/*==================== NVS persistence =====================*/
/*==========================================================*/

static void save_values(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
    {
        err = nvs_set_i32(h, PARAMS[i].nvs_key, s_values[i]);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "save %s failed: %s", PARAMS[i].nvs_key, esp_err_to_name(err));
    }
    err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "settings saved");
    else
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
}

esp_err_t settings_init(settings_change_cb_t on_change)
{
    s_on_change = on_change;
    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
        s_values[i] = PARAMS[i].def;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs erasing (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) // NOT_FOUND on first boot is normal
    {
        for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
        {
            int32_t v;
            if (nvs_get_i32(h, PARAMS[i].nvs_key, &v) == ESP_OK)
                s_values[i] = clamp_param(i, v);
        }
        nvs_close(h);
    }

    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
    {
        ESP_LOGI(TAG, "%s = %" PRIi32, PARAMS[i].name, s_values[i]);
        if (s_on_change)
            s_on_change(i, s_values[i]);
    }
    return ESP_OK;
}

int32_t settings_get(int index)
{
    return (index >= 0 && index < SETTINGS_PARAM_COUNT) ? s_values[index] : 0;
}

const char *settings_name(int index)
{
    return (index >= 0 && index < SETTINGS_PARAM_COUNT) ? PARAMS[index].name : "";
}

bool settings_ui_active(void)
{
    return s_mode != UI_OFF;
}

void settings_ui_set_info(const char *text)
{
    if (s_mode == UI_SETTINGS && s_title)
        lv_label_set_text(s_title, text);
}

/*==========================================================*/
/*==================== access-code screen ==================*/
/*==========================================================*/

#define CODE_BOX_W 56
#define CODE_BOX_H 72
#define CODE_BOX_GAP 12

static void code_refresh(void)
{
    for (int i = 0; i < CODE_LEN; i++)
    {
        char txt[2] = {0};
        lv_color_t color;
        if (i < s_pos)
        {
            txt[0] = '*';
            color = COLOR_HINT;
        }
        else if (i == s_pos)
        {
            txt[0] = (char)('0' + s_digits[i]);
            color = COLOR_WHITE;
        }
        else
        {
            txt[0] = '-';
            color = COLOR_DIM;
        }
        lv_label_set_text(s_code_digit[i], txt);
        lv_obj_set_style_text_color(s_code_digit[i], color, 0);
        set_box_focus(s_code_box[i], i == s_pos, COLOR_CYAN);
    }
}

static void code_reset_entry(void)
{
    memset(s_digits, 0, sizeof(s_digits));
    s_pos = 0;
}

static void shake_exec_cb(void *obj, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)x);
}

static void shake_ready_cb(lv_anim_t *a)
{
    lv_obj_set_x((lv_obj_t *)a->var, 0);
}

static void code_shake(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_code_row);
    lv_anim_set_exec_cb(&a, shake_exec_cb);
    lv_anim_set_values(&a, -10, 10);
    lv_anim_set_time(&a, 50);
    lv_anim_set_playback_time(&a, 50);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_ready_cb(&a, shake_ready_cb);
    lv_anim_start(&a);
}

static void idle_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_mode == UI_CODE && lv_tick_elaps(s_last_activity) > CODE_IDLE_TIMEOUT_MS)
    {
        ESP_LOGI(TAG, "code entry timed out");
        go_to_main();
    }
}

static void lockout_timer_cb(lv_timer_t *t)
{
    (void)t; // one-shot: LVGL deletes it after this call
    if (s_mode == UI_CODE)
        go_to_main();
}

static void code_screen_create(void)
{
    s_code_scr = make_screen();

    lv_obj_t *title = make_label(s_code_scr, &lv_font_montserrat_22, COLOR_YELLOW, "Enter Access Code");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    s_code_row = lv_obj_create(s_code_scr);
    lv_obj_remove_style_all(s_code_row);
    lv_obj_clear_flag(s_code_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_code_row, CODE_LEN * CODE_BOX_W + (CODE_LEN - 1) * CODE_BOX_GAP, CODE_BOX_H);
    lv_obj_align(s_code_row, LV_ALIGN_CENTER, 0, -8);

    for (int i = 0; i < CODE_LEN; i++)
    {
        s_code_box[i] = make_box(s_code_row, CODE_BOX_W, CODE_BOX_H);
        lv_obj_set_pos(s_code_box[i], i * (CODE_BOX_W + CODE_BOX_GAP), 0);
        s_code_digit[i] = make_label(s_code_box[i], &lv_font_montserrat_36, COLOR_WHITE, "");
        lv_obj_center(s_code_digit[i]);
    }

    s_code_msg = make_label(s_code_scr, &lv_font_montserrat_18, COLOR_RED, "");
    lv_obj_align(s_code_msg, LV_ALIGN_CENTER, 0, 52);

    lv_obj_t *hint = make_label(s_code_scr, &lv_font_montserrat_14, COLOR_HINT,
                                "Turn: digit    Click: next    Hold: cancel");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    code_reset_entry();
    code_refresh();
}

static void settings_screen_open(void); // forward

static void code_accept_digit(void)
{
    s_pos++;
    if (s_pos < CODE_LEN)
    {
        code_refresh();
        return;
    }

    if (memcmp(s_digits, ACCESS_CODE, CODE_LEN) == 0)
    {
        ESP_LOGI(TAG, "access code accepted");
        settings_screen_open();
        return;
    }

    s_tries++;
    ESP_LOGW(TAG, "wrong access code (%d/%d)", s_tries, CODE_MAX_TRIES);
    if (s_tries >= CODE_MAX_TRIES)
    {
        s_input_blocked = true;
        code_refresh(); // all boxes show '*'
        lv_label_set_text(s_code_msg, "Access denied");
        code_shake();
        lv_timer_t *t = lv_timer_create(lockout_timer_cb, 1500, NULL);
        lv_timer_set_repeat_count(t, 1);
        return;
    }

    int left = CODE_MAX_TRIES - s_tries;
    char msg[40];
    snprintf(msg, sizeof(msg), "Wrong code - %d %s left", left, left == 1 ? "try" : "tries");
    lv_label_set_text(s_code_msg, msg);
    code_shake();
    code_reset_entry();
    code_refresh();
}

static void code_handle_event(const rotary_encoder_event_t *e)
{
    if (s_input_blocked)
        return;
    s_last_activity = lv_tick_get();

    switch (e->type)
    {
    case RE_ET_CHANGED:
    {
        int d = dir_of(e);
        if (d == 0 || s_pos >= CODE_LEN)
            break;
        s_digits[s_pos] = (uint8_t)((s_digits[s_pos] + d + 10) % 10);
        code_refresh();
        break;
    }
    case RE_ET_BTN_CLICKED:
        code_accept_digit();
        break;
    case RE_ET_BTN_LONG_PRESSED:
        go_to_main();
        break;
    default:
        break;
    }
}

/*==========================================================*/
/*===================== settings screen ====================*/
/*==========================================================*/

#define ROW_W 296
#define ROW_H 34
#define ROW_Y0 28
#define ROW_PITCH 38
#define GROUP_GAP 4 // extra space between the Readout and Output pairs
#define SAVE_H 28

static lv_coord_t row_y(int i)
{
    return ROW_Y0 + i * ROW_PITCH + (i >= SETTING_OUT_LOW ? GROUP_GAP : 0);
}

static void format_value(int index, char *buf, size_t len)
{
    // show an explicit + on ranges that can go negative
    const char *fmt = (PARAMS[index].min < 0 && s_values[index] != 0) ? "%+ld%s" : "%ld%s";
    snprintf(buf, len, fmt, (long)s_values[index], PARAMS[index].unit);
}

static void settings_refresh(void)
{
    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
    {
        bool focused = (s_focus == i);
        bool editing = focused && s_editing;
        lv_color_t accent = editing ? COLOR_ORANGE : COLOR_CYAN;

        set_box_focus(s_row[i], focused, accent);

        char buf[24];
        format_value(i, buf, sizeof(buf));
        lv_label_set_text(s_row_value[i], buf);
        lv_obj_set_style_text_color(s_row_value[i], editing ? COLOR_ORANGE : COLOR_WHITE, 0);
    }

    bool save_focused = (s_focus == FOCUS_SAVE);
    set_box_focus(s_save_box, save_focused, COLOR_GREEN);
    lv_obj_set_style_text_color(s_save_lbl, save_focused ? COLOR_GREEN : COLOR_WHITE, 0);

    if (s_editing)
        lv_label_set_text(s_hint, "Turn: adjust    Click: done    Hold: cancel");
    else if (save_focused)
        lv_label_set_text(s_hint, "Turn: select    Click: save    Hold: cancel");
    else
        lv_label_set_text(s_hint, "Turn: select    Click: edit    Hold: cancel");
}

static void settings_screen_create(void)
{
    s_set_scr = make_screen();

    // title line; main.c replaces it with the live set/actual speed via settings_ui_set_info()
    s_title = make_label(s_set_scr, &lv_font_montserrat_18, COLOR_YELLOW, "Spindle Calibration");
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 4);

    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
    {
        s_row[i] = make_box(s_set_scr, ROW_W, ROW_H);
        lv_obj_align(s_row[i], LV_ALIGN_TOP_MID, 0, row_y(i));

        lv_obj_t *name = make_label(s_row[i], &lv_font_montserrat_18, COLOR_CYAN, PARAMS[i].name);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);

        s_row_value[i] = make_label(s_row[i], &lv_font_montserrat_22, COLOR_WHITE, "");
        lv_obj_align(s_row_value[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }

    s_save_box = make_box(s_set_scr, ROW_W, SAVE_H);
    lv_obj_align(s_save_box, LV_ALIGN_TOP_MID, 0, row_y(SETTINGS_PARAM_COUNT - 1) + ROW_H + 6);
    s_save_lbl = make_label(s_save_box, &lv_font_montserrat_18, COLOR_WHITE, "Save & Exit");
    lv_obj_center(s_save_lbl);

    s_hint = make_label(s_set_scr, &lv_font_montserrat_14, COLOR_HINT, "");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void settings_screen_open(void)
{
    memcpy(s_entry_values, s_values, sizeof(s_values));
    s_focus = 0;
    s_editing = false;

    if (s_idle_timer)
    {
        lv_timer_del(s_idle_timer);
        s_idle_timer = NULL;
    }

    settings_screen_create();
    settings_refresh();
    s_mode = UI_SETTINGS;
    show_screen(s_set_scr, true); // deletes code screen
    s_code_scr = NULL;
}

/* Marks changed[i] for any parameter the event changed, and sets *save when the
 * user picks Save & Exit; the caller acts on both after releasing the LVGL lock. */
static void settings_handle_event(const rotary_encoder_event_t *e, bool changed[], bool *save)
{
    switch (e->type)
    {
    case RE_ET_CHANGED:
    {
        int d = dir_of(e);
        if (d == 0)
            break;
        if (s_editing)
        {
            int i = s_focus;
            int32_t v = clamp_param(i, s_values[i] + d * PARAMS[i].step);
            if (v != s_values[i])
            {
                s_values[i] = v;
                changed[i] = true;
            }
        }
        else
        {
            s_focus += d;
            if (s_focus < 0)
                s_focus = 0;
            if (s_focus > FOCUS_SAVE)
                s_focus = FOCUS_SAVE;
        }
        settings_refresh();
        break;
    }
    case RE_ET_BTN_CLICKED:
        if (s_focus == FOCUS_SAVE)
        {
            *save = true;
            go_to_main();
        }
        else
        {
            s_editing = !s_editing;
            settings_refresh();
        }
        break;
    case RE_ET_BTN_LONG_PRESSED:
        // cancel: put back the values from when the screen was opened
        for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
        {
            if (s_values[i] != s_entry_values[i])
            {
                s_values[i] = s_entry_values[i];
                changed[i] = true;
            }
        }
        ESP_LOGI(TAG, "settings cancelled");
        go_to_main();
        break;
    default:
        break;
    }
}

/*==========================================================*/
/*======================= public API =======================*/
/*==========================================================*/

void settings_ui_open(void)
{
    lvgl_port_lock(0);
    if (s_mode == UI_OFF)
    {
        s_tries = 0;
        s_input_blocked = false;
        code_screen_create();
        s_mode = UI_CODE;
        s_last_activity = lv_tick_get();
        s_idle_timer = lv_timer_create(idle_timer_cb, 1000, NULL);
        show_screen(s_code_scr, false); // keep ui_Primary
    }
    lvgl_port_unlock();
}

void settings_ui_handle_event(const rotary_encoder_event_t *e)
{
    bool changed[SETTINGS_PARAM_COUNT] = {false};
    bool save = false;

    lvgl_port_lock(0);
    if (s_mode == UI_CODE)
        code_handle_event(e);
    else if (s_mode == UI_SETTINGS)
        settings_handle_event(e, changed, &save);
    lvgl_port_unlock();

    // flash writes and user callbacks run without holding the LVGL lock
    if (save)
        save_values();
    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++)
    {
        if (changed[i] && s_on_change)
            s_on_change(i, s_values[i]);
    }
}
