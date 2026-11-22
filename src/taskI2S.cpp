// I2S制御タスク

#include <M5Unified.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>

#include <ModuleAudio/M5Module_Audio.h>
static M5ModuleAudio moduleAudio;

#include "taskI2S.h"
#include "taskheader.h"

// 特殊用途
//#define I2S_USE_PCM5102A

#define CODEC_STM_ADDR  0x33    // Audio HatのSTM32のI2Cアドレス
#define INTSP_I2C_ADDR  0x36    // AW88298(内蔵スピーカー)のCODECのI2Cアドレス
#define I2S_VOL_MAX     100     // ボリューム値の最大
#define I2S_VOL_MIN     0       // ボリューム値の最小
#define I2S_VOL_STEP    5       // ボリュームの変更量

#ifdef I2S_USE_PCM5102A
  #define I2S_XSMT_PIN  12  // GPIO-12
#endif

TaskHandle_t tskI2S;
QueueHandle_t queueAudio;
i2s_chan_handle_t tx_handle = NULL;

// 出力先ごとに音量を独立して管理する（0～100）。
static uint8_t headphoneVolume = 50;
static uint8_t internalSpeakerVolume = 70;

// 出力先は同時初期化せず、切り替え時に現在のI2Sを解放する。
static bool outputReady = false;        // 出力の切り替え中はfalseとなる
static bool headphoneOutput = false;    // ヘッドホン出力の場合はtrueとなる

// AW88298のレジスタ設定(16bit長レジスタ)
static void writeRegister(uint8_t reg, uint16_t value)
{
    value = __builtin_bswap16(value);
    M5.In_I2C.writeRegister(INTSP_I2C_ADDR, reg, (const uint8_t*)&value, 2, 400000);
}

// AW88298の音量レジスタ値へ変換する（0～100 → 0xF0～0x00）。
static uint8_t speakerVolume(uint8_t value)
{
    return (uint8_t)(0xF0 - (uint16_t)value * 0xF0 / 100);
}

void setVolume(uint8_t value)
{
    uint8_t volume = (value > I2S_VOL_MAX) ? I2S_VOL_MAX : value;
    if (headphoneOutput) {
        headphoneVolume = volume;
    } else {
        internalSpeakerVolume = volume;
    }

    if (!outputReady) return;

    if (headphoneOutput) {
        moduleAudio.setSpeakerVolume(headphoneVolume);
    } else {
        uint16_t regValue = (uint16_t(speakerVolume(internalSpeakerVolume)) << 8) | 0x0064;
        writeRegister(0x0C, regValue);
    }
}

uint8_t getVolume()
{
    return headphoneOutput ? headphoneVolume : internalSpeakerVolume;
}

void volumeUp()
{
    uint8_t volume = getVolume();
    setVolume(volume >= I2S_VOL_MAX - I2S_VOL_STEP ? I2S_VOL_MAX : volume + I2S_VOL_STEP);
}

void volumeDown()
{
    uint8_t volume = getVolume();
    setVolume(volume <= I2S_VOL_STEP ? I2S_VOL_MIN : volume - I2S_VOL_STEP);
}

static bool setupModuleAudio()
{
    // M144のピン順序は正常再生できていた元の指定を維持する。
    if (!moduleAudio.begin(M5.In_I2C, CODEC_STM_ADDR, 400000, SAMPLE_RATE,
                           GPIO_NUM_7, GPIO_NUM_14, GPIO_NUM_6,
                           GPIO_NUM_13, GPIO_NUM_0)) {
        Serial.println("Module Audio initialization failed");
        return false;
    }
    moduleAudio.setHPMode(AUDIO_HPMODE_NATIONAL);
    moduleAudio.setSpeakerOutput(DAC_OUTPUT_OUT1);
    moduleAudio.setSpeakerVolume(headphoneVolume);
    moduleAudio.setBitsSample(ES_MODULE_ADC_DAC, BIT_LENGTH_16BITS);
    moduleAudio.setSampleRate(SAMPLE_RATE_48K);
    moduleAudio.setMute(false);
    Serial.println("Module Audio initialized (official I2S)");
    return true;
}

