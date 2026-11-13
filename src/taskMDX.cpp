
#include <Arduino.h>
#include <SD.h>

#include "taskMDX.h"
#include "taskheader.h"
#include "mdx2wav/gamdx/mxdrvg/mxdrvg.h"

TaskHandle_t tskMDX;

const static size_t MAGIC_OFFSET = 10;  // MDXファイルの先頭からのオフセット（MXDRVGの仕様に合わせる）
const static size_t MDX_BUF_SIZE = 16;  // MDXバッファサイズ（MXDRVGの仕様に合わせる）
const static size_t PDX_BUF_SIZE = 16;  // PDXバッファサイズ（MXDRVGの仕様に合わせる）
const static uint32_t FILTER_MODE = 0;
const static uint32_t fadeout = 1;
const static uint32_t song_loop = 1;

static uint8_t *mdxData = nullptr;
static size_t mdxDataSize = 0;

static uint8_t *pdxData = nullptr;
static size_t pdxDataSize = 0;

static boolean mdxIsPlaying = false;

boolean ProcMDX(const char *mdx_name, char *title, int title_len);

boolean isPlaying(void) {
    return mdxIsPlaying;
}

void setupMDX(void) {
    // MDXの初期化処理
    MXDRVG_SetEmulationType(MXDRVG_YM2151TYPE_MAME);
    if (MXDRVG_Start(SAMPLE_RATE, FILTER_MODE, MDX_BUF_SIZE, PDX_BUF_SIZE)) {
        Serial.println("MXDRVG_Start failed");
        while (1);
    }
    MXDRVG_TotalVolume(256);

    Serial.println("MXDRVG initialized");
}

boolean mdxLoad(const char *mdxPath) {
    if (mdxData != nullptr) {
        mdxUnload();    // 既にロードされている場合は解放（再生も停止）
    }

    // MDXファイルをSDカードから読み込む
    Serial.printf("Loading %s\n", mdxPath);
    File mdxFile = SD.open(mdxPath, FILE_READ);
    if (!mdxFile) {
        Serial.println("Failed to open MDX file.");
        return false;
    }

    // ファイルサイズを取得
    mdxDataSize = mdxFile.size() + MAGIC_OFFSET;  // MXDRVGの仕様に合わせてオフセットを追加
    mdxData = (uint8_t *)heap_caps_malloc(mdxDataSize, MALLOC_CAP_8BIT);
    if (mdxData == nullptr) {
        Serial.println("Failed to allocate memory for MDX data.");
        mdxFile.close();
        return false;
    }

    // ファイル内容をバッファに読み込む
    mdxFile.read(mdxData + MAGIC_OFFSET, mdxDataSize - MAGIC_OFFSET);  // オフセット分を空けて読み込む（MXDRVGの仕様に合わせる）
    mdxFile.close();

    // MDXデータを処理する（ここでは仮の処理）
    // 実際にはmxdrvgライブラリを使用して音声再生などを行う
    Serial.printf("Loaded %zu bytes from %s\n", mdxDataSize, mdxPath);

    // MDXファイルを処理
    char title[256];
    if (ProcMDX(mdxPath, title, sizeof(title))) {
        Serial.println("Loading OK");
    } else {
        Serial.println("Failed to process MDX file.");
        mdxUnload();  // 処理に失敗した場合は解放
        return false;
    }

    return true;
}

void strcpy_cnv(char *dst, const char *src, int mode) {
  while (int c = *src++) {
    *dst++ =
      mode == 0 ? tolower(c) :
      mode == 1 ? toupper(c) :
      c;
  }
  *dst = 0;
}

