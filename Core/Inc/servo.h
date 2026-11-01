#ifndef __SERVO_H__
#define __SERVO_H__

#include "main.h"

/* 信号线：PA1 = TIM2_CH2（换引脚要同时改 servo.c 里的定时器和通道） */
#define SERVO_TIM           TIM2
#define SERVO_TIM_CHANNEL   TIM_CHANNEL_2
#define SERVO_GPIO_PORT     GPIOA
#define SERVO_GPIO_PIN      GPIO_PIN_1

/* SG90 脉宽范围：0.5ms ~ 2.5ms 对应 0° ~ 180° */
#define SERVO_PULSE_MIN_US  500
#define SERVO_PULSE_MAX_US  2500

/**
 * @brief 初始化 PWM（50Hz，分辨率 1us）
 */
void Servo_Init(void);

/**
 * @brief 设置角度
 * @param deg 0~180，超出自动限幅
 */
void Servo_SetAngle(uint8_t deg);

/**
 * @brief 直接设置脉宽
 * @param us 500~2500（微秒），超出自动限幅
 */
void Servo_SetPulse(uint16_t us);

#endif // __SERVO_H__
