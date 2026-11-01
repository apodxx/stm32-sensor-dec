/**
 * @file ui.c
 * @brief 屏幕界面：页面调度 + 所有绘制代码（中文界面）
 *
 * 页面：
 *   1) 生命体征页：心率、血氧、PPG 波形
 *   2) 温湿度页  ：DHT11 温度、湿度
 *   3) 亮度页    ：GL5516 亮度百分比、亮度条、电压
 *
 * 中文字库 16×16（font16x16），数字用 afont16x8 / afont24x12 兜底。
 * 默认每 5 秒自动轮播；接按键后可用 UI_NextPage() 手动切换。
 */
#include "ui.h"
#include "oled.h"
#include "max30102.h"
#include "dht11.h"
#include "light.h"
#include "mpu6050.h"
#include "speed.h"
#include <string.h>

#define UI_FRAME_MS        40    /* 刷屏周期：25fps */
#define UI_PAGE_PERIOD_MS  5000  /* 自动轮播时每页停留时间 */
#define UI_AUTO_ROTATE     0     /* 1 = 每 5 秒自动换页；0 = 由旋钮电位器控制 */

#define WAVE_DRAW_LEN      128

static UI_Page  currentPage;
static uint32_t pageTick;
static uint32_t drawTick;
static uint32_t waveBuf[WAVE_DRAW_LEN];

/* ============================ 小工具 ============================ */
/**
 * @brief 无符号整数转定长十进制字符串（高位补 0）
 */
static void UintToStr(uint32_t v, uint8_t digits, char *out)
{
  out[digits] = '\0';
  for (int8_t i = (int8_t)digits - 1; i >= 0; i--)
  {
    out[i] = (char)('0' + (v % 10));
    v /= 10;
  }
}

/**
 * @brief 有符号整数转字符串（不补零），返回字符数
 */
static uint8_t IntToStr(int32_t v, char *out)
{
  char tmp[8];
  uint8_t n = 0, k = 0;
  uint32_t u;

  if (v < 0)
  {
    out[n++] = '-';
    u = (uint32_t)(-v);
  }
  else
  {
    u = (uint32_t)v;
  }

  do
  {
    tmp[k++] = (char)('0' + (u % 10));
    u /= 10;
  } while (u > 0 && k < sizeof(tmp));

  while (k > 0)
    out[n++] = tmp[--k];

  out[n] = '\0';
  return n;
}

/**
 * @brief 计算 UTF-8 字符串在指定字体下的像素宽度（用于居中）
 */
static uint8_t UI_StrWidth(const char *s, const Font *font)
{
  uint8_t w = 0;

  while (*s)
  {
    uint8_t c = (uint8_t)*s;

    if (c < 0x80)
    {
      w += font->ascii->w;
      s += 1;
    }
    else if (c >= 0xF0)
    {
      w += font->w;
      s += 4;
    }
    else if (c >= 0xE0)
    {
      w += font->w;
      s += 3;
    }
    else
    {
      w += font->w;
      s += 2;
    }
  }
  return w;
}

/**
 * @brief 居中绘制一行中文
 */
static void UI_CenterText(uint8_t y, const char *s)
{
  uint8_t w = UI_StrWidth(s, &font16x16);

  OLED_PrintString((uint8_t)((128 - w) / 2), y, (char *)s, &font16x16, OLED_COLOR_NORMAL);
}

/**
 * @brief 传感器异常提示（两行中文）
 */
static void UI_DrawError(const char *line1, const char *line2)
{
  OLED_NewFrame();
  UI_CenterText(12, line1);
  UI_CenterText(34, line2);
  OLED_ShowFrame();
}

/**
 * @brief 把浮点数格式化成 "+0.12" 这种定长字符串（两位小数）
 * @note  加速度量程 ±2g，整数部分最多 1 位
 */
static void UI_FmtSigned(char *out, float v)
{
  int32_t m = (int32_t)(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f));
  uint32_t a;

  if (m < 0)
  {
    *out++ = '-';
    a = (uint32_t)(-m);
  }
  else
  {
    *out++ = '+';
    a = (uint32_t)m;
  }

  *out++ = (char)('0' + (a / 100) % 10);
  *out++ = '.';
  *out++ = (char)('0' + (a / 10) % 10);
  *out++ = (char)('0' + a % 10);
  *out = '\0';
}

