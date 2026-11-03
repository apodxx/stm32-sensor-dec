	/**
 * @file link.c
 * @brief 与 ESP32（小智 AI）通信的串口命令通道
 *
 * 硬件：USART1，PA9 = TX，PA10 = RX，115200 8N1，3.3V 电平，必须共地。
 *
 * 协议：ASCII 文本，一行一条命令，以 \n 或 \r 结尾；回复同样以 \r\n 结尾。
 *
 *   命令              回复示例
 *   PING            -> PONG
 *   GET ALL         -> HR=72 SpO2=0 Finger=1 Temp=26 Humi=58 Light=62 RPM=1234 Ax=0.12 Ay=-0.03 Az=1.00
 *   GET HR          -> HR=72 SpO2=0 Finger=1
 *   GET ENV         -> Temp=26 Humi=58
 *   GET LIGHT       -> Light=62
 *   GET IMU         -> Ax=0.12 Ay=-0.03 Az=1.00
 *   GET RPM         -> RPM=1234
 *   PAGE <0-4>      -> OK Page=3
 *   BEEP <ms>       -> OK
 *   其它            -> ERR
 *
 * 为什么用文本而不是二进制：ESP32 侧直接把字符串交给 MCP 工具返回给大模型，
 * 省一次解析；调试时用串口助手也能直接看懂。
 *
 * @note USART1 的 HAL 驱动没有加进工程（CubeMX 没启用），所以这里直接写寄存器，
 *       和 adc1.c 的做法一致，重新生成代码不会丢。
 */
#include "link.h"
#include "max30102.h"
#include "dht11.h"
#include "light.h"
#include "mpu6050.h"
#include "speed.h"
#include "buzzer.h"
#include "led.h"
#include "ui.h"
#include <string.h>

#define RX_BUF_SIZE 128
#define LINE_MAX    64

static volatile uint8_t  rxBuf[RX_BUF_SIZE];
static volatile uint16_t rxHead, rxTail;
static char line[LINE_MAX];
static uint8_t lineLen;

static char txBuf[128];
static uint8_t txLen;

/* ============================ 底层串口 ============================ */
void Link_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();

  /* PA9 = TX：复用推挽输出 50MHz（CRH[7:4] = 1011） */
  GPIOA->CRH = (GPIOA->CRH & ~(0xFu << 4)) | (0xBu << 4);
  /* PA10 = RX：上拉输入（CRH[11:8] = 1000），ODR 置 1 选中上拉 */
  GPIOA->CRH = (GPIOA->CRH & ~(0xFu << 8)) | (0x8u << 8);
  GPIOA->ODR |= (1u << 10);

  USART1->BRR = (SystemCoreClock / LINK_BAUD); /* 72MHz / 115200 = 625 */
  USART1->CR2 = 0;
  USART1->CR3 = 0;
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

  HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

  rxHead = rxTail = 0;
  lineLen = 0;
}

/**
 * @brief USART1 中断：只负责把字节塞进环形缓冲
 */
void USART1_IRQHandler(void)
{
  uint32_t sr = USART1->SR;

  if (sr & USART_SR_RXNE)
  {
    uint8_t b = (uint8_t)USART1->DR; /* 读 DR 会清 RXNE / ORE */
    uint16_t next = (uint16_t)((rxHead + 1) % RX_BUF_SIZE);

    if (next != rxTail) /* 满了就丢弃，不阻塞 */
    {
      rxBuf[rxHead] = b;
      rxHead = next;
    }
  }
  else if (sr & USART_SR_ORE)
  {
    (void)USART1->DR;
  }
}

static void Link_SendRaw(const char *s)
{
  while (*s)
  {
    while (!(USART1->SR & USART_SR_TXE))
    {
    }
    USART1->DR = (uint8_t)(*s++);
  }
}

/* ============================ 组包小工具 ============================ */
static void txReset(void) { txLen = 0; }

static void txStr(const char *s)
{
  while (*s && txLen < sizeof(txBuf) - 1)
    txBuf[txLen++] = *s++;
}

static void txInt(int32_t v)
{
  char tmp[12];
  uint8_t n = 0;
  uint32_t u;
  uint8_t neg = 0;

  if (v < 0)
  {
    neg = 1;
    u = (uint32_t)(-v);
  }
  else
  {
    u = (uint32_t)v;
  }

  do
  {
    tmp[n++] = (char)('0' + u % 10);
    u /= 10;
  } while (u > 0 && n < sizeof(tmp));

  if (neg && txLen < sizeof(txBuf) - 1)
    txBuf[txLen++] = '-';
  while (n > 0 && txLen < sizeof(txBuf) - 1)
    txBuf[txLen++] = tmp[--n];
}

/**
 * @brief 定点两位小数，例如 0.12 / -0.03 / 1.00
 */
static void txFloat2(float v)
{
  int32_t m = (int32_t)(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f));
  uint32_t a;
  char frac[4];

  if (m < 0)
  {
    txStr("-");
    a = (uint32_t)(-m);
  }
  else
  {
    a = (uint32_t)m;
  }

  txInt((int32_t)(a / 100));
  frac[0] = '.';
  frac[1] = (char)('0' + (a / 10) % 10);
  frac[2] = (char)('0' + a % 10);
  frac[3] = '\0';
  txStr(frac);
}

static void txSend(void)
{
  txBuf[txLen] = '\0';
  Link_SendRaw(txBuf);
  Link_SendRaw("\r\n");
}

