/**
 * @file light.c
 * @brief GL5516 光敏电阻驱动（ADC1_IN0 / PA0）
 *
 * 接线（分压）：
 *      3.3V ──[GL5516]──┬── PA0
 *                       │
 *                    [10kΩ]
 *                       │
 *                      GND
 *
 * 光照越强 -> 光敏电阻阻值越小 -> PA0 电压越高 -> ADC 值越大。
 *
 * ADC 配置：12 位、单次转换、239.5 周期采样（适配高阻信号源）。
 * 这里直接写寄存器而不用 HAL 的 ADC 模块，避免额外添加驱动文件。
 */
#include "light.h"
#include "adc1.h"

static Light_Data light;

/* ============================ ADC 底层 ============================ */
/**
 * @brief 把 PA0 配成模拟输入并初始化 ADC1
 * @note  ADC 外设本身由 adc1.c 统一管理，这里只负责引脚
 */
static void Light_AdcInit(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  g.Pin = LIGHT_ADC_PIN;
  g.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(LIGHT_ADC_PORT, &g);

  ADC1_Init();
}

/* ============================ 亮度分段映射 ============================ */
/**
 * @brief ADC -> 0~100 的亮度百分比
 * @note  光敏电阻的阻值随照度呈指数变化，直接按电压线性映射会让室内灯光
 *        全部挤在 80%~95%。这里用几个标定点做分段线性，把常用区间拉开。
 *        大致对应：10% ≈ 1 lux，40% ≈ 10 lux，70% ≈ 100 lux，100% ≈ 1000 lux
 *        （GL5516 + 10k 分压的典型值，个体有差异）
 */
static uint8_t Light_Percent(uint16_t adc)
{
  static const uint16_t adcPt[5] = {0, 960, 2410, 3560, 3970};
  static const uint8_t  pctPt[5] = {0, 10, 40, 70, 100};

  if (adc >= adcPt[4])
    return 100;

  for (uint8_t i = 0; i < 4; i++)
  {
    if (adc < adcPt[i + 1])
    {
      uint16_t spanAdc = (uint16_t)(adcPt[i + 1] - adcPt[i]);
      uint16_t spanPct = (uint16_t)(pctPt[i + 1] - pctPt[i]);
      return (uint8_t)(pctPt[i] + (uint16_t)((uint32_t)(adc - adcPt[i]) * spanPct / spanAdc));
    }
  }
  return 100;
}

/* ============================ 对外接口 ============================ */
void Light_Init(void)
{
  Light_AdcInit();

  light.raw = 0;
  light.millivolt = 0;
  light.percent = 0;
  light.valid = 0;
}

void Light_Task(void)
{
  uint32_t sum = 0;
  uint16_t raw;

  /* 8 次平均，抑制工频干扰和随机噪声 */
  for (uint8_t i = 0; i < 8; i++)
    sum += ADC1_Read(LIGHT_ADC_CH);

  raw = (uint16_t)(sum / 8);

  light.raw = raw;
  light.millivolt = (uint16_t)((uint32_t)raw * 3300U / 4095U);
  light.percent = Light_Percent(raw);
  light.valid = 1;
}

const Light_Data *Light_GetData(void)
{
  return &light;
}
