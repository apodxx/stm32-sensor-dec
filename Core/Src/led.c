/**
 * @file led.c
 * @brief 一个受控的 LED / 灯（PA4，推挽输出）
 *
 * 语音"开灯"最终会走到这里：
 *   ESP32 → UART "LED ON" → link.c → Led_Set(1)
 */
#include "led.h"

static uint8_t ledOn;

void Led_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  g.Pin = LED_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &g);

  Led_Set(0); /* 上电默认熄灭 */
}

void Led_Set(uint8_t on)
{
  ledOn = on ? 1 : 0;

#if LED_ACTIVE_LOW
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, ledOn ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, ledOn ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
}

void Led_Toggle(void)
{
  Led_Set((uint8_t)!ledOn);
}

uint8_t Led_Get(void)
{
  return ledOn;
}
