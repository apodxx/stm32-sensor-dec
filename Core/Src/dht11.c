/**
 * @file dht11.c
 * @brief DHT11 温湿度传感器驱动（单总线，阻塞式读取，一次约 4ms）
 *
 * 时序要点：
 *   1. 主机拉低 >= 18ms 作为起始信号，然后释放总线
 *   2. 传感器应答：80us 低 + 80us 高
 *   3. 40 位数据：每位 = 50us 低 + (26~28us 高 = 0 / 70us 高 = 1)
 *   4. 5 字节：湿度整数、湿度小数、温度整数、温度小数、校验和
 *
 * 微秒延时使用 Cortex-M3 的 DWT 周期计数器，不受中断影响。
 */
#include "dht11.h"

#define DHT11_START_MS    20   /* 起始低电平时间，>=18ms */
#define DHT11_TIMEOUT_US  120  /* 等待电平翻转的超时时间 */

static DHT11_Data dht;
static uint32_t   nextReadTick;

/* ============================ 底层工具 ============================ */
static void DHT11_ClockEnable(void)
{
  if (DHT11_PORT == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
  else if (DHT11_PORT == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
  else if (DHT11_PORT == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
  else if (DHT11_PORT == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
}

static void DHT11_DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000U);

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

/**
 * @brief 切换引脚方向
 * @param output 1=开漏输出（驱动总线）  0=输入+上拉（释放总线并读取）
 * @note  STM32F1 的输出模式无法启用内部上拉，所以释放总线时切到输入模式，
 *        这样即使模块没有板载上拉电阻，也能靠内部弱上拉工作
 */
static void DHT11_PinConfig(uint8_t output)
{
  GPIO_InitTypeDef g = {0};

  g.Pin = DHT11_PIN;
  g.Speed = GPIO_SPEED_FREQ_HIGH;

  if (output)
  {
    g.Mode = GPIO_MODE_OUTPUT_OD;
  }
  else
  {
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
  }

  HAL_GPIO_Init(DHT11_PORT, &g);
}

/**
 * @brief 等待引脚变为指定电平
 * @return 1 成功，0 超时
 */
static uint8_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeoutUs)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = timeoutUs * (SystemCoreClock / 1000000U);

  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) != level)
  {
    if ((DWT->CYCCNT - start) > ticks)
      return 0;
  }
  return 1;
}

/* ============================ 读取一帧 ============================ */
/**
 * @brief 读一次完整数据
 * @param out 5 字节原始数据
 * @return 1 成功，0 失败（无应答 / 超时 / 校验错）
 */
static uint8_t DHT11_ReadBytes(uint8_t *out)
{
  uint8_t data[5] = {0, 0, 0, 0, 0};
  uint8_t ok = 0;

  /* 起始信号：拉低 20ms，然后释放总线 */
  DHT11_PinConfig(1);
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
  HAL_Delay(DHT11_START_MS);

  /* 时序敏感，关中断避免被 SysTick 打断（最长约 5ms） */
  __disable_irq();
  DHT11_PinConfig(0); /* 切输入 + 上拉，总线被释放 */

  do
  {
    /* 先等总线真正被上拉拉高，再等传感器应答，避免把释放过程误判成应答 */
    if (!DHT11_WaitLevel(GPIO_PIN_SET, DHT11_TIMEOUT_US)) goto done;
    /* 应答信号：80us 低 + 80us 高 */
    if (!DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_TIMEOUT_US)) goto done;
    if (!DHT11_WaitLevel(GPIO_PIN_SET, DHT11_TIMEOUT_US)) goto done;
    if (!DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_TIMEOUT_US)) goto done;

    for (uint8_t i = 0; i < 40; i++)
    {
      /* 每一位都以 50us 低电平开始 */
      if (!DHT11_WaitLevel(GPIO_PIN_SET, DHT11_TIMEOUT_US)) goto done;

      /* 高电平持续 26~28us 表示 0，70us 表示 1；在 40us 处采样最稳 */
      DHT11_DelayUs(40);
      if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
      {
        data[i / 8] |= (uint8_t)(1u << (7 - (i % 8)));
        /* 1 的高电平还要持续 30us，等它结束再进入下一位 */
        if (!DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_TIMEOUT_US)) goto done;
      }
    }

    /* 校验：前 4 字节之和的低 8 位等于第 5 字节 */
    ok = ((uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4]) ? 1 : 0;
    if (ok)
    {
      for (uint8_t i = 0; i < 5; i++)
        out[i] = data[i];
    }
  } while (0);

done:
  __enable_irq();
  return ok;
}

/* ============================ 对外接口 ============================ */
void DHT11_Init(void)
{
  DHT11_ClockEnable();

  /* 空闲状态：输入 + 上拉，总线保持高电平 */
  DHT11_PinConfig(0);

  /* 使能 DWT 周期计数器，用于微秒级延时 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  dht.temperature = 0;
  dht.humidity = 0;
  dht.valid = 0;
  dht.failCnt = 0;
  dht.lastOkTick = 0;

  /* 上电后传感器需要约 1s 稳定，第一次读取延后 */
  nextReadTick = HAL_GetTick() + 1000;
}

void DHT11_Task(void)
{
  uint8_t raw[5];
  uint32_t now = HAL_GetTick();

  /* 还没到下一次读取时间（用有符号差值，天然处理 tick 回绕） */
  if ((int32_t)(now - nextReadTick) < 0)
    return;

  nextReadTick = now + DHT11_PERIOD_MS;

  if (DHT11_ReadBytes(raw))
  {
    dht.humidity = raw[0];
    /* 温度整数 + 符号位（DHT11 负温时 bit7 为 1） */
    dht.temperature = (raw[3] & 0x80) ? (int8_t)(-(int8_t)raw[2]) : (int8_t)raw[2];
    dht.valid = 1;
    dht.failCnt = 0;
    dht.lastOkTick = now;
  }
  else
  {
    if (dht.failCnt < 255)
      dht.failCnt++;
    if (dht.failCnt >= 3)
      dht.valid = 0; /* 连续失败才判为无效，避免偶发一次就闪 */
  }
}

const DHT11_Data *DHT11_GetData(void)
{
  return &dht;
}
