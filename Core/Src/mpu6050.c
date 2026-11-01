/**
 * @file mpu6050.c
 * @brief MPU6050 六轴传感器驱动（I2C1，与 MAX30102 共用总线）
 *
 * 配置：
 *   PWR_MGMT_1  = 0x00  唤醒，内部 8MHz 时钟
 *   SMPLRT_DIV  = 0x09  采样率 1kHz/(1+9) = 100Hz
 *   CONFIG      = 0x03  DLPF 44Hz
 *   GYRO_CONFIG = 0x00  ±250 °/s（131 LSB/°/s）
 *   ACCEL_CONFIG= 0x00  ±2 g（16384 LSB/g）
 *
 * 接线（GY-521 模块）：
 *   VCC -> 3.3V    GND -> GND
 *   SCL -> PB6     SDA -> PB7
 *   AD0 -> 悬空(0x68) / 接 3.3V(0x69)
 *   INT -> 悬空
 */
#include "mpu6050.h"
#include "i2c1.h"

/* 寄存器 */
#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG        0x1A
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_WHO_AM_I      0x75

static MPU6050_Data mpu;
static uint32_t lastRetryTick;
static uint8_t  failCnt;
static uint8_t  chipId;                    /* WHO_AM_I，决定温度换算公式 */
static uint16_t mpuAddr = MPU6050_I2C_ADDR; /* 实际地址，支持 0x68 / 0x69 自动探测 */

static uint8_t MPU_WriteReg(uint8_t reg, uint8_t val)
{
  return (HAL_I2C_Mem_Write(&hi2c1, mpuAddr, reg, I2C_MEMADD_SIZE_8BIT,
                            &val, 1, 50) == HAL_OK) ? 1 : 0;
}

static uint8_t MPU_ReadRegs(uint8_t reg, uint8_t *buf, uint16_t len)
{
  return (HAL_I2C_Mem_Read(&hi2c1, mpuAddr, reg, I2C_MEMADD_SIZE_8BIT,
                           buf, len, 50) == HAL_OK) ? 1 : 0;
}

/**
 * @brief 扫描 I2C1 总线，记录应答的地址（排查用）
 * @note  只扫 0x08~0x77 这段常用地址，避免误碰保留地址
 */
static void MPU_ScanBus(void)
{
  mpu.scanCount = 0;

  for (uint8_t addr = 0x08; addr <= 0x77; addr++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 5) == HAL_OK)
    {
      if (mpu.scanCount < sizeof(mpu.scan))
        mpu.scan[mpu.scanCount++] = addr;
    }
  }
}

/**
 * @brief 判断 WHO_AM_I 是不是 MPU 系列（寄存器映射兼容）
 *
 * 0x68 MPU6050 / MPU6000
 * 0x70 MPU6500
 * 0x71 MPU9250
 * 0x72 部分兼容片
 * 0x73 MPU9255
 * 0x74 MPU6515
 * 0x98 ICM20689
 */
static uint8_t MPU_IsKnownId(uint8_t id)
{
  return (id == 0x68 || id == 0x70 || id == 0x71 || id == 0x72 ||
          id == 0x73 || id == 0x74 || id == 0x98) ? 1 : 0;
}

/**
 * @brief 在指定地址上读 WHO_AM_I 并判断是不是 MPU 系列
 */
static uint8_t MPU_Probe(uint16_t addr8)
{
  uint8_t id = 0;

  if (HAL_I2C_Mem_Read(&hi2c1, addr8, MPU_REG_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                       &id, 1, 50) != HAL_OK)
    return 0;

  mpu.whoAmI = id;
  return MPU_IsKnownId(id);
}

uint8_t MPU6050_Init(void)
{
  lastRetryTick = HAL_GetTick();

  /* 地址自动探测：AD0 悬空是 0x68，接高是 0x69 */
  if (MPU_Probe(0x68 << 1))
  {
    mpuAddr = 0x68 << 1;
  }
  else if (MPU_Probe(0x69 << 1))
  {
    mpuAddr = 0x69 << 1;
  }
  else
  {
    mpu.ready = 0;
    MPU_ScanBus();
    return 1;
  }

  MPU_WriteReg(MPU_REG_PWR_MGMT_1, 0x80); /* 软复位 */
  HAL_Delay(100);
  MPU_WriteReg(MPU_REG_PWR_MGMT_1, 0x00); /* 唤醒 */
  HAL_Delay(10);

  MPU_WriteReg(MPU_REG_SMPLRT_DIV, 0x09);   /* 100Hz */
  MPU_WriteReg(MPU_REG_CONFIG, 0x03);       /* DLPF 44Hz */
  MPU_WriteReg(MPU_REG_GYRO_CONFIG, 0x00);  /* ±250°/s */
  MPU_WriteReg(MPU_REG_ACCEL_CONFIG, 0x00); /* ±2g */

  mpu.ax = mpu.ay = mpu.az = 0.0f;
  mpu.gx = mpu.gy = mpu.gz = 0.0f;
  mpu.tempC = 0.0f;
  mpu.scanCount = 0;
  mpu.ready = 1;
  chipId = mpu.whoAmI;
  failCnt = 0;

  return 0;
}

void MPU6050_Task(void)
{
  uint8_t buf[14];
  int16_t raw[7];

  if (!mpu.ready)
  {
    /* 每秒尝试重连一次 */
    if (HAL_GetTick() - lastRetryTick >= 1000)
      MPU6050_Init();
    return;
  }

  if (!MPU_ReadRegs(MPU_REG_ACCEL_XOUT_H, buf, 14))
  {
    if (++failCnt >= 10)
    {
      mpu.ready = 0;
      failCnt = 0;
      lastRetryTick = HAL_GetTick();
    }
    return;
  }
  failCnt = 0;

  /* 大端：加速度 XYZ、温度、角速度 XYZ */
  for (uint8_t i = 0; i < 7; i++)
    raw[i] = (int16_t)(((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1]);

  mpu.ax = raw[0] / 16384.0f;
  mpu.ay = raw[1] / 16384.0f;
  mpu.az = raw[2] / 16384.0f;
  /* 温度公式：MPU6050 和其它（6500/6515/9250/20689）不一样 */
  if (chipId == 0x68)
    mpu.tempC = raw[3] / 340.0f + 36.53f;
  else
    mpu.tempC = raw[3] / 333.87f + 21.0f;
  mpu.gx = raw[4] / 131.0f;
  mpu.gy = raw[5] / 131.0f;
  mpu.gz = raw[6] / 131.0f;
}

const MPU6050_Data *MPU6050_GetData(void)
{
  return &mpu;
}
