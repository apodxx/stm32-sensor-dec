/**
 * @file buzzer.c
 * @brief 有源蜂鸣器驱动（PA2）
 *
 * 有源蜂鸣器内部自带振荡电路，给电就响，不需要 PWM 或频率信号。
 * 这里只做三件事：初始化引脚、开关、非阻塞定时关闭。
 *
 * 接线（3 针模块）：
 *   VCC -> 5V（有些模块支持 3.3V）
 *   GND -> GND
 *   I/O -> PA2
 *
 * 如果是 2 脚裸蜂鸣器：不能直接接 GPIO（电流可能超 20mA），
 * 要用一个 NPN 三极管（S8050/2N3904）驱动：
 *   PA2 --[1k]-- B
 *   E -> GND
 *   C -> 蜂鸣器负极，蜂鸣器正极 -> 5V
 *   蜂鸣器两端反并一个 1N4148 续流二极管
 */
#include "buzzer.h"

static uint8_t  beeping;
static uint32_t offTick;

void Buzzer_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  g.Pin = BUZZER_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZZER_PORT, &g);

  Buzzer_Off();
}

void Buzzer_On(void)
{
#if BUZZER_ACTIVE_LOW
  HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
#endif
}

void Buzzer_Off(void)
{
#if BUZZER_ACTIVE_LOW
  HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#endif
}

void Buzzer_Beep(uint16_t ms)
{
  Buzzer_On();
  beeping = 1;
  offTick = HAL_GetTick() + ms;
}

void Buzzer_Task(void)
{
  if (beeping && (int32_t)(HAL_GetTick() - offTick) >= 0)
  {
    Buzzer_Off();
    beeping = 0;
  }
}