/* ============================ 生命体征页 ============================ */
static void UI_DrawVitals(void)
{
  const MAX30102_Data *d = MAX30102_GetData();
  char txt[8];
  uint8_t i, n;
  uint32_t vmin = 0xFFFFFFFF, vmax = 0, span;
  uint8_t y, prevY;

  /* 传感器不在线 */
  if (!d->ready)
  {
    UI_DrawError("传感器未连接", "检查接线");
    return;
  }

  OLED_NewFrame();

  /* ---------- 第一行：心率 / 血氧 ---------- */
  OLED_PrintString(0, 0, "心率", &font16x16, OLED_COLOR_NORMAL);
  if (d->fingerOn && d->heartRate > 0)
    UintToStr(d->heartRate, 3, txt);
  else
    strcpy(txt, "---");
  OLED_PrintASCIIString(32, 0, txt, &afont16x8, OLED_COLOR_NORMAL);

  OLED_PrintString(64, 0, "血氧", &font16x16, OLED_COLOR_NORMAL);
  if (d->fingerOn && d->spo2 > 0)
  {
    IntToStr(d->spo2, txt);
    strcat(txt, "%");
  }
  else
  {
    strcpy(txt, "--%");
  }
  OLED_PrintASCIIString(96, 0, txt, &afont16x8, OLED_COLOR_NORMAL);

  /* ---------- 中间：PPG 波形 ---------- */
  n = MAX30102_CopyWave(waveBuf, WAVE_DRAW_LEN);
  if (n >= 2)
  {
    for (i = 0; i < n; i++)
    {
      if (waveBuf[i] < vmin) vmin = waveBuf[i];
      if (waveBuf[i] > vmax) vmax = waveBuf[i];
    }

    if (vmax - vmin < 300)
    {
      /* 没有明显脉搏起伏：画一条中线 */
      for (uint8_t x = 0; x < 128; x += 2)
        OLED_SetPixel(x, 32, OLED_COLOR_NORMAL);
    }
    else
    {
      span = vmax - vmin;
      prevY = 0;
      for (i = 0; i < n; i++)
      {
        y = (uint8_t)(47 - ((waveBuf[i] - vmin) * 29) / span);
        if (i == 0)
          prevY = y;
        else
        {
          OLED_DrawLine((uint8_t)(i - 1), prevY, i, y, OLED_COLOR_NORMAL);
          prevY = y;
        }
      }
    }
  }

  /* ---------- 底部：提示 ---------- */
  if (!d->fingerOn)
    UI_CenterText(48, "请放手指");

  OLED_ShowFrame();
}

/* ============================ 温湿度页 ============================ */
static void UI_DrawClimate(void)
{
  const DHT11_Data *d = DHT11_GetData();
  char txt[8];
  uint8_t len;

  if (!d->valid)
  {
    UI_DrawError("读取失败", "检查接线");
    return;
  }

  OLED_NewFrame();

  /* 温度 */
  OLED_PrintString(8, 12, "温度", &font16x16, OLED_COLOR_NORMAL);
  len = IntToStr(d->temperature, txt);
  OLED_PrintASCIIString(48, 4, txt, &afont24x12, OLED_COLOR_NORMAL);
  OLED_PrintString((uint8_t)(52 + len * 12), 12, "℃", &font16x16, OLED_COLOR_NORMAL);

  /* 湿度 */
  OLED_PrintString(8, 44, "湿度", &font16x16, OLED_COLOR_NORMAL);
  len = IntToStr(d->humidity, txt);
  OLED_PrintASCIIString(48, 36, txt, &afont24x12, OLED_COLOR_NORMAL);
  OLED_PrintASCIIString((uint8_t)(52 + len * 12), 44, "%", &afont16x8, OLED_COLOR_NORMAL);

  OLED_ShowFrame();
}

