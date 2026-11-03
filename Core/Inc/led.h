#ifndef __LED_H__
#define __LED_H__

#include "main.h"

/*
 * 控制灯 / LED：PA4
 *
 * 接线（高电平点亮，默认）：
 *   PA4 ──[220Ω]── LED 正极（长脚），LED 负极（短脚）── GND
 *
 * 如果 LED 是接成"低电平点亮"（PA4 ─ LED ─ 3V3），把 LED_ACTIVE_LOW 改成 1。
 *
 * 想换引脚：只改下面三行（比如换 PA3：LED_PIN = GPIO_PIN_3）。
 */
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_4
#define LED_ACTIVE_LOW  0

void Led_Init(void);
void Led_Set(uint8_t on);
void Led_Toggle(void);
uint8_t Led_Get(void);

#endif // __LED_H__
