#ifndef __UI_H__
#define __UI_H__

#include "main.h"

/**
 * @brief 界面页面
 */
typedef enum
{
  UI_PAGE_VITALS = 0, /* 心率 / 血氧 / 波形 */
  UI_PAGE_CLIMATE,    /* 温湿度 */
  UI_PAGE_LIGHT,      /* 光照 */
  UI_PAGE_MPU,        /* 加速度 */
  UI_PAGE_SPEED,      /* 转速 */
  UI_PAGE_COUNT
} UI_Page;

/**
 * @brief 初始化界面模块（清空状态，从第一页开始）
 */
void UI_Init(void);

/**
 * @brief 界面任务：自动轮播页面 + 定时刷屏
 * @note  在主循环里反复调用，内部按 40ms 节流（25fps）
 */
void UI_Task(void);

/**
 * @brief 手动切换页面（以后接按键时用）
 */
void UI_SetPage(UI_Page page);

/**
 * @brief 切到下一页
 */
void UI_NextPage(void);

/**
 * @brief 当前页面
 */
UI_Page UI_GetPage(void);

#endif // __UI_H__
