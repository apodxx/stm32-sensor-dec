/**
 * @file max30102.c
 * @brief MAX30102 心率/血氧传感器驱动 + PPG 信号处理（不含界面绘制）
 *
 * 采样配置：SpO2 模式（红光 + 红外），100sps，4 次平均 -> FIFO 输出 25Hz
 *
 * 通道自适应：正常模块两路都有信号，用红外做心率、双路算血氧；
 *             若红外通道无信号（LED 损坏 / 克隆芯片），自动改用红光通道测心率，
 *             此时血氧无法测量（SpO2 必须双波长），数据里 spo2 保持 0。
 *
 * 用法：
 *   MAX30102_Init();
 *   主循环每 10ms 调一次 MAX30102_Process();
 *   需要数据时调 MAX30102_GetData() / MAX30102_CopyWave()
 *
 * @note 血氧值是估算值，仅供学习和演示，不能用于医疗用途。
 */
#include "max30102.h"
#include "i2c1.h"   /* 血氧模块走 I2C1（PB6/PB7） */
#include <string.h>

/* ============================ 寄存器地址 ============================ */
#define REG_INTR_STATUS_1 0x00
#define REG_INTR_STATUS_2 0x01
#define REG_INTR_ENABLE_1 0x02
#define REG_INTR_ENABLE_2 0x03
#define REG_FIFO_WR_PTR   0x04
#define REG_OVF_COUNTER   0x05
#define REG_FIFO_RD_PTR   0x06
#define REG_FIFO_DATA     0x07
#define REG_FIFO_CONFIG   0x08
#define REG_MODE_CONFIG   0x09
#define REG_SPO2_CONFIG   0x0A
#define REG_LED1_PA       0x0B
#define REG_LED2_PA       0x0C
#define REG_PART_ID       0xFF

#define MAX30102_PART_ID  0x15

/* ============================ 参数配置 ============================ */
#define WAVE_LEN      128  /* 波形缓存点数 */
#define SPO2_WIN      64   /* 血氧窗口：64 点 @25Hz ≈ 2.5s */
#define HR_HIST       4    /* 心率平均用的心跳间隔个数 */
#define FINGER_TH     8000UL /* 判定"有手指"的原始值阈值 */

/* LED 电流自动调节的目标区间（18 位 ADC，满量程 262143） */
#define LED_DC_HIGH   200000.0f
#define LED_DC_LOW    60000.0f
#define LED_PA_MIN    0x08      /* 1.6mA */
#define LED_PA_MAX    0x64      /* 20mA */
#define LED_PA_STEP   6         /* 每步 1.2mA */

/* 判断某一路是否"有信号"的直流门限 */
#define CH_DC_ALIVE   2000.0f

typedef struct
{
  uint32_t red;
  uint32_t ir;
} MAX30102_Sample;

/* ============================ 模块状态 ============================ */
static uint8_t  sensorReady;
static uint32_t lastRetryTick;
static uint8_t  errCnt;
static uint8_t  fifoBuf[6 * 32];

/* 波形缓存（环形） */
static uint32_t waveBuf[WAVE_LEN];
static uint8_t  wavePos;
static uint8_t  waveCnt;

/* 血氧窗口（环形） */
static uint32_t spo2RedBuf[SPO2_WIN];
static uint32_t spo2IrBuf[SPO2_WIN];
static uint8_t  spo2Pos;
static uint8_t  spo2Cnt;
static uint32_t lastSpo2Tick;

/* 心率检测 */
static float    dcIr, dcRed;
static int32_t  acHist[3];
static int32_t  smHist[3];
static int32_t  acPeak;
static uint32_t lastBeatTick;
static uint16_t beatIv[HR_HIST];
static uint8_t  beatIdx, beatCnt;
static uint8_t  beatFlag;          /* 心跳事件，供蜂鸣器使用 */

/* 对外数据 */
static uint16_t bpm;
static uint8_t  spo2;
static uint8_t  fingerOn, fingerPrev;
static uint8_t  sigIsRed;
static uint32_t lastIr, lastRed;

