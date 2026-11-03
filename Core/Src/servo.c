/**
 * @file servo.c
 * @brief SG90 舵机驱动（TIM2_CH2 / PA1，50Hz PWM）
 *
 * 原理：
 *   SG90 每 20ms 收到一个脉冲，脉宽决定角度：
 *     0.5ms ->   0°
 *     1.5ms ->  90°
 *     2.5ms -> 180°
 *   所以用 72MHz 定时器：预分频 72 -> 1MHz 计数（1us 分辨率），
 *   ARR = 20000 -> 20ms 周期，CCR = 脉宽 us 数。
 *
 * 接线：
 *   棕/黑 -> GND
 *   红    -> 5V（建议外部 5V，务必共地）
 *   橙/黄 -> PA1
 *
 * @note 舵机堵转电流可达 700mA，不要接 3.3V，也不要用 ST-Link 供电。
 */
#include "servo.h"

static TIM_HandleTypeDef htim2;

void Servo_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();

  /* PA1 复用推挽输出 */
  GPIO_InitStruct.Pin = SERVO_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SERVO_GPIO_PORT, &GPIO_InitStruct);

  /* 72MHz / 72 = 1MHz，计数 20000 次 = 20ms */
  htim2.Instance = SERVO_TIM;
  htim2.Init.Prescaler = 72 - 1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 20000 - 1;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = SERVO_PULSE_MIN_US; /* 上电默认 0°（闸门关闭），不主动动 */
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, SERVO_TIM_CHANNEL) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_TIM_PWM_Start(&htim2, SERVO_TIM_CHANNEL);
}

void Servo_SetPulse(uint16_t us)
{
  if (us < SERVO_PULSE_MIN_US)
    us = SERVO_PULSE_MIN_US;
  if (us > SERVO_PULSE_MAX_US)
    us = SERVO_PULSE_MAX_US;

  __HAL_TIM_SET_COMPARE(&htim2, SERVO_TIM_CHANNEL, us);
}

void Servo_SetAngle(uint8_t deg)
{
  uint32_t us;

  if (deg > 180)
    deg = 180;

  /* 500 + deg * 2000 / 180 */
  us = SERVO_PULSE_MIN_US +
       (uint32_t)deg * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) / 180;

  Servo_SetPulse((uint16_t)us);
}
