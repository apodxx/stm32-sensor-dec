#ifndef __ADC1_H__
#define __ADC1_H__

#include "main.h"

/**
 * @brief 初始化 ADC1（12MHz 时钟、12 位、校准）
 * @note  多次调用只生效一次；各传感器的引脚配置由各自模块负责
 */
void ADC1_Init(void);

/**
 * @brief 在指定通道上做一次单次转换
 * @param channel 0~17（对应 ADC1_INx）
 * @return 0~4095；超时返回 0
 */
uint16_t ADC1_Read(uint8_t channel);

#endif // __ADC1_H__