static bool setupInternalSpeaker()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
        Serial.printf("Speaker i2s_new_channel failed: %d\n", (int)err);
        tx_handle = NULL;
        return false;
    }
    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = GPIO_NUM_34,
        .ws = GPIO_NUM_33,
        .dout = GPIO_NUM_13,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
      },
    };
    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err == ESP_OK) err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
        Serial.printf("Speaker I2S initialization failed: %d\n", (int)err);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return false;
    }

    // setup AW88298 regisgter
    M5.In_I2C.bitOn(INTSP_I2C_ADDR, 0x02, 0b00000100, 400000);
    writeRegister(0x61, 0x0673);
    writeRegister(0x04, 0x4040);
    writeRegister(0x05, 0x0008);
    switch (SAMPLE_RATE) {
        case 22050: writeRegister(0x06, 0b0001110000000100); break;
        case 44100: writeRegister(0x06, 0b0001110000000111); break;
        case 48000: writeRegister(0x06, 0b0001110000001000); break;
        default:    writeRegister(0x06, 0b0001110000001000); break;
    }
    writeRegister(0x0C, (uint16_t(speakerVolume(internalSpeakerVolume)) << 8) | 0x0064);
    Serial.println("Internal speaker I2S initialized");
    return true;
}

static bool switchAudioOutput(bool inserted)
{
    if (outputReady && inserted == headphoneOutput) return true;

    if (outputReady) {
        if (headphoneOutput) {
            moduleAudio.setMute(true);
            if (!moduleAudio.end()) {   // 公式ライブラリを拡張
                Serial.println("Module Audio end() failed");
                return false;
            }
            Serial.println("Module Audio I2S released");
        } else if (tx_handle != NULL) {
            i2s_channel_disable(tx_handle);
            i2s_del_channel(tx_handle);
            tx_handle = NULL;
            Serial.println("Internal speaker I2S released");
        }
        outputReady = false;
    }

    headphoneOutput = inserted;
    outputReady = inserted ? setupModuleAudio() : setupInternalSpeaker();
    Serial.printf("Audio output: %s (%s)\n",
                  inserted ? "HEADPHONE" : "SPEAKER",
                  outputReady ? "READY" : "FAILED");
    return outputReady;
}

void setupI2S1() {
    // 初期化時点のヘッドホン装着状態に合わせて片方だけ起動する。
    // getHPInsertStatus() のI2C情報はbegin()で設定されるため、まずM144を起動する。
    headphoneOutput = true;
    outputReady = setupModuleAudio();
    if (outputReady) {
        bool inserted = moduleAudio.getHPInsertStatus();
        Serial.printf("Headphone at startup: %s\n", inserted ? "INSERTED" : "REMOVED");
        if (!inserted) switchAudioOutput(false);
    }
    Serial.println("I2S initialized");
}

// I2SのDMA処理
void TaskI2SMain(void *pvParameters) {

    Serial.println("TaskI2S started");

    // 256ステレオフレーム相当の無音データ
    static int16_t silence[FRAME_SAMPLES * FRAME_BATCH / sizeof(int16_t)] = {0};

    for (;;) {
        int16_t *ptr = nullptr;
        size_t bytes_written = 0;
        bool writeOK = false;
        static uint32_t lastCheck = 0;

        // ヘッドホン挿抜の監視
        if (millis() - lastCheck >= 100) {
            lastCheck = millis();
            bool inserted = moduleAudio.getHPInsertStatus();
            if (inserted != headphoneOutput || !outputReady) {
                Serial.printf("Headphone: %s\n", inserted ? "INSERTED" : "REMOVED");
                switchAudioOutput(inserted);
            }
        }

        // PCMがなければ無音を送る
        if (xQueueReceive(queueAudio, &ptr, 0) == pdPASS && ptr != nullptr) {
            if (outputReady && headphoneOutput) {
                writeOK = moduleAudio.play((const uint8_t*)ptr, FRAME_SAMPLES * FRAME_BATCH);
            } else if (outputReady && tx_handle != NULL) {
                writeOK = (i2s_channel_write(
                tx_handle,
                ptr,
                FRAME_SAMPLES * FRAME_BATCH,
                &bytes_written,
                portMAX_DELAY
                ) == ESP_OK);
            }
            heap_caps_free(ptr);
        } else {
            if (outputReady && headphoneOutput) {
                writeOK = moduleAudio.play((const uint8_t*)silence, sizeof(silence));
            } else if (outputReady && tx_handle != NULL) {
                writeOK = (i2s_channel_write(
                tx_handle,
                silence,
                sizeof(silence),
                &bytes_written,
                portMAX_DELAY
                ) == ESP_OK);
            }
        }

        if (outputReady && !writeOK) {
            Serial.println("I2S write failed");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1)); // エラー時のCPU占有・WDTを防ぐ（念のため）
    }
}
