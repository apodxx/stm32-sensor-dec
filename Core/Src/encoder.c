/**
 * @file encoder.c
 * @brief 增量式旋转编码器（360° 旋钮，A/B 两相）
 *
 * 接线（模块上的 5 个脚）：
 *   GND -> GND
 *   VCC -> 3.3V
 *   A   -> PA5
 *   B   -> PA6
 *   C   -> GND（多数模块的 C 是公共端；若是按键脚，接地只是让按键失效）
 *
 * 解码：A、B 相位差 90°，用状态表做 4 倍频，一个机械档位产生 4 个计数。
 *       状态表对“抖动跳变”天然免疫：非法跳变计 0，不会累加错误。
 *
 * @note 如果模块没有板载上拉，这里已经开了内部上拉（约 40k），短线下可用。
 */
#include "encoder.h"

static volatile int16_t encCount;
static volatile uint8_t encPrev; /* (A<<1)|B */

void Encoder_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  g.Pin = ENC_A_PIN | ENC_B_PIN;
  g.Mode = GPIO_MODE_IT_RISING_FALLING; /* 两相都双边沿触发 */
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ENC_A_PORT, &g);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  encCount = 0;
  encPrev = (uint8_t)((HAL_GPIO_ReadPin(ENC_A_PORT, ENC_A_PIN) << 1) |
                      HAL_GPIO_ReadPin(ENC_B_PORT, ENC_B_PIN));
}

void Encoder_OnExti(uint16_t pin)
{
  /* index = (上一状态 << 2) | 当前状态；值为 ±1 表示有效步进 */
  static const int8_t table[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0};
  uint8_t cur;

  if (pin != ENC_A_PIN && pin != ENC_B_PIN)
    return;

  cur = (uint8_t)((HAL_GPIO_ReadPin(ENC_A_PORT, ENC_A_PIN) << 1) |
                  HAL_GPIO_ReadPin(ENC_B_PORT, ENC_B_PIN));

  encCount += table[(encPrev << 2) | cur];
  encPrev = cur;
}

/**
 * @brief EXTI 5~9 共用中断入口（PA5、PA6 都在这一组）
 */
void EXTI9_5_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(ENC_A_PIN);
  HAL_GPIO_EXTI_IRQHandler(ENC_B_PIN);
}

int16_t Encoder_GetCount(void)
{
  return encCount;
}

int16_t Encoder_GetDetents(void)
{
  return (int16_t)(encCount / 4);
}

void Encoder_Reset(void)
{
  encCount = 0;
}