boolean ProcMDX(const char *mdx_name, char *title, int title_len) {
  uint8_t *mdx_buf = mdxData;
  size_t mdx_size = mdxDataSize;

  // Skip title.
  int pos = MAGIC_OFFSET;  // MXDRVGの仕様に合わせてオフセットを追加
  {
    char *ptitle = title;
    while (pos < mdx_size && --title_len > 0) {
      *ptitle++ = mdx_buf[pos];
      if (mdx_buf[pos] == 0x0d && mdx_buf[pos + 1] == 0x0a)
        break;
      pos++;
    }
    *ptitle = 0;
  }

  while (pos < mdx_size) {
    uint8_t c = mdx_buf[pos++];
    if (c == 0x1a) break;
  }

  char *pdx_name = (char*) mdx_buf + pos;

  while (pos < mdx_size) {
    uint8_t c = mdx_buf[pos++];
    if (c == 0) break;
  }

  if (pos >= mdx_size)
    return false;

  // Get mdx path.
  if (*pdx_name) {
    char pdx_path[FILENAME_MAX];
    strncpy(pdx_path, mdx_name, sizeof(pdx_path));

    int pdx_name_start = 0;
    for (int i = strlen(pdx_path) - 1; i > 0; i--) {
      if (pdx_path[i - 1] == '/') {
        pdx_name_start = i;
        break;
      }
    }

    if (pdx_name_start + strlen(pdx_path) + 4 >= sizeof(pdx_path)) {
      return false;
    }

    // remove .pdx from pdx_name
    {
      int pdx_name_len = strlen(pdx_name);
      if (pdx_name_len > 4) {
        if (pdx_name[pdx_name_len - 4] == '.') {
          pdx_name[pdx_name_len - 4] = 0;
        }
      }
    }

    // Make pdx path.
    for (int i = 0; i < 3 * 2; i++) {
      strcpy_cnv(pdx_path + pdx_name_start, pdx_name, i % 3);
      strcpy_cnv(pdx_path + pdx_name_start + strlen(pdx_name), ".pdx", i / 3);
      Serial.printf("Loading %s\n", pdx_path);
      File pdxFile = SD.open(pdx_path, FILE_READ);
      if (!pdxFile) {
        Serial.println("Failed to open PDX file.");
        continue;  // 次の試行に進む
      }

      // ファイルサイズを取得
      pdxDataSize = pdxFile.size() + MAGIC_OFFSET;  // MXDRVGの仕様に合わせてオフセットを追加
      pdxData = (uint8_t *)heap_caps_malloc(pdxDataSize, MALLOC_CAP_8BIT);
      if (pdxData == nullptr) {
        Serial.println("Failed to allocate memory for PDX data.");
        pdxFile.close();
        break;  // メモリ不足のためループを抜ける
      }

      // ファイル内容をバッファに読み込む
      pdxFile.read(pdxData + MAGIC_OFFSET, pdxDataSize - MAGIC_OFFSET);  // オフセット分を空けて読み込む（MXDRVGの仕様に合わせる）
      pdxFile.close();
      Serial.printf("Loaded %zu bytes from %s\n", pdxDataSize, pdx_path);
      break;  // 成功した場合はループを抜ける
    }
  }

  // Convert mdx to MXDRVG readable structure.
  int mdx_body_pos = pos;

  uint8_t *mdx_head = mdx_buf + mdx_body_pos - MAGIC_OFFSET;  // MXDRVGの仕様に合わせてオフセットを追加
  mdx_head[0] = 0x00;
  mdx_head[1] = 0x00;
  mdx_head[2] = (pdxData ? 0 : 0xff);
  mdx_head[3] = (pdxData ? 0 : 0xff);
  mdx_head[4] = 0;
  mdx_head[5] = 0x0a;
  mdx_head[6] = 0x00;
  mdx_head[7] = 0x08;
  mdx_head[8] = 0x00;
  mdx_head[9] = 0x00;

  if (pdxData) {
    pdxData[0] = 0x00;
    pdxData[1] = 0x00;
    pdxData[2] = 0x00;
    pdxData[3] = 0x00;
    pdxData[4] = 0x00;
    pdxData[5] = 0x0a;
    pdxData[6] = 0x00;
    pdxData[7] = 0x02;
    pdxData[8] = 0x00;
    pdxData[9] = 0x00;
  }

  MXDRVG_SetData(mdx_head, mdx_size, pdxData, pdxDataSize);

  return true;
}

void mdxUnload(void) {
    if (mdxData != nullptr) {
        MXDRVG_Stop();  // 念のため再生停止
        // MDXバッファを解放
        free(mdxData);
        mdxData = nullptr;
        mdxDataSize = 0;
    }
    if (pdxData != nullptr) {
        // PDXバッファを解放
        free(pdxData);
        pdxData = nullptr;
        pdxDataSize = 0;
    }
}

void mdxPlay(void) {
    if (mdxData == nullptr) {
        Serial.println("MDX buffer is not loaded.");
        return;
    }

   MXDRVG_PlayAt(0, song_loop, fadeout);
   mdxIsPlaying = true;
}

void TaskMDXMain(void *pvParameters) {

//#define AUDIO_DEBUG
#ifdef AUDIO_DEBUG  // チューニング用の計測変数
    extern uint32_t opm_duration_us;
    extern uint32_t pcm8_duration_us;
    extern uint32_t downsample_duration_us;

    static uint32_t meas_count = 0;
    static uint32_t opm_duration_max = 0;
    static uint32_t pcm8_duration_max = 0;
    static uint32_t downsample_duration_max = 0;
#endif

    Serial.println("TaskMDX started");

    for (;;) {
        if (!mdxIsPlaying || MXDRVG_GetTerminated()) {
            mdxIsPlaying = false;
            Serial.println("Playback terminated.");
            delay(10);    // 再生終了後に少し待機
            continue;
        }

        // メモリプールからオーディオバッファを確保
        int16_t *ptr = (int16_t *)heap_caps_malloc(FRAME_SAMPLES * FRAME_BATCH, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (ptr == NULL) {
            // メモリ確保失敗時の処理
            // メッセージキューの数が多すぎて確保できない場合を想定（音切れが無いならば減らすと良いはず）
            Serial.println("Audio buffer malloc error");
            delay(10);
            continue;
        }

        // MXDRVGからオーディオデータを取得
        size_t len = MXDRVG_GetPCM(ptr, FRAME_BATCH);

#ifdef AUDIO_DEBUG
        if (opm_duration_us > opm_duration_max) {
            opm_duration_max = opm_duration_us;
        }
        if (pcm8_duration_us > pcm8_duration_max) {
            pcm8_duration_max = pcm8_duration_us;
        }
        if (downsample_duration_us > downsample_duration_max) {
            downsample_duration_max = downsample_duration_us;
        }
        if (meas_count++ % 500 == 0) {
            Serial.printf("MXDRVG_GetPCM OPM: %d[us], PCM8: %d[us], D/S: %d[us]\r\n",opm_duration_max, pcm8_duration_max, downsample_duration_max);
            opm_duration_max = 0;
            pcm8_duration_max = 0;
            downsample_duration_max = 0;
        }
#endif
        if (len <= 0) {
            // 再生終了時の処理
            heap_caps_free(ptr);  // 確保したバッファを解放
            Serial.println("MXDRVG_GetPCM returned 0 or negative length");
            delay(1000);
            continue;
        }

        // レンダリング済みのバッファIndexをキューにつなぐ
        xQueueSend(queueAudio, &ptr, portMAX_DELAY);  // キューがいっぱいなら待機する
    }
}