void Link_SendLine(const char *s)
{
  Link_SendRaw(s);
  Link_SendRaw("\r\n");
}

/* ============================ 命令处理 ============================ */
static void ReplyAll(void)
{
  const MAX30102_Data *hr = MAX30102_GetData();
  const DHT11_Data *env = DHT11_GetData();
  const Light_Data *lt = Light_GetData();
  const MPU6050_Data *imu = MPU6050_GetData();
  const Speed_Data *sp = Speed_GetData();

  txReset();
  txStr("HR=");
  txInt(hr->fingerOn ? hr->heartRate : 0);
  txStr(" SpO2=");
  txInt(hr->spo2);
  txStr(" Finger=");
  txInt(hr->fingerOn);
  txStr(" Temp=");
  txInt(env->valid ? env->temperature : 0);
  txStr(" Humi=");
  txInt(env->valid ? env->humidity : 0);
  txStr(" Light=");
  txInt(lt->percent);
  txStr(" RPM=");
  txInt(sp->rpm);
  txStr(" Ax=");
  txFloat2(imu->ax);
  txStr(" Ay=");
  txFloat2(imu->ay);
  txStr(" Az=");
  txFloat2(imu->az);
  txSend();
}

static void HandleLine(char *cmd)
{
  /* 去掉尾部空格 */
  size_t len = strlen(cmd);
  while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t'))
    cmd[--len] = '\0';

  if (len == 0)
    return;

  if (strcmp(cmd, "PING") == 0)
  {
    Link_SendLine("PONG");
  }
  else if (strcmp(cmd, "GET ALL") == 0)
  {
    ReplyAll();
  }
  else if (strcmp(cmd, "GET HR") == 0)
  {
    const MAX30102_Data *d = MAX30102_GetData();

    txReset();
    txStr("HR=");
    txInt(d->fingerOn ? d->heartRate : 0);
    txStr(" SpO2=");
    txInt(d->spo2);
    txStr(" Finger=");
    txInt(d->fingerOn);
    txSend();
  }
  else if (strcmp(cmd, "GET ENV") == 0)
  {
    const DHT11_Data *d = DHT11_GetData();

    txReset();
    txStr("Temp=");
    txInt(d->valid ? d->temperature : 0);
    txStr(" Humi=");
    txInt(d->valid ? d->humidity : 0);
    txSend();
  }
  else if (strcmp(cmd, "GET LIGHT") == 0)
  {
    txReset();
    txStr("Light=");
    txInt(Light_GetData()->percent);
    txSend();
  }
  else if (strcmp(cmd, "GET IMU") == 0)
  {
    const MPU6050_Data *d = MPU6050_GetData();

    txReset();
    txStr("Ax=");
    txFloat2(d->ax);
    txStr(" Ay=");
    txFloat2(d->ay);
    txStr(" Az=");
    txFloat2(d->az);
    txSend();
  }
  else if (strcmp(cmd, "GET RPM") == 0)
  {
    txReset();
    txStr("RPM=");
    txInt(Speed_GetData()->rpm);
    txSend();
  }
  else if (strncmp(cmd, "PAGE ", 5) == 0)
  {
    int32_t p = 0;
    const char *s = cmd + 5;

    while (*s >= '0' && *s <= '9')
      p = p * 10 + (*s++ - '0');

    if (p < 0 || p >= UI_PAGE_COUNT)
    {
      Link_SendLine("ERR Page");
      return;
    }

    UI_SetPage((UI_Page)p);

    txReset();
    txStr("OK Page=");
    txInt(p);
    txSend();
  }
  else if (strncmp(cmd, "BEEP", 4) == 0)
  {
    int32_t ms = 100;
    const char *s = cmd + 4;

    while (*s == ' ')
      s++;
    if (*s >= '0' && *s <= '9')
    {
      ms = 0;
      while (*s >= '0' && *s <= '9')
        ms = ms * 10 + (*s++ - '0');
    }

    Buzzer_Beep((uint16_t)ms);
    Link_SendLine("OK");
  }
  else if (strcmp(cmd, "LED ON") == 0)
  {
    Led_Set(1);
    Link_SendLine("OK LED=1");
  }
  else if (strcmp(cmd, "LED OFF") == 0)
  {
    Led_Set(0);
    Link_SendLine("OK LED=0");
  }
  else if (strcmp(cmd, "LED TOGGLE") == 0)
  {
    Led_Toggle();

    txReset();
    txStr("OK LED=");
    txInt(Led_Get());
    txSend();
  }
  else if (strcmp(cmd, "GET LED") == 0)
  {
    txReset();
    txStr("LED=");
    txInt(Led_Get());
    txSend();
  }
  else
  {
    Link_SendLine("ERR");
  }
}

void Link_Task(void)
{
  while (rxTail != rxHead)
  {
    char c = (char)rxBuf[rxTail];
    rxTail = (uint16_t)((rxTail + 1) % RX_BUF_SIZE);

    if (c == '\n' || c == '\r')
    {
      if (lineLen > 0)
      {
        line[lineLen] = '\0';
        lineLen = 0;
        HandleLine(line);
      }
    }
    else if (lineLen < LINE_MAX - 1)
    {
      line[lineLen++] = c;
    }
    else
    {
      lineLen = 0; /* 超长行直接丢弃 */
    }
  }
}
