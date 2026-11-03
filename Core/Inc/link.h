#ifndef __LINK_H__
#define __LINK_H__

#include "main.h"

/* 与 ESP32 通信的串口：USART1，PA9=TX、PA10=RX，115200 8N1 */
#define LINK_BAUD     115200

/**
 * @brief 初始化 USART1（中断接收 + 环形缓冲）
 */
void Link_Init(void);

/**
 * @brief 解析上位机命令，建议主循环每 5ms 调用一次
 */
void Link_Task(void);

/**
 * @brief 主动上报一行文本（调试用）
 */
void Link_SendLine(const char *s);

#endif // __LINK_H__
