/**
 * @file i2c1.c
 * @brief 血氧模块（MAX30102）专用的 I2C1 总线：PB6 = SCL，PB7 = SDA
 *
 * 为什么要单独一路：
 *   OLED 挂在 I2C2（PB10/PB11），血氧模块改挂 I2C1 后两路互不干扰，
 *   互不影响时序，也方便分开排查（OLED 不亮不会连带血氧掉线）。
 *
 * @note 本文件自己完成时钟、GPIO、NVIC 配置，不依赖 stm32f1xx_hal_msp.c，
 *       所以 CubeMX 重新生成代码不会覆盖这部分。
 */
#include "i2c1.h"

I2C_HandleTypeDef hi2c1;

void MX_I2C1_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 时钟：GPIOB + I2C1 */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_I2C1_CLK_ENABLE();

  /**I2C1 GPIO Configuration
  PB6     ------> I2C1_SCL
  PB7     ------> I2C1_SDA
  */
  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /* 中断：HAL 的阻塞式收发靠中断状态机推进 */
  HAL_NVIC_SetPriority(I2C1_EV_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  HAL_NVIC_SetPriority(I2C1_ER_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}

/**
 * @brief I2C1 事件中断
 */
void I2C1_EV_IRQHandler(void)
{
  HAL_I2C_EV_IRQHandler(&hi2c1);
}

/**
 * @brief I2C1 错误中断
 */
void I2C1_ER_IRQHandler(void)
{
  HAL_I2C_ER_IRQHandler(&hi2c1);
}
