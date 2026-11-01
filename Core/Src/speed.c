/**
 * @file speed.c
 * @brief 槽型光耦测速模块驱动（PB1 / EXTI1）
 *
 * 原理：
 *   电机轴上装一个 N 槽的码盘，光电对管每被挡住一次输出一个下降沿。
 *   500ms 窗口内数到 P 个脉冲，则
 *       转速 RPM = P * (60000 / 500) / N = P * 120 / N
 *
 * 接线（3 针模块）：
 *   VCC -> 3.3V 或 5V（PB1 是 5V 容忍脚）
 *   GND -> GND
 *   DO  -> PB1
 *   AO  -> 悬空（本模块不用模拟输出）
 *
 * @note 码盘槽数由 SPEED_SLOTS 决定，20 槽时 1 转 = 20 个脉冲。
 */
#include "speed.h"

static volatile uint32_t pulseCnt;   /* 中断里累加 */
static Speed_Data speed;
static uint32_t windowStart;

/* ============================ 中断 ============================ */
/**
 * @brief EXTI1 中断入口（PB1）
 */
void EXTI1_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(SPEED_PIN);
}

/**
 * @brief 外部中断分发：每来一个下降沿计一次数
 * @note  由 main.c 的 HAL_GPIO_EXTI_Callback 统一分发进来
 */
void Speed_OnExti(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == SPEED_PIN)
  {
    pulseCnt++;
  }
}

/* ============================ 对外接口 ============================ */
uint8_t Speed_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  g.Pin = SPEED_PIN;
  g.Mode = GPIO_MODE_IT_FALLING; /* 挡光时模块输出由高变低 */
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SPEED_PORT, &g);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  pulseCnt = 0;
  speed.rpm = 0;
  speed.pulsesPerSec = 0;
  speed.totalPulses = 0;
  speed.lastCount = 0;
  speed.level = (HAL_GPIO_ReadPin(SPEED_PORT, SPEED_PIN) == GPIO_PIN_SET) ? 1 : 0;
  speed.active = 0;
  windowStart = HAL_GetTick();

  return 0;
}

void Speed_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t cnt;

  /* 电平每次都刷新，方便在屏幕上看 DO 有没有跳变 */
  speed.level = (HAL_GPIO_ReadPin(SPEED_PORT, SPEED_PIN) == GPIO_PIN_SET) ? 1 : 0;

  if ((now - windowStart) < SPEED_WINDOW_MS)
    return;

  /* 读并清零计数：关中断避免这一瞬间漏掉一个脉冲 */
  __disable_irq();
  cnt = pulseCnt;
  pulseCnt = 0;
  __enable_irq();

  windowStart = now;

  speed.totalPulses += cnt;
  speed.lastCount = (uint16_t)cnt;
  speed.pulsesPerSec = (uint16_t)(cnt * (1000 / SPEED_WINDOW_MS));
  speed.active = (cnt > 0) ? 1 : 0;

  /* RPM = cnt * 60000 / (窗口ms * 槽数)，四舍五入 */
  speed.rpm = (uint16_t)((cnt * 60000UL + (SPEED_WINDOW_MS * SPEED_SLOTS) / 2) /
                         (SPEED_WINDOW_MS * SPEED_SLOTS));
}

const Speed_Data *Speed_GetData(void)
{
  return &speed;
}
