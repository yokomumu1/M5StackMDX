#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <vector>
#include <iostream>

#include "taskheader.h"
#include "taskI2S.h"
#include "taskMDX.h"

void WaitForSDCardInsert(void);
void ListupMDXFiles(const char *path);

std::vector<std::string> MDXFileList;
int32_t currentFileIndex = 0;

const static char *TAG = "MAIN";

void setup() {
  M5.begin();
  M5.Display.setFont(&fonts::FreeMono12pt7b);
  M5.Display.setTextSize(1);
  Serial.begin(115200);

  // AudioタスクとI2Sタスクで共有するキューの作成
  queueAudio = xQueueCreate(MSG_QUEUE_NUM, sizeof(int16_t *));
  if (queueAudio == NULL) {
    ESP_LOGE(TAG, "Error creating the queue");
    while (1);
  }

  setupI2S1();  // I2Sの初期化
  setupMDX();   // MDXの初期化

  WaitForSDCardInsert();  // SDカードが挿入されるまで待機
  ListupMDXFiles("/");  // SDカード内のファイルをリストアップ

  M5.Display.println("MDX Player");

  // I2Sの処理 => Core 0に割り当て
  xTaskCreatePinnedToCore(
    TaskI2SMain,   // タスクのメイン関数
    "TaskI2S",     // タスク名
    2 * 1024,      // スタックサイズ（バイト単位）
    NULL,          // 引数
    5,             // 優先度
    &tskI2S,       // タスクハンドル
    0              // Core 0
  );

  // MDXの処理 => Core 1に割り当て
  xTaskCreatePinnedToCore(
    TaskMDXMain,   // タスクのメイン関数
    "TaskMDX",     // タスク名
    4 * 1024,      // スタックサイズ（バイト単位）
    NULL,          // 引数
    3,             // 優先度(High priority for audio processing)
    &tskMDX,       // タスクハンドル
    1              // Core 1
  );

  // MDXファイルが見つかった場合、最初のファイルをロード
  if (mdxLoad(MDXFileList[currentFileIndex].c_str())) {
    mdxPlay();  // 再生開始
    M5.Display.printf("Playing: %s\n", MDXFileList[currentFileIndex].c_str());
  } else {
    M5.Display.println("Failed to load MDX file.");
  }
}

void loop() {
  M5.update();  // M5.update()を呼び出して、ボタンの状態を更新

  if (!isPlaying()) {
    // 再生が終了した場合、次のMDXファイルをロードして再生
    currentFileIndex++;
    if (currentFileIndex >= MDXFileList.size()) {
      currentFileIndex = 0;  // 最初のファイルに戻る
    }

    M5.Display.clearDisplay();
    M5.Display.setCursor(0, 0);
    M5.Display.println("MDX Player");
    if (mdxLoad(MDXFileList[currentFileIndex].c_str())) {
      M5.Display.printf("Playing: %s\n", MDXFileList[currentFileIndex].c_str());
      mdxPlay();
    } else {
      M5.Display.printf("Failed to load: %s\n", MDXFileList[currentFileIndex].c_str());
      delay(2000);  // エラー表示のため少し待機
    }
  }
}

#define SD_SPI_CS_PIN 4   // M5Stack Core S3のSDカードのCSピン番号

void WaitForSDCardInsert(void) {
    uint32_t count = 0;
    boolean SDEnable = false;

    do {
        // SDカードのマウント
        if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000, "/sd")) {
            M5.Display.clearDisplay();
            if (count % 2 == 0) {
                M5.Display.setCursor(0, 0);
                M5.Display.print("Insert SD Card");
            }
            delay(500);
            count++;
        }
        else {
            SDEnable = true;
            M5.Display.clearDisplay();
        }
    } while (!SDEnable);
}

void ListupMDXFiles(const char *path) {
    File root = SD.open(path);
    if (!root) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        ESP_LOGE(TAG, "Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
          char *ext = strrchr(file.name(), '.');
          if (ext != nullptr) {
            if (strcasecmp(ext, ".mdx") == 0) {
                String pathName = "/" + String(file.name());
                MDXFileList.push_back(pathName.c_str());
            }
          }
        }
        file = root.openNextFile();
    }
}
