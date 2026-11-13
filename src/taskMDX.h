#ifndef _TASKMDX_H
#define _TASKMDX_H

// グローバル定義
extern TaskHandle_t tskMDX;

// 関数プロトタイプ
boolean isPlaying(void);
extern void setupMDX(void);
extern boolean mdxLoad(const char *mdxPath);
extern void mdxUnload(void);
extern void mdxPlay(void);

extern void TaskMDXMain(void *pvParameters);

#endif
