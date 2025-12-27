#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "LGFX_WAVESHARE_ESP32S3TouchLCD2_SPI_ST7789T3_I2C_CST816D.hpp"
static LGFX_WAVESHARE_ESP32S3TouchLCD2_SPI_ST7789T3_I2C_CST816D lcd;
static const char *TAG = "test";

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// --- 1. ディスクリプタの定義 ---
// PCに対して「私はキーボードである」と名乗るためのデータです
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

// 文字列ディスクリプタ（PC上で表示される名前など）
static const char* hid_string_descriptor[5] = {
    // 0: 言語ID (0x0409 = English)
    (const char[]) {0x09, 0x04},
    "TinyUSB",                  // 1: 製造元
    "StreamDeck-like keyboard", // 2: 製品名
    "123456",                   // 3: シリアル番号
    "SDL HID Keyboard"          // 4: インターフェース名
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// --- 2. USB HID の初期化 ---
void init_usb_hid() {
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor)/sizeof(hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
        //.configuration_descriptor = NULL,
        .self_powered = false,
        .vbus_monitor_io = -1,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
}

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}
// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}
// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}

// --- 指定したキーコードを送信する関数 ---
void send_keyboard_key(uint8_t modifier, uint8_t keycode) {
    if (!tud_hid_ready()) {
        printf("USB HID not ready yet...\n");
        return;
    }
    // 1. キーを押した状態を送信
    uint8_t key_report[6] = { keycode, 0, 0, 0, 0, 0 };
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, key_report);
    
    // 2. 確実に認識させるため少し待つ
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 3. キーを離した状態を送信
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
}

// ボタンの情報を保持する構造体
struct TouchButton {
    int x, y, w, h;
    const char* label;
    uint8_t key_code; // 将来的にUSB-HIDで使うキー
};

// 3x2 のボタン配置を定義
TouchButton buttons[6];

void setupButtons() {
    int btnW = lcd.width() / 3;
    int btnH = lcd.height() / 2;

    for (int i = 0; i < 6; i++) {
        buttons[i].x = (i % 3) * btnW;
        buttons[i].y = (i / 3) * btnH;
        buttons[i].w = btnW;
        buttons[i].h = btnH;
        // 仮のラベル、キーコード
        static char labels[6][10];
        static uint8_t key_code[6] = {HID_KEY_A, HID_KEY_B, HID_KEY_C, HID_KEY_D, HID_KEY_E, HID_KEY_F};
        sprintf(labels[i], "Btn %d", i);
        buttons[i].label = labels[i];
        buttons[i].key_code = key_code[i];
    }
}

void drawButtons() {
    lcd.startWrite();
    for (int i = 0; i < 6; i++) {
        // 枠とテキストの描画
        lcd.drawRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, TFT_WHITE);
        lcd.drawCenterString(buttons[i].label, buttons[i].x + buttons[i].w / 2, buttons[i].y + buttons[i].h / 2 - 8);
    }
    lcd.endWrite();
}

extern "C" void app_main(void) {
    lcd.init();
    lcd.setRotation(1); // 横向き設定
    lcd.fillScreen(TFT_BLACK);

    setupButtons();
    drawButtons();

    init_usb_hid();
    
    printf("Stream Deck Ready!\n");

    while (true) {
        uint16_t tx, ty;
        if (lcd.getTouch(&tx, &ty)) {
            // どのボタンが押されたか判定
            for (int i = 0; i < 6; i++) {
                if (tx >= buttons[i].x && tx < (buttons[i].x + buttons[i].w) &&
                    ty >= buttons[i].y && ty < (buttons[i].y + buttons[i].h)) {
                    
                    printf("Button %d Pressed! (Key: %d)\n", i, buttons[i].key_code);
                    send_keyboard_key(0, buttons[i].key_code);

                    // 視覚的フィードバック（一瞬色を変えるなど）
                    lcd.fillRect(buttons[i].x + 2, buttons[i].y + 2, buttons[i].w - 4, buttons[i].h - 4, TFT_BLUE);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    lcd.fillRect(buttons[i].x + 2, buttons[i].y + 2, buttons[i].w - 4, buttons[i].h - 4, TFT_BLACK);
                    drawButtons(); // 再描画
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}