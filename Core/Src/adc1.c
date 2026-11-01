/**
 * @file adc1.c
 * @brief ADC1 公共驱动（光敏电阻、旋钮电位器共用）
 *
 * 直接操作寄存器，不依赖 HAL 的 ADC 模块，所以不需要在 CubeMX 里勾选 ADC，
 * 也不用打开 HAL_ADC_MODULE_ENABLED。
 *
 * 用法：
 *   1. 各自把引脚配成模拟输入（HAL_GPIO_Init，GPIO_MODE_ANALOG）
 *   2. ADC1_Init() 只需调用一次
 *   3. ADC1_Read(通道号) 做单次转换
 */
#include "adc1.h"

static uint8_t adcInited;

void ADC1_Init(void)
{
  if (adcInited)
    return;

  __HAL_RCC_ADC1_CLK_ENABLE();

  /* ADC 时钟 = PCLK2/6 = 12MHz（上限 14MHz） */
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV6;

  ADC1->CR1 = 0;                /* 独立模式，12 位 */
  ADC1->CR2 = 0;                /* 单次转换，右对齐，软件触发 */
  ADC1->SMPR2 = ADC_SMPR2_SMP0; /* 先用最长采样时间，Read 时会重设 */
  ADC1->SQR1 = 0;               /* 规则组 1 个转换 */

  /* 上电并等待稳定 */
  ADC1->CR2 |= ADC_CR2_ADON;
  for (volatile uint32_t i = 0; i < 2000; i++)
  {
  }

  /* 校准 */
  ADC1->CR2 |= ADC_CR2_RSTCAL;
  while (ADC1->CR2 & ADC_CR2_RSTCAL)
  {
  }
  ADC1->CR2 |= ADC_CR2_CAL;
  while (ADC1->CR2 & ADC_CR2_CAL)
  {
  }

  adcInited = 1;
}

uint16_t ADC1_Read(uint8_t channel)
{
  uint32_t guard = 200000;

  /* 选择通道 + 采样时间 239.5 周期（适配高阻信号源） */
  ADC1->SQR3 = channel;
  if (channel <= 9)
    ADC1->SMPR2 |= (7UL << (channel * 3));
  else
    ADC1->SMPR1 |= (7UL << ((channel - 10) * 3));

  ADC1->CR2 |= ADC_CR2_ADON; /* 再次置位 = 启动一次转换 */

  while (!(ADC1->SR & ADC_SR_EOC))
  {
    if (--guard == 0)
      return 0;
  }

  return (uint16_t)ADC1->DR; /* 读 DR 会清 EOC */
}
