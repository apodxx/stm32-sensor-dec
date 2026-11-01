#ifndef __MAX30102_H__
#define __MAX30102_H__

#include "main.h"

/* MAX30102 的 7 位地址是 0x57，HAL 库需要 8 位地址，左移一位得到 0xAE */
#define MAX30102_I2C_ADDR (0x57 << 1)

/* 返回值 */
#define MAX30102_OK   0
#define MAX30102_ERR  1

/**
 * @brief 传感器对外数据（只读，由驱动内部维护）
 */
typedef struct
{
  uint16_t heartRate;   /* 心率 bpm，0 表示暂时没有有效值 */
  uint8_t  spo2;        /* 血氧百分比，0 表示无效 */
  uint8_t  fingerOn;    /* 1 = 检测到手指 */
  uint8_t  ready;       /* 1 = 器件在线 */
  uint8_t  usingRed;    /* 1 = 红外通道无信号，正在用红光测心率 */
  uint32_t rawRed;      /* 最近一次红光原始值（18 位） */
  uint32_t rawIr;       /* 最近一次红外原始值（18 位） */
  uint32_t waveP2P;     /* 波形窗口内的峰峰值 */
} MAX30102_Data;

/**
 * @brief 初始化传感器（软复位 + 配置 + 开灯）
 * @return 0 成功，1 失败（器件不在线）
 * @note  失败后不需要外部重试，MAX30102_Process() 会每秒自动重连
 */
uint8_t MAX30102_Init(void);

/**
 * @brief 轮询 FIFO 并更新心率/血氧/波形
 * @note  建议每 10ms 调用一次
 */
void MAX30102_Process(void);

/**
 * @brief 获取最新数据
 */
const MAX30102_Data *MAX30102_GetData(void);

/**
 * @brief 按时间顺序（旧 -> 新）拷贝波形数据
 * @param dst    目标缓冲区
 * @param maxLen 最多拷贝多少个点
 * @return 实际拷贝的点数
 */
uint8_t MAX30102_CopyWave(uint32_t *dst, uint8_t maxLen);

/**
 * @brief 取一次"心跳事件"（读后自动清零）
 * @return 1 表示自上次调用以来检测到一次有效心跳
 * @note  用于驱动蜂鸣器打拍子，UI 模块不会消耗这个事件
 */
uint8_t MAX30102_TakeBeatEvent(void);

#endif // __MAX30102_H__
