#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "tusb_cdc_acm.h"
#include "LGFX_WAVESHARE_ESP32S3TouchLCD2_SPI_ST7789T3_I2C_CST816D.hpp"
static LGFX_WAVESHARE_ESP32S3TouchLCD2_SPI_ST7789T3_I2C_CST816D lcd;
static const char *TAG = "test";
static uint8_t rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];

#define TUSB_DESC_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// --- 1. ディスクリプタの定義 ---
// PCに対して「私はキーボードである」と名乗るためのデータです
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

// 文字列ディスクリプタ（PC上で表示される名前など）
static const char* usb_string_descriptor[] = {
    // 0: 言語ID (0x0409 = English)
    (const char[]) {0x09, 0x04},
    "TinyUSB",                  // 1: 製造元
    "StreamDeck-like keyboard", // 2: 製品名
    "123456",                   // 3: シリアル番号
    "HID Keyboard(StreamDeck)", // 4: HIDインターフェース用
    "USB Serial(StreamDeck)"    // 5: CDCインターフェース用
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */

static const uint8_t composite_configuration_descriptor[] = {
    // 1. Configuration Descriptor (インターフェース数3)
    // CDCは2つのインターフェース（Control & Data）を占有するため計3になる
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // 2. HID Interface Descriptor (インターフェース番号 0, 文字列ディスクリプタ 4 を使用)
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
    // 3. CDC Interface Descriptor (インターフェース番号 1,2, 文字列ディスクリプタ 5 を使用)
    // 0x82, 0x83 など、HIDと被らないエンドポイントを割り当てる
    TUD_CDC_DESCRIPTOR(1, 5, 0x82, 8, 0x03, 0x83, 64),
};
/*
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

*/

/**
 * @brief Application Queue
 */
static QueueHandle_t app_queue;
typedef struct {
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];     // Data buffer
    size_t buf_len;                                     // Number of bytes received
    uint8_t itf;                                        // Index of CDC device interface
} app_message_t;

/**
 * @brief CDC device RX callback
 *
 * CDC device signals, that new data were received
 *
 * @param[in] itf   CDC device index
 * @param[in] event CDC event type
 */
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, rx_buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {

        /*
        app_message_t tx_msg = {
            .buf = {},
            .buf_len = rx_size,
            .itf = (uint8_t)itf,
        };
        */

        /* Copy received message to application queue buffer */
        //memcpy(tx_msg.buf, rx_buf, rx_size);
        //xQueueSend(app_queue, &tx_msg, 0);

    } else {
        ESP_LOGE(TAG, "Read Error");
    }
}

/**
 * @brief CDC device line change callback
 *
 * CDC device signals, that the DTR, RTS states changed
 *
 * @param[in] itf   CDC device index
 * @param[in] event CDC event type
 */
void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed on channel %d: DTR:%d, RTS:%d", itf, dtr, rts);
}

// --- 2. USB HID の初期化 ---
void init_usb_composite_device() {
    static const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = usb_string_descriptor,
        .string_descriptor_count = sizeof(usb_string_descriptor)/sizeof(usb_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = composite_configuration_descriptor,
        .self_powered = false,
        .vbus_monitor_io = -1,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    static tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = &tinyusb_cdc_rx_callback, // the first way to register a callback
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    /* the second way to register a callback */
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
                        TINYUSB_CDC_ACM_0,
                        CDC_EVENT_LINE_STATE_CHANGED,
                        &tinyusb_cdc_line_state_changed_callback));

    // ログをUSB CDCに転送する設定（sdkconfig CONFIG_TINYUSB_CDC_ENABLED=y の設定が必要）
    //esp_log_set_vprintf(vprintf);
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
        ESP_LOGW(TAG, "USB HID not ready yet...\n");
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

    /*
    app_queue = xQueueCreate(5, sizeof(app_message_t));
    assert(app_queue);
    app_message_t msg;
    */
    init_usb_composite_device();
    vTaskDelay(pdMS_TO_TICKS(500));

    lcd.init();
    lcd.setRotation(1); // 横向き設定
    lcd.fillScreen(TFT_BLACK);

    setupButtons();
    drawButtons();

    
    ESP_LOGI(TAG, "Stream Deck Ready!\n");

    while (true) {
        uint16_t tx, ty;
        if (lcd.getTouch(&tx, &ty)) {
            // どのボタンが押されたか判定
            for (int i = 0; i < 6; i++) {
                if (tx >= buttons[i].x && tx < (buttons[i].x + buttons[i].w) &&
                    ty >= buttons[i].y && ty < (buttons[i].y + buttons[i].h)) {
                    
                    ESP_LOGI(TAG, "Button %d Pressed! (Key: %d)\n", i, buttons[i].key_code);
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