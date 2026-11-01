/**
 * @file knob.c
 * @brief 360° 旋转电位器（PA4 / ADC1_IN4），用来切换界面页面
 *
 * 接线（电位器三个脚）：
 *   一端   -> 3.3V
 *   另一端 -> GND
 *   滑片   -> PA4
 *   建议滑片对 GND 并 100nF 电容，旋钮读数更稳
 *
 * 换算：ADC 0~4095 平均分成 KNOB_PAGES 段，每段对应一页。
 *       如果旋钮方向反了，把 3.3V 和 GND 两个头对调即可。
 */
#include "knob.h"
#include "adc1.h"

static uint8_t  page;      /* 当前档位 */
static uint8_t  cand;      /* 候选档位 */
static uint8_t  candCnt;   /* 候选连续出现次数 */
static uint16_t raw;       /* 最近一次采样值 */

void Knob_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  g.Pin = KNOB_ADC_PIN;
  g.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(KNOB_ADC_PORT, &g);

  ADC1_Init();

  page = 0;
  cand = 0;
  candCnt = 0;
  raw = 0;
}

uint8_t Knob_Scan(void)
{
  uint32_t sum = 0;
  uint8_t p;

  /* 4 次平均，抑制抖动 */
  for (uint8_t i = 0; i < 4; i++)
    sum += ADC1_Read(KNOB_ADC_CH);

  raw = (uint16_t)(sum / 4);

  /* 0~4095 映射到 0~(KNOB_PAGES-1) */
  p = (uint8_t)((uint32_t)raw * KNOB_PAGES / 4096);
  if (p >= KNOB_PAGES)
    p = KNOB_PAGES - 1;

  if (p != cand)
  {
    cand = p;
    candCnt = 0;
    return 0;
  }

  /* 连续 3 次落在同一档才切换，防止在边界上反复横跳 */
  if (p != page && ++candCnt >= 3)
  {
    page = p;
    candCnt = 0;
    return 1;
  }

  return 0;
}

uint8_t Knob_GetPage(void)
{
  return page;
}

uint16_t Knob_GetRaw(void)
{
  return raw;
}