/* LED 电流 */
static uint8_t ledPa = 0x24;
static uint8_t ledPaGood = 0x24;

/* ============================ 底层 I2C ============================ */
static HAL_StatusTypeDef MAX_WriteReg(uint8_t reg, uint8_t val)
{
  return HAL_I2C_Mem_Write(&hi2c1, MAX30102_I2C_ADDR, reg,
                           I2C_MEMADD_SIZE_8BIT, &val, 1, 50);
}

static HAL_StatusTypeDef MAX_ReadRegs(uint8_t reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(&hi2c1, MAX30102_I2C_ADDR, reg,
                          I2C_MEMADD_SIZE_8BIT, buf, len, 50);
}

/**
 * @brief 读取 FIFO
 * @return -1 表示 I2C 出错；>=0 表示读到的采样个数（0 表示 FIFO 为空）
 */
static int8_t MAX_ReadFIFO(MAX30102_Sample *out, uint8_t maxN)
{
  uint8_t ptr[3]; /* [0]=写指针 [1]=溢出计数 [2]=读指针 */
  uint8_t n;

  if (MAX_ReadRegs(REG_FIFO_WR_PTR, ptr, 3) != HAL_OK)
    return -1;

  n = (uint8_t)((ptr[0] - ptr[2]) & 0x1F);
  if (n > maxN)
    n = maxN;
  if (n == 0)
    return 0;

  if (MAX_ReadRegs(REG_FIFO_DATA, fifoBuf, (uint16_t)(n * 6)) != HAL_OK)
    return -1;

  for (uint8_t i = 0; i < n; i++)
  {
    out[i].red = (((uint32_t)fifoBuf[6 * i + 0] << 16) |
                  ((uint32_t)fifoBuf[6 * i + 1] << 8) |
                  ((uint32_t)fifoBuf[6 * i + 2])) & 0x03FFFF;
    out[i].ir = (((uint32_t)fifoBuf[6 * i + 3] << 16) |
                 ((uint32_t)fifoBuf[6 * i + 4] << 8) |
                 ((uint32_t)fifoBuf[6 * i + 5])) & 0x03FFFF;
  }
  return (int8_t)n;
}

/* ============================ LED 电流 ============================ */
static void MAX_ApplyLed(uint8_t pa)
{
  ledPa = pa;
  MAX_WriteReg(REG_LED1_PA, pa);
  MAX_WriteReg(REG_LED2_PA, pa);
}

/**
 * @brief 根据信号通道的直流分量调整 LED 亮度
 * @return 1 表示档位发生了变化
 */
static uint8_t MAX_AdjustLed(float dc)
{
  uint8_t pa = ledPa;

  if (dc > LED_DC_HIGH && pa > LED_PA_MIN)
    pa = (uint8_t)((dc > 250000.0f) ? (pa - LED_PA_STEP * 2) : (pa - LED_PA_STEP));
  else if (dc < LED_DC_LOW && pa < LED_PA_MAX)
    pa = (uint8_t)((dc < 2000.0f) ? (pa + LED_PA_STEP * 2) : (pa + LED_PA_STEP));

  if (pa < LED_PA_MIN) pa = LED_PA_MIN;
  if (pa > LED_PA_MAX) pa = LED_PA_MAX;

  if (pa == ledPa)
  {
    if (dc >= LED_DC_LOW && dc <= LED_DC_HIGH)
      ledPaGood = pa;
    return 0;
  }

  MAX_ApplyLed(pa);

  /* 亮度变了，基线要重新建立 */
  dcIr = (float)lastIr;
  dcRed = (float)lastRed;
  acHist[0] = acHist[1] = acHist[2] = 0;
  smHist[0] = smHist[1] = smHist[2] = 0;
  acPeak = 0;
  beatCnt = 0;
  beatIdx = 0;
  spo2Cnt = 0;
  spo2Pos = 0;
  return 1;
}

/* ============================ 初始化 ============================ */
uint8_t MAX30102_Init(void)
{
  uint8_t id = 0;
  uint8_t st;

  lastRetryTick = HAL_GetTick();

  /* 1. 读器件 ID，顺便确认接线 */
  if (MAX_ReadRegs(REG_PART_ID, &id, 1) != HAL_OK || id != MAX30102_PART_ID)
  {
    sensorReady = 0;
    return MAX30102_ERR;
  }

  /* 2. 软复位，并等待 RESET 位自动清零（最多 100ms） */
  MAX_WriteReg(REG_MODE_CONFIG, 0x40);
  for (uint8_t i = 0; i < 20; i++)
  {
    uint8_t mode = 0x40;
    if (MAX_ReadRegs(REG_MODE_CONFIG, &mode, 1) == HAL_OK && (mode & 0x40) == 0)
      break;
    HAL_Delay(5);
  }

  /* 3. 清空 FIFO 指针 */
  MAX_WriteReg(REG_FIFO_WR_PTR, 0x00);
  MAX_WriteReg(REG_OVF_COUNTER, 0x00);
  MAX_WriteReg(REG_FIFO_RD_PTR, 0x00);

  /* 4. FIFO：4 次平均(0x40) + 允许覆盖(0x10) + 几乎满阈值 15 */
  MAX_WriteReg(REG_FIFO_CONFIG, 0x40 | 0x10 | 0x0F);
  /* 5. 模式：SpO2（红光 + 红外） */
  MAX_WriteReg(REG_MODE_CONFIG, 0x03);
  /* 6. 量程 4096nA(0x20) + 100sps(0x04) + 脉宽 411us(0x03) */
  MAX_WriteReg(REG_SPO2_CONFIG, 0x27);
  /* 7. LED 电流（每档 0.2mA） */
  MAX_ApplyLed(ledPaGood);

  /* 8. 关闭中断并清标志（本工程用轮询） */
  MAX_WriteReg(REG_INTR_ENABLE_1, 0x00);
  MAX_WriteReg(REG_INTR_ENABLE_2, 0x00);
  MAX_ReadRegs(REG_INTR_STATUS_1, &st, 1);
  MAX_ReadRegs(REG_INTR_STATUS_2, &st, 1);

  /* 9. 清空本地状态 */
  memset(waveBuf, 0, sizeof(waveBuf));
  memset(spo2RedBuf, 0, sizeof(spo2RedBuf));
  memset(spo2IrBuf, 0, sizeof(spo2IrBuf));

  wavePos = waveCnt = 0;
  spo2Pos = spo2Cnt = 0;
  lastSpo2Tick = HAL_GetTick();
  dcIr = dcRed = 0.0f;
  acHist[0] = acHist[1] = acHist[2] = 0;
  smHist[0] = smHist[1] = smHist[2] = 0;
  acPeak = 0;
  lastBeatTick = 0;
  beatIdx = beatCnt = 0;
  bpm = 0;
  spo2 = 0;
  fingerOn = fingerPrev = 0;
  sigIsRed = 0;
  lastIr = lastRed = 0;
  errCnt = 0;

  HAL_Delay(50);

  sensorReady = 1;
  return MAX30102_OK;
}

/* ============================ 信号处理 ============================ */
/**
 * @brief 用窗口内的最大值/最小值估算 AC 和 DC，再算 R 比值和 SpO2
 */
static void MAX_CalcSpO2(void)
{
  uint32_t minR = 0xFFFFFFFF, maxR = 0;
  uint32_t minI = 0xFFFFFFFF, maxI = 0;

  for (uint8_t i = 0; i < SPO2_WIN; i++)
  {
    if (spo2RedBuf[i] < minR) minR = spo2RedBuf[i];
    if (spo2RedBuf[i] > maxR) maxR = spo2RedBuf[i];
    if (spo2IrBuf[i] < minI) minI = spo2IrBuf[i];
    if (spo2IrBuf[i] > maxI) maxI = spo2IrBuf[i];
  }

  float acR = (float)(maxR - minR);
  float dcR = ((float)maxR + (float)minR) * 0.5f;
  float acI = (float)(maxI - minI);
  float dcI = ((float)maxI + (float)minI) * 0.5f;

  if (acI < 100.0f || dcI < 1000.0f || dcR < 1000.0f)
  {
    spo2 = 0;
    return;
  }

  float ratio = (acR / dcR) / (acI / dcI);
  float value = -45.06f * ratio * ratio + 30.354f * ratio + 94.845f;

  if (value > 100.0f)
    value = 100.0f;
  if (value < 70.0f)
  {
    spo2 = 0;
    return;
  }

  spo2 = (uint8_t)(value + 0.5f);
}

/**
 * @brief 处理单个采样点
 */
static void MAX_ProcessSample(uint32_t red, uint32_t ir)
{
  uint32_t now = HAL_GetTick();
  uint32_t sig;
  float sigDc;

  lastRed = red;
  lastIr = ir;

  /* 直流分量一阶低通跟踪（时间常数约 1s @25Hz） */
  if (dcIr < 1.0f)
  {
    dcIr = (float)ir;
    dcRed = (float)red;
  }
  dcIr += ((float)ir - dcIr) * 0.04f;
  dcRed += ((float)red - dcRed) * 0.04f;

  /* 通道自适应：红外没有信号而红光有，就改用红光做心率 */
  sigIsRed = (dcIr < CH_DC_ALIVE && dcRed > CH_DC_ALIVE) ? 1 : 0;
  sig = sigIsRed ? red : ir;
  sigDc = sigIsRed ? dcRed : dcIr;

  /* 手指在位检测 */
  fingerOn = (sig > FINGER_TH);

  /* 刚放上手指时重置基线，避免 DC 跳变导致长时间测不到心跳 */
  if (fingerOn && !fingerPrev)
  {
    dcIr = (float)ir;
    dcRed = (float)red;
    acHist[0] = acHist[1] = acHist[2] = 0;
    smHist[0] = smHist[1] = smHist[2] = 0;
    acPeak = 0;
    beatCnt = 0;
    beatIdx = 0;
    bpm = 0;
  }
  fingerPrev = fingerOn;

  /* 基线可能刚被重置，重新取一次直流 */
  sigDc = sigIsRed ? dcRed : dcIr;

  /* 波形缓存 */
  waveBuf[wavePos] = sig;
  wavePos = (uint8_t)((wavePos + 1) % WAVE_LEN);
  if (waveCnt < WAVE_LEN)
    waveCnt++;

  /* 血氧窗口缓存（两路都存） */
  spo2RedBuf[spo2Pos] = red;
  spo2IrBuf[spo2Pos] = ir;
  spo2Pos = (uint8_t)((spo2Pos + 1) % SPO2_WIN);
  if (spo2Cnt < SPO2_WIN)
    spo2Cnt++;

  /* 交流分量 */
  int32_t ac = (int32_t)sig - (int32_t)sigDc;
  acHist[0] = acHist[1];
  acHist[1] = acHist[2];
  acHist[2] = ac;

  /* 3 点滑动平均，压掉高频噪声和重搏波 */
  int32_t smooth = (acHist[0] + acHist[1] + acHist[2]) / 3;
  smHist[0] = smHist[1];
  smHist[1] = smHist[2];
  smHist[2] = smooth;

  /* 包络跟踪 */
  if (smHist[1] > acPeak)
    acPeak = smHist[1];
  else
  {
    acPeak -= acPeak >> 5;
    if (acPeak < 0)
      acPeak = 0;
  }

  if (fingerOn)
  {
    /* 局部极大值 + 0.45 倍峰值阈值 + 300ms 不应期 */
    if (smHist[1] > smHist[0] && smHist[1] >= smHist[2] &&
        smHist[1] > (acPeak * 45) / 100 && smHist[1] > 50)
    {
      uint32_t dt = now - lastBeatTick;
      if (dt > 300 && dt < 3000)
      {
        uint32_t sum = 0;

        beatIv[beatIdx] = (uint16_t)dt;
        beatIdx = (uint8_t)((beatIdx + 1) % HR_HIST);
        if (beatCnt < HR_HIST)
          beatCnt++;

        for (uint8_t i = 0; i < beatCnt; i++)
          sum += beatIv[i];

        if (sum > 0)
          bpm = (uint16_t)((60000UL * beatCnt) / sum);

        beatFlag = 1; /* 通知上层：这次心跳有效 */
      }
      lastBeatTick = now;
    }

    if (now - lastBeatTick > 3000)
    {
      bpm = 0;
      beatCnt = 0;
      beatIdx = 0;
      acPeak = 0;
    }
  }
  else
  {
    bpm = 0;
    spo2 = 0;
    beatCnt = 0;
    beatIdx = 0;
    acPeak = 0;
    lastBeatTick = now - 3001;
  }

  /* 每秒调整一次 LED 亮度；两路都健康时更新血氧 */
  if ((now - lastSpo2Tick) >= 1000)
  {
    lastSpo2Tick = now;
    MAX_AdjustLed(sigDc);

    if (fingerOn && spo2Cnt >= SPO2_WIN &&
        dcIr > 5000.0f && dcRed > 5000.0f)
      MAX_CalcSpO2();
  }
}

/* ============================ 对外接口 ============================ */
void MAX30102_Process(void)
{
  MAX30102_Sample s[16];
  int8_t n;

  if (!sensorReady)
  {
    if (HAL_GetTick() - lastRetryTick >= 1000)
      MAX30102_Init();
    return;
  }

  n = MAX_ReadFIFO(s, 16);

  if (n < 0)
  {
    /* 连续读失败说明器件掉线，复位 I2C 后重新初始化 */
    if (++errCnt >= 20)
    {
      HAL_I2C_DeInit(&hi2c1);
      MX_I2C1_Init();
      sensorReady = 0;
      errCnt = 0;
      lastRetryTick = HAL_GetTick();
    }
    return;
  }

  errCnt = 0;

  for (uint8_t i = 0; i < (uint8_t)n; i++)
    MAX_ProcessSample(s[i].red, s[i].ir);
}

const MAX30102_Data *MAX30102_GetData(void)
{
  static MAX30102_Data d;
  uint32_t vmin, vmax, v;

  d.heartRate = bpm;
  d.spo2 = spo2;
  d.fingerOn = fingerOn;
  d.ready = sensorReady;
  d.usingRed = sigIsRed;
  d.rawRed = lastRed;
  d.rawIr = lastIr;

  if (waveCnt >= 2)
  {
    uint8_t start = (uint8_t)((wavePos + WAVE_LEN - waveCnt) % WAVE_LEN);
    vmin = vmax = waveBuf[start];
    for (uint8_t i = 1; i < waveCnt; i++)
    {
      v = waveBuf[(uint8_t)((start + i) % WAVE_LEN)];
      if (v < vmin) vmin = v;
      if (v > vmax) vmax = v;
    }
    d.waveP2P = vmax - vmin;
  }
  else
  {
    d.waveP2P = 0;
  }

  return &d;
}

uint8_t MAX30102_CopyWave(uint32_t *dst, uint8_t maxLen)
{
  uint8_t n = waveCnt;
  uint8_t start;

  if (n > maxLen)
    n = maxLen;
  if (n == 0)
    return 0;

  start = (uint8_t)((wavePos + WAVE_LEN - n) % WAVE_LEN);
  for (uint8_t i = 0; i < n; i++)
    dst[i] = waveBuf[(uint8_t)((start + i) % WAVE_LEN)];

  return n;
}

uint8_t MAX30102_TakeBeatEvent(void)
{
  uint8_t b = beatFlag;

  beatFlag = 0;
  return b;
}