/* ============================ 亮度页 ============================ */
static void UI_DrawLight(void)
{
  const Light_Data *l = Light_GetData();
  char txt[8];
  const char *lvl;

  OLED_NewFrame();

  /* 亮度百分比 */
  OLED_PrintString(0, 0, "亮度", &font16x16, OLED_COLOR_NORMAL);
  IntToStr(l->percent, txt);
  strcat(txt, "%");
  OLED_PrintASCIIString(40, 0, txt, &afont16x8, OLED_COLOR_NORMAL);

  /* 亮度条 */
  OLED_DrawRectangle(0, 18, 127, 8, OLED_COLOR_NORMAL);
  if (l->percent > 0)
    OLED_DrawFilledRectangle(2, 20, (uint8_t)((uint16_t)l->percent * 123 / 100), 5, OLED_COLOR_NORMAL);

  /* 电压 */
  OLED_PrintString(0, 28, "电压", &font16x16, OLED_COLOR_NORMAL);
  txt[0] = (char)('0' + (l->millivolt / 1000) % 10);
  txt[1] = '.';
  txt[2] = (char)('0' + (l->millivolt / 100) % 10);
  txt[3] = (char)('0' + (l->millivolt / 10) % 10);
  txt[4] = 'V';
  txt[5] = '\0';
  OLED_PrintASCIIString(40, 28, txt, &afont16x8, OLED_COLOR_NORMAL);

  /* 等级 */
  if (l->percent < 10)
    lvl = "暗";
  else if (l->percent < 40)
    lvl = "微亮";
  else if (l->percent < 70)
    lvl = "正常";
  else
    lvl = "明亮";

  UI_CenterText(48, lvl);

  OLED_ShowFrame();
}

/* ============================ 加速度页 ============================ */
static char UI_HexChar(uint8_t v)
{
  return (char)(v < 10 ? ('0' + v) : ('A' + v - 10));
}

/**
 * @brief 加速度页：未连接时显示 I2C 扫描结果，方便排查
 */
static void UI_DrawMpu(void)
{
  const MPU6050_Data *m = MPU6050_GetData();
  char txt[20];
  uint8_t len, n;

  if (!m->ready)
  {
    OLED_NewFrame();
    UI_CenterText(4, "传感器未连接");

    /* I2C1 上扫描到的地址 */
    n = 0;
    txt[n++] = 'I';
    txt[n++] = '2';
    txt[n++] = 'C';
    for (uint8_t i = 0; i < m->scanCount && i < 3; i++)
    {
      txt[n++] = ' ';
      txt[n++] = UI_HexChar(m->scan[i] >> 4);
      txt[n++] = UI_HexChar(m->scan[i] & 0x0F);
    }
    if (m->scanCount == 0)
    {
      txt[n++] = ' ';
      txt[n++] = '-';
      txt[n++] = '-';
    }
    txt[n] = '\0';
    OLED_PrintASCIIString((uint8_t)((128 - n * 8) / 2), 24, txt, &afont16x8, OLED_COLOR_NORMAL);

    /* WHO_AM_I 读到的值 */
    txt[0] = 'I';
    txt[1] = 'D';
    txt[2] = ' ';
    txt[3] = UI_HexChar(m->whoAmI >> 4);
    txt[4] = UI_HexChar(m->whoAmI & 0x0F);
    txt[5] = '\0';
    OLED_PrintASCIIString(44, 44, txt, &afont16x8, OLED_COLOR_NORMAL);

    OLED_ShowFrame();
    return;
  }

  OLED_NewFrame();

  /* 标题行：加速度 + 芯片温度 */
  OLED_PrintString(0, 0, "加速度", &font16x16, OLED_COLOR_NORMAL);
  len = IntToStr((int32_t)(m->tempC + 0.5f), txt);
  OLED_PrintASCIIString(56, 0, txt, &afont16x8, OLED_COLOR_NORMAL);
  OLED_PrintString((uint8_t)(60 + len * 8), 0, "℃", &font16x16, OLED_COLOR_NORMAL);

  /* 三轴加速度（单位 g） */
  OLED_PrintASCIIString(8, 16, "X", &afont16x8, OLED_COLOR_NORMAL);
  UI_FmtSigned(txt, m->ax);
  OLED_PrintASCIIString(24, 16, txt, &afont16x8, OLED_COLOR_NORMAL);

  OLED_PrintASCIIString(8, 32, "Y", &afont16x8, OLED_COLOR_NORMAL);
  UI_FmtSigned(txt, m->ay);
  OLED_PrintASCIIString(24, 32, txt, &afont16x8, OLED_COLOR_NORMAL);

  OLED_PrintASCIIString(8, 48, "Z", &afont16x8, OLED_COLOR_NORMAL);
  UI_FmtSigned(txt, m->az);
  OLED_PrintASCIIString(24, 48, txt, &afont16x8, OLED_COLOR_NORMAL);

  OLED_ShowFrame();
}

