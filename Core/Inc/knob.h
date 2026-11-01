#ifndef __KNOB_H__
#define __KNOB_H__

#include "main.h"

/* 电位器滑片：PA4 = ADC1_IN4（PA0 已被光敏电阻占用） */
#define KNOB_ADC_PORT  GPIOA
#define KNOB_ADC_PIN   GPIO_PIN_4
#define KNOB_ADC_CH    4

/* 旋钮分几档（等于界面页数） */
#define KNOB_PAGES     5

/**
 * @brief 初始化电位器引脚（模拟输入）
 */
void Knob_Init(void);

/**
 * @brief 扫描旋钮，建议每 50ms 调用一次
 * @return 1 表示页码发生了变化，新页码用 Knob_GetPage() 取
 * @note  内部做了 3 次连续一致的判断，边界上不会来回跳
 */
uint8_t Knob_Scan(void);

/**
 * @brief 当前档位（0 ~ KNOB_PAGES-1）
 */
uint8_t Knob_GetPage(void);

/**
 * @brief 最近一次采样的 ADC 原始值（0~4095），排查用
 */
uint16_t Knob_GetRaw(void);

#endif // __KNOB_H__
