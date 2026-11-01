#ifndef __MPU6050_H__
#define __MPU6050_H__

#include "main.h"

/* MPU6050 的 7 位地址 0x68（AD0 接地），HAL 需要 8 位地址 */
#define MPU6050_I2C_ADDR  (0x68 << 1)

/**
 * @brief 六轴数据（只读）
 */
typedef struct
{
  float ax, ay, az;   /* 加速度，单位 g（1g ≈ 9.8 m/s²） */
  float gx, gy, gz;   /* 角速度，单位 °/s */
  float tempC;        /* 芯片温度，℃ */
  uint8_t ready;      /* 1 = 器件在线 */
  uint8_t whoAmI;     /* 读到的 WHO_AM_I，排查用 */
  uint8_t scan[4];    /* I2C1 上扫描到的地址（7 位），排查用 */
  uint8_t scanCount;
} MPU6050_Data;

/**
 * @brief 初始化（唤醒 + 配置量程/滤波）
 * @return 0 成功，1 失败
 * @note  挂在 I2C1 上，和 MAX30102 共用总线
 */
uint8_t MPU6050_Init(void);

/**
 * @brief 读取一次数据，建议每 100ms 调用
 */
void MPU6050_Task(void);

/**
 * @brief 获取最新数据
 */
const MPU6050_Data *MPU6050_GetData(void);

#endif // __MPU6050_H__
