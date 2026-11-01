#ifndef __SPEED_H__
#define __SPEED_H__

#include "main.h"

/* 信号脚：PB1（EXTI1）。PB1 是 5V 容忍脚，模块用 5V 供电也安全 */
#define SPEED_PORT   GPIOB
#define SPEED_PIN    GPIO_PIN_1

/* 码盘槽数：常见的 20 槽码盘；换成别的槽数只改这里 */
#define SPEED_SLOTS  20

/* 测速窗口（毫秒） */
#define SPEED_WINDOW_MS  500

/**
 * @brief 测速数据（只读）
 */
typedef struct
{
  uint16_t rpm;           /* 转速（转/分） */
  uint16_t pulsesPerSec;  /* 每秒脉冲数，低转速时看这个更直观 */
  uint32_t totalPulses;   /* 累计脉冲数 */
  uint16_t lastCount;     /* 上一个窗口内的脉冲数（排查用） */
  uint8_t  level;         /* DO 引脚当前电平（排查用） */
  uint8_t  active;        /* 1 = 最近一个窗口内有脉冲 */
} Speed_Data;

/**
 * @brief 初始化 PB1 为下降沿中断
 * @return 0 成功
 */
uint8_t Speed_Init(void);

/**
 * @brief 每 100ms 调用一次，内部按 500ms 窗口计算转速
 */
void Speed_Task(void);

/**
 * @brief 获取最新数据
 */
const Speed_Data *Speed_GetData(void);

/**
 * @brief EXTI 分发入口，由 HAL_GPIO_EXTI_Callback 调用
 */
void Speed_OnExti(uint16_t pin);

#endif // __SPEED_H__
