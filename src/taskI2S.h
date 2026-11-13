#ifndef _TASKI2S_H
#define _TASKI2S_H

// グローバル定義
extern TaskHandle_t tskI2S;

// 関数プロトタイプ
extern void setupI2S1(void);
extern void TaskI2SMain(void *pvParameters);

#endif