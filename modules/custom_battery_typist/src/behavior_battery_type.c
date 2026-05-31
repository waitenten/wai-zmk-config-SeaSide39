/*
 * Battery Level Typing Behavior for ZMK
 * SeaSide39 (dongle configuration) version
 *
 * キーを押すとLeft/Rightのバッテリー残量をキーストロークとして送信する: "L85 R72"
 * - ドングル（Central）はバッテリーなしのため、Left/Right両方をペリフェラルから取得
 * - zmk_peripheral_battery_state_changed イベントを購読してキャッシュする
 * - source: 0 = Left, 1 = Right
 * - 出力文字はアルファベット大文字と数字のみ（JIS環境での記号問題を回避）
 */

#define DT_DRV_COMPAT zmk_behavior_battery_type

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* キーストローク間のディレイ（ms）。文字落ちする場合は値を大きくする */
#define KEYSTROKE_DELAY_MS 30

/* ペリフェラルのバッテリーキャッシュ [0]=Left, [1]=Right */
static uint8_t peripheral_battery[2] = {0, 0};
static bool peripheral_battery_valid[2] = {false, false};

/* zmk_peripheral_battery_state_changed イベントを購読してキャッシュ */
static int battery_event_listener(const zmk_event_t *eh)
{
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->source < 2) {
        peripheral_battery[ev->source] = ev->state_of_charge;
        peripheral_battery_valid[ev->source] = true;
        LOG_DBG("Peripheral %d battery: %d%%", ev->source, ev->state_of_charge);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_type_listener, battery_event_listener);
ZMK_SUBSCRIPTION(battery_type_listener, zmk_peripheral_battery_state_changed);

/* HID Usage IDs (USB HID Usage Tables 1.21, Keyboard/Keypad page) */
#define HID_KEY_0      0x27
#define HID_KEY_1      0x1E
#define HID_KEY_2      0x1F
#define HID_KEY_3      0x20
#define HID_KEY_4      0x21
#define HID_KEY_5      0x22
#define HID_KEY_6      0x23
#define HID_KEY_7      0x24
#define HID_KEY_8      0x25
#define HID_KEY_9      0x26
#define HID_KEY_L      0x0F
#define HID_KEY_R      0x15
#define HID_KEY_SPACE  0x2C
#define HID_KEY_LSHIFT 0xE1

/* 文字→HIDキーコードのマッピング構造体 */
struct char_keycode {
    uint8_t keycode;
    bool shift;
};

/*
 * 出力に使う文字のみ定義。
 * 数字(0-9)はShift不要、L/RはShift必要（大文字）。
 * JIS環境でもアルファベット大文字・数字はUS配列と同一なので問題なし。
 */
static const struct char_keycode char_map[] = {
    ['0'] = { .keycode = HID_KEY_0, .shift = false },
    ['1'] = { .keycode = HID_KEY_1, .shift = false },
    ['2'] = { .keycode = HID_KEY_2, .shift = false },
    ['3'] = { .keycode = HID_KEY_3, .shift = false },
    ['4'] = { .keycode = HID_KEY_4, .shift = false },
    ['5'] = { .keycode = HID_KEY_5, .shift = false },
    ['6'] = { .keycode = HID_KEY_6, .shift = false },
    ['7'] = { .keycode = HID_KEY_7, .shift = false },
    ['8'] = { .keycode = HID_KEY_8, .shift = false },
    ['9'] = { .keycode = HID_KEY_9, .shift = false },
    ['L'] = { .keycode = HID_KEY_L, .shift = true  },
    ['R'] = { .keycode = HID_KEY_R, .shift = true  },
    [' '] = { .keycode = HID_KEY_SPACE, .shift = false },
};

#define CHAR_MAP_SIZE (sizeof(char_map) / sizeof(char_map[0]))

/* 1文字をキーストロークとして送信する */
static int send_char(char c)
{
    if ((uint8_t)c >= CHAR_MAP_SIZE || char_map[(uint8_t)c].keycode == 0) {
        LOG_WRN("No keycode mapping for char: 0x%02x", (uint8_t)c);
        return -EINVAL;
    }

    const struct char_keycode *m = &char_map[(uint8_t)c];

    if (m->shift) {
        zmk_hid_keyboard_press(HID_KEY_LSHIFT);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        k_msleep(KEYSTROKE_DELAY_MS);
    }

    zmk_hid_keyboard_press(m->keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(KEYSTROKE_DELAY_MS);

    zmk_hid_keyboard_release(m->keycode);
    if (m->shift) {
        zmk_hid_keyboard_release(HID_KEY_LSHIFT);
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(KEYSTROKE_DELAY_MS);

    return 0;
}

/* 文字列をキーストロークとして送信する */
static int send_string(const char *str)
{
    for (int i = 0; str[i] != '\0'; i++) {
        int ret = send_char(str[i]);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

/* 整数を文字列に変換して送信する（0〜100の範囲を想定） */
static int send_number(int value)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", value);
    return send_string(buf);
}

/* キー押下時の処理: "L85 R72" を送信する */
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event)
{
    /* "L" */
    send_char('L');

    /* Left battery value or "--" if unavailable */
    if (peripheral_battery_valid[0]) {
        send_number(peripheral_battery[0]);
    } else {
        send_string("--");
    }

    /* " " (space) */
    send_char(' ');

    /* "R" */
    send_char('R');

    /* Right battery value or "--" if unavailable */
    if (peripheral_battery_valid[1]) {
        send_number(peripheral_battery[1]);
    } else {
        send_string("--");
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_battery_type_init(const struct device *dev)
{
    return 0;
}

static const struct behavior_driver_api behavior_battery_type_driver_api = {
    .binding_pressed  = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define BATTERY_TYPE_INST(n)                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_battery_type_init, NULL, NULL, NULL,        \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,       \
                            &behavior_battery_type_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BATTERY_TYPE_INST)
