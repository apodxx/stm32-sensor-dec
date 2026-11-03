/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "max30102.h"
#include "dht11.h"
#include "light.h"
#include "ui.h"
#include "i2c1.h"
#include "servo.h"
#include "buzzer.h"
#include "mpu6050.h"
#include "speed.h"
#include "encoder.h"
#include "link.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SERVO_DEMO_SWEEP  1   /* 1 = 开机自动来回扫动；0 = 只保留接口，由你调用 */
#define BUZZER_BEAT_SOUND 1   /* 1 = 每检测到一次心跳响一下；0 = 只做开机提示音 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t lastPollTick;  // 上次读传感器 FIFO 的时刻
static uint32_t lastEnvTick;   // 上次调用 DHT11 任务的时刻
static uint32_t lastLightTick; // 上次调用光照采样任务的时刻
static uint32_t lastMpuTick;   // 上次读 MPU6050 的时刻
static uint32_t lastSpeedTick; // 上次处理测速窗口的时刻
static uint32_t lastLinkTick;  // 上次解析串口命令的时刻
static int16_t  lastDetent;    // 上次记录的旋钮档位
#if SERVO_DEMO_SWEEP
static uint32_t lastServoTick; // 舵机扫动节拍
static uint8_t  servoAngle;    // 当前角度
static int8_t   servoDir = 1;  // 方向
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C2_Init();
  /* USER CODE BEGIN 2 */
  MX_I2C1_Init();  /* 血氧模块专用 I2C1：PB6=SCL、PB7=SDA */
  HAL_Delay(20); // 单片机启动比OLED上电快,需要延迟等待一下
  OLED_Init();

  /* 开机 LOGO */
  OLED_NewFrame();
  OLED_DrawImage((128 - bilibiliImg.w) / 2, 0, &bilibiliImg, OLED_COLOR_NORMAL);
  OLED_ShowFrame();
  HAL_Delay(600);

  DHT11_Init();      /* 温湿度传感器（PB0） */
  Light_Init();      /* 光敏电阻（PA0 / ADC1_IN0） */
  Servo_Init();      /* 舵机 PWM（PA1 / TIM2_CH2），上电回中位 */
  Buzzer_Init();     /* 蜂鸣器（PA2） */

  /* 开机提示音：响两声，确认蜂鸣器接对了 */
  Buzzer_On();
  HAL_Delay(80);
  Buzzer_Off();
  HAL_Delay(80);
  Buzzer_On();
  HAL_Delay(80);
  Buzzer_Off();

  MAX30102_Init();   /* 心率血氧，失败也没关系：Process() 会每秒自动重连 */
  MPU6050_Init();    /* 六轴传感器，挂在 I2C1 上（0x68） */
  Speed_Init();      /* 测速模块（PB1 下降沿中断） */
  Encoder_Init();    /* 旋转编码器（PA5/PA6） */
  Link_Init();       /* 与 ESP32 通信的 USART1（PA9/PA10） */
  UI_Init();         /* 界面从第一页开始 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* 每 10ms 取一次传感器 FIFO（输出 25Hz，留足余量） */
    if (now - lastPollTick >= 10)
    {
      lastPollTick = now;
      MAX30102_Process();
    }

    /* DHT11 上限 1Hz，这里 1s 调一次，内部还会再限速到 2s */
    if (now - lastEnvTick >= 1000)
    {
      lastEnvTick = now;
      DHT11_Task();
    }

    /* 光照采样：200ms 一次，内部 8 次平均 */
    if (now - lastLightTick >= 200)
    {
      lastLightTick = now;
      Light_Task();
    }

    /* MPU6050：100ms 读一次（100Hz 输出，够用） */
    if (now - lastMpuTick >= 100)
    {
      lastMpuTick = now;
      MPU6050_Task();
    }

    /* 测速：100ms 查一次窗口，内部按 500ms 统计 */
    if (now - lastSpeedTick >= 100)
    {
      lastSpeedTick = now;
      Speed_Task();
    }

    /* 串口命令：5ms 解析一次（ESP32 / 上位机） */
    if (now - lastLinkTick >= 5)
    {
      lastLinkTick = now;
      Link_Task();
    }

    /* 旋钮切页：档位变化就切换界面（正转下一页、反转上一页） */
    {
      int16_t d = Encoder_GetDetents();

      if (d != lastDetent)
      {
        int16_t p = (int16_t)UI_GetPage() + (d - lastDetent);
        lastDetent = d;

        while (p < 0)
          p += UI_PAGE_COUNT;
        UI_SetPage((UI_Page)(p % UI_PAGE_COUNT));
      }
    }

#if SERVO_DEMO_SWEEP
    /* 舵机演示：每 20ms 走 1°，来回扫动；不需要就把它删掉或关掉宏 */
    if (now - lastServoTick >= 20)
    {
      lastServoTick = now;

      if (servoDir > 0)
      {
        if (servoAngle >= 180) servoDir = -1;
        else servoAngle++;
      }
      else
      {
        if (servoAngle == 0) servoDir = 1;
        else servoAngle--;
      }

      Servo_SetAngle(servoAngle);
    }
#endif

    /* 蜂鸣器：处理鸣叫结束 + 心跳打拍子 */
    Buzzer_Task();
#if BUZZER_BEAT_SOUND
    if (MAX30102_TakeBeatEvent())
      Buzzer_Beep(30);
#endif

    /* 界面：25fps 刷屏 + 5s 自动换页 */
    UI_Task();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * @brief 外部中断统一分发（HAL 的弱回调，放在这里避免各驱动重复定义）
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  Speed_OnExti(GPIO_Pin);   /* PB1：测速脉冲 */
  Encoder_OnExti(GPIO_Pin); /* PA5/PA6：旋钮 A/B 相 */
}

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
