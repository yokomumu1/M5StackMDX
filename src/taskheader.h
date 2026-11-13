// タスク間で共有するオブジェクトの定義
#ifndef _TASKHEADER_H
#define _TASKHEADER_H

#include <Arduino.h>

// オーディオ関連定義
#define MSG_QUEUE_NUM   8   // メッセージキューの数（メモリ確保の関係でFRAME_SAMPLES以下にすること）
#define SAMPLE_RATE     48000                           // サンプルレート（Hz）
#define CHANNEL_NUM     2                               // チャンネル数（ステレオ）
#define FRAME_SAMPLES   (sizeof(int16_t) * CHANNEL_NUM) // Number of samples per frame
#define FRAME_BATCH     256                              // まとめてDMA転送するフレーム数

extern QueueHandle_t queueAudio;    // AudioタスクとI2Sタスクで共有するキューの定義

#endif
