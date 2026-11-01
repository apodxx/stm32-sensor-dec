#ifndef __ENCODER_H__
#define __ENCODER_H__

#include "main.h"

/* 编码器两相：PA5 = A 相，PA6 = B 相（都在 EXTI9_5 中断组里） */
#define ENC_A_PORT   GPIOA
#define ENC_A_PIN    GPIO_PIN_5
#define ENC_B_PORT   GPIOA
#define ENC_B_PIN    GPIO_PIN_6

/**
 * @brief 初始化 A/B 两相为双边沿中断 + 内部上拉
 */
void Encoder_Init(void);

/**
 * @brief 累计计数（4 倍频：一个机械档位 = 4 个计数）
 */
int16_t Encoder_GetCount(void);

/**
 * @brief 累计档位 = 计数 / 4
 */
int16_t Encoder_GetDetents(void);

/**
 * @brief 清零计数
 */
void Encoder_Reset(void);

/**
 * @brief EXTI 分发入口，由 HAL_GPIO_EXTI_Callback 调用
 */
void Encoder_OnExti(uint16_t pin);

#endif // __ENCODER_H__
