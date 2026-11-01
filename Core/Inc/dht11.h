#ifndef __DHT11_H__
#define __DHT11_H__

#include "main.h"

/* 数据引脚：默认 PB0，换引脚只改这两行 */
#define DHT11_PORT   GPIOB
#define DHT11_PIN    GPIO_PIN_0

/* 两次读取的最小间隔（DHT11 规定 >= 1s，这里放宽到 2s 更稳） */
#define DHT11_PERIOD_MS  2000

/**
 * @brief 温湿度数据（只读）
 */
typedef struct
{
  int8_t   temperature; /* 摄氏度，DHT11 量程 0~50，可能为负 */
  uint8_t  humidity;    /* 相对湿度 %RH */
  uint8_t  valid;       /* 1 = 数据有效 */
  uint8_t  failCnt;     /* 连续失败次数 */
  uint32_t lastOkTick;  /* 上次读取成功的时刻 */
} DHT11_Data;

/**
 * @brief 初始化数据引脚（开漏输出 + 上拉），并启用 DWT 微秒延时
 */
void DHT11_Init(void);

/**
 * @brief 周期任务：内部限速 2s 读一次
 * @note  建议在主循环里每 1s 调用一次即可
 */
void DHT11_Task(void);

/**
 * @brief 获取最新数据
 */
const DHT11_Data *DHT11_GetData(void);

#endif // __DHT11_H__
