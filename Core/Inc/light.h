#ifndef __LIGHT_H__
#define __LIGHT_H__

#include "main.h"

/* 模拟输入引脚：PA0 = ADC1_IN0，换引脚需同时改 light.c 里的通道号 */
#define LIGHT_ADC_PORT   GPIOA
#define LIGHT_ADC_PIN    GPIO_PIN_0
#define LIGHT_ADC_CH     0

/**
 * @brief 光照数据（只读）
 */
typedef struct
{
  uint16_t raw;       /* ADC 原始值 0~4095 */
  uint16_t millivolt; /* 换算电压 0~3300 mV */
  uint8_t  percent;   /* 0~100，按人眼感觉做了分段映射，不是线性电压 */
  uint8_t  valid;     /* 1 = 最近一次转换正常 */
} Light_Data;

/**
 * @brief 初始化 PA0 为模拟输入并启动 ADC1
 * @note  直接操作寄存器，不依赖 HAL 的 ADC 驱动模块
 */
void Light_Init(void);

/**
 * @brief 采样任务：8 次平均，建议每 200ms 调用一次
 */
void Light_Task(void);

/**
 * @brief 获取最新数据
 */
const Light_Data *Light_GetData(void);

#endif // __LIGHT_H__