/* ============================ 转速页 ============================ */
static void UI_DrawSpeed(void)
{
  const Speed_Data *s = Speed_GetData();
  char txt[16];
  uint8_t len, w, x, n;
  uint16_t v;

  OLED_NewFrame();

  UI_CenterText(0, "转速");

  /* 大号数字 + RPM 单位，整体居中 */
  len = IntToStr(s->rpm, txt);
  w = (uint8_t)(len * 12 + 4 + 24);
  x = (uint8_t)((128 - w) / 2);

  OLED_PrintASCIIString(x, 18, txt, &afont24x12, OLED_COLOR_NORMAL);
  OLED_PrintASCIIString((uint8_t)(x + len * 12 + 4), 26, "RPM", &afont16x8, OLED_COLOR_NORMAL);

  /* 诊断行：DO 电平 + 上一窗口脉冲数 */
  v = s->lastCount;
  if (v > 999)
    v = 999;

  n = 0;
  txt[n++] = 'D';
  txt[n++] = 'O';
  txt[n++] = ' ';
  txt[n++] = s->level ? '1' : '0';
  txt[n++] = ' ';
  txt[n++] = ' ';
  txt[n++] = 'P';
  txt[n++] = ' ';
  txt[n++] = (char)('0' + (v / 100) % 10);
  txt[n++] = (char)('0' + (v / 10) % 10);
  txt[n++] = (char)('0' + v % 10);
  txt[n] = '\0';

  OLED_PrintASCIIString((uint8_t)((128 - n * 8) / 2), 48, txt, &afont16x8, OLED_COLOR_NORMAL);

  OLED_ShowFrame();
}

/* ============================ 对外接口 ============================ */
void UI_Init(void)
{
  currentPage = UI_PAGE_VITALS;
  pageTick = drawTick = HAL_GetTick();
}

void UI_SetPage(UI_Page page)
{
  if (page < UI_PAGE_COUNT)
  {
    currentPage = page;
    pageTick = HAL_GetTick();
  }
}

void UI_NextPage(void)
{
  currentPage = (UI_Page)((currentPage + 1) % UI_PAGE_COUNT);
  pageTick = HAL_GetTick();
}

UI_Page UI_GetPage(void)
{
  return currentPage;
}

void UI_Task(void)
{
  uint32_t now = HAL_GetTick();

  /* 自动轮播（默认关闭，改由旋钮选页） */
#if UI_AUTO_ROTATE
  if ((now - pageTick) >= UI_PAGE_PERIOD_MS)
  {
    pageTick = now;
    currentPage = (UI_Page)((currentPage + 1) % UI_PAGE_COUNT);
  }
#endif

  /* 定时刷屏 */
  if ((now - drawTick) >= UI_FRAME_MS)
  {
    drawTick = now;
    switch (currentPage)
    {
    case UI_PAGE_VITALS:
      UI_DrawVitals();
      break;
    case UI_PAGE_CLIMATE:
      UI_DrawClimate();
      break;
    case UI_PAGE_LIGHT:
      UI_DrawLight();
      break;
    case UI_PAGE_MPU:
      UI_DrawMpu();
      break;
    default:
      UI_DrawSpeed();
      break;
    }
  }
}
