#ifndef __I2C1_H__
#define __I2C1_H__

#include "main.h"

/* 血氧模块专用总线：PB6 = I2C1_SCL，PB7 = I2C1_SDA */
extern I2C_HandleTypeDef hi2c1;

/**
 * @brief 初始化 I2C1（400kHz，PB6/PB7）
 * @note  时钟、GPIO、NVIC 都在这里配置，不依赖 CubeMX 生成的 MSP 回调，
 *        因此重新生成代码不会影响它
 */
void MX_I2C1_Init(void);

#endif // __I2C1_H__
