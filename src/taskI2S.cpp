// I2S制御タスク

#include <M5Unified.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>

#include "taskI2S.h"
#include "taskheader.h"

static const char *TAG = "TaskI2S";

// 特殊用途
//#define I2S_USE_PCM5102A

// I2Sピン設定
#define I2S_BCK_PIN   GPIO_NUM_34
#define I2S_WS_PIN    GPIO_NUM_33
#define I2S_DATA_PIN  GPIO_NUM_13
#ifdef I2S_USE_PCM5102A
  #define I2S_XSMT_PIN  12  // GPIO-12
#endif

// I2C設定
#define AW88298_I2C_ADDR 0x36

TaskHandle_t tskI2S;
QueueHandle_t queueAudio;
i2s_chan_handle_t tx_handle = NULL;

static void writeRegister(uint8_t reg, uint16_t value)
{
    value = __builtin_bswap16(value);
    M5.In_I2C.writeRegister(AW88298_I2C_ADDR, reg, (const uint8_t*)&value, 2, 400000);
}

void setupI2S1() {
    
    // PCM5102Aのリセットシーケンス
#ifdef I2S_USE_PCM5102A
    pinMode(I2S_XSMT_PIN, OUTPUT);
    digitalWrite(I2S_XSMT_PIN, LOW);
    delay(20); // 電源立ち上がり待ち（AVDD/DVDD安定しないとPLLロックしない事がある）
#endif

    // setup AW88298 regisgter
    M5.In_I2C.bitOn(AW88298_I2C_ADDR, 0x02, 0b00000100, 400000);
    writeRegister(0x61, 0x0673);
    writeRegister(0x04, 0x4040);
    writeRegister(0x05, 0x0008);
    switch (SAMPLE_RATE) {
        case 22050:
            writeRegister(0x06, 0b0001110000000100); // I2SCTL: 22.05kHz, 16bits, monoral (他は全部デフォルト値)
            break;
        case 44100:
            writeRegister(0x06, 0b0001110000000111); // I2SCTL: 44.1kHz, 16bits, monoral (他は全部デフォルト値)
            break;
        case 48000:
            writeRegister(0x06, 0b0001110000001000); // I2SCTL: 48kHz, 16bits, monoral (他は全部デフォルト値)
            break;
        default:
            writeRegister(0x06, 0b0001110000001000); // I2SCTL: 48kHz, 16bits, monoral (他は全部デフォルト値)
            break;
    }  
    writeRegister(0x0C, 0x5000 | 0x0064);    // VOLUME: -32.0dB(Test)

    // I2Sの初期化
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_BCK_PIN,
        .ws = I2S_WS_PIN,
        .dout = I2S_DATA_PIN,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
      },
    };
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    i2s_channel_enable(tx_handle);

#ifdef I2S_USE_PCM5102A
    // DACのミュート解除（I2S準備完了後）
    digitalWrite(I2S_XSMT_PIN, HIGH);
    delay(20);  // DAC内部回路の立ち上がり待ち
#endif

    Serial.println("I2S initialized");
}

// I2SのDMA処理
void TaskI2SMain(void *pvParameters) {

  Serial.println("TaskI2S started");

  for (;;) {
    int16_t *ptr;
    size_t bytes_written;
    esp_err_t err;
    // キューから受信（待機する）
    if (xQueueReceive(queueAudio, &ptr, portMAX_DELAY) == pdPASS) { // 受信するまで待機
      if (ptr != NULL) {
        err = i2s_channel_write(tx_handle, (char *)ptr, FRAME_SAMPLES * FRAME_BATCH, &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
          Serial.println("i2s_write failed");
        }
        heap_caps_free(ptr); // 解放
      }
      else {
        // 異常値は読み捨て
        Serial.println("Invalid buffer pointer: NULL");
      }
    }
    else {
      // キュー受信失敗時の処理
      Serial.println("Queue receive failed");
    }
  }
}
