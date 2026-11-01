#ifndef __BUZZER_H__
#define __BUZZER_H__

#include "main.h"

/* 控制脚：PA2（空脚，普通推挽输出即可） */
#define BUZZER_PORT   GPIOA
#define BUZZER_PIN    GPIO_PIN_2

/* 触发极性：0 = 高电平响（大多数模块），1 = 低电平响（低触发模块） */
#define BUZZER_ACTIVE_LOW  0

/**
 * @brief 初始化控制脚并关闭蜂鸣器
 */
void Buzzer_Init(void);

/**
 * @brief 直接开 / 关
 */
void Buzzer_On(void);
void Buzzer_Off(void);

/**
 * @brief 非阻塞鸣叫：立即响，到时间由 Buzzer_Task() 关掉
 * @param ms 持续时间（毫秒）
 */
void Buzzer_Beep(uint16_t ms);

/**
 * @brief 鸣叫计时任务，放主循环里反复调用
 */
void Buzzer_Task(void);

#endif // __BUZZER_H__
