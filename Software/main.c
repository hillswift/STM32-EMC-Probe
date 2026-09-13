/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "spi.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ssd1306.h"
/* USER CODE END Includes */




/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

#define KEY_PRESSED   GPIO_PIN_RESET
#define KEY_RELEASED  GPIO_PIN_SET

#define ADC_RESOLUTION  4096.0f
#define V_REF           3.3f

// 两档总增益
#define GAIN_LOW    20.0f    // 低档位：20倍
#define GAIN_HIGH   200.0f   // 高档位：硬件202倍，软件取整200倍

uint8_t  g_gain_level = 0;    // 0=低增益，1=高增益
float    g_current_gain = GAIN_LOW; // 当前生效增益

#define SAMPLE_CNT      20

float g_bias_voltage;
float g_signal_mv;
#define SAT_THRESHOLD  2.4f    // 饱和判定阈值（输出电压超过2.4V视为饱和）
uint8_t g_sat_flag = 0;       // 饱和标志：0=正常，1=饱和
// ================== 滑动平均滤波 ==================
#define WINDOW_SIZE     16      // 滑动窗口大小，越大越平稳但响应越慢
float g_adc_window[WINDOW_SIZE];
uint8_t g_win_idx = 0;
float g_win_sum = 0;

// ================== 峰值保持功能 ==================
float g_peak_mv = 0;
uint8_t g_peak_mode = 0;     // 0=实时测量模式，1=峰值保持模式


// ================== 蜂鸣器声音提示 ==================
#define BEEP_GPIO_PORT  GPIOB
#define BEEP_GPIO_PIN   GPIO_PIN_7    // 实际引脚是PB7
#define BEEP_ON()       HAL_GPIO_WritePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN, GPIO_PIN_SET)   // 高电平导通
#define BEEP_OFF()      HAL_GPIO_WritePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN, GPIO_PIN_RESET) // 低电平截止

uint16_t g_beep_cnt = 0;    // 蜂鸣器计时计数器
uint16_t g_beep_interval = 1000; // 鸣叫间隔(ms)，信号越强间隔越小

/* USER CODE END PV */





/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
uint8_t Key_Scan(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
/* USER CODE END PFP */


/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// 滑动平均滤波：输入新采样值，输出滤波后的值
float slide_filter(float new_val)
{
    g_win_sum -= g_adc_window[g_win_idx];   // 移除窗口中最旧的数值
    g_adc_window[g_win_idx] = new_val;       // 填入新的采样值
    g_win_sum += new_val;                    // 更新累加和

    g_win_idx++;
    if(g_win_idx >= WINDOW_SIZE)
        g_win_idx = 0;

    return g_win_sum / WINDOW_SIZE;
}

float adc_get_avg(void)
{
    uint32_t sum = 0;
    for(uint8_t i = 0; i < SAMPLE_CNT; i++)
    {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 10);
        sum += HAL_ADC_GetValue(&hadc1);
    }
    float adc_avg = (float)sum / SAMPLE_CNT;
    return adc_avg * V_REF / ADC_RESOLUTION;
}

void signal_calculate(void)
{
    float adc_raw = adc_get_avg();          // 原始多次采样平均
    float adc_voltage = slide_filter(adc_raw); // 经过滑动滤波
    float signal_amp = adc_voltage - g_bias_voltage;

    // 饱和判定
    g_sat_flag = (adc_voltage >= SAT_THRESHOLD) ? 1 : 0;

    if(signal_amp < 0) signal_amp = 0;
    g_signal_mv = (signal_amp / g_current_gain) * 1000;

    // 峰值模式：更新历史最大值
    if(g_peak_mode)
    {
        if(g_signal_mv > g_peak_mv)
            g_peak_mv = g_signal_mv;
    }
}


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
  MX_ADC1_Init();
  MX_SPI1_Init();
/* USER CODE BEGIN 2 */
// OLED初始化
OLED_Init();
HAL_Delay(100);

// ADC校准
HAL_ADCEx_Calibration_Start(&hadc1);
HAL_Delay(10);

// 默认低增益档：PB0输出低电平
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

// 等硬件稳定后采零点
HAL_Delay(500);
g_bias_voltage = adc_get_avg();

/* USER CODE END 2 */









  /* Infinite loop */
/* USER CODE BEGIN WHILE */
while (1)
{
    /* USER CODE END WHILE */

/* USER CODE BEGIN 3 */
// KEY1(PB1)：切换增益档位
if(Key_Scan(GPIOB, GPIO_PIN_1))
{
    g_gain_level = !g_gain_level;
    
    if(g_gain_level == 0)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
        g_current_gain = GAIN_LOW;
    }
    else
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
        g_current_gain = GAIN_HIGH;
    }
    
    // 切换档位后重新校准零点 + 重置峰值
    HAL_Delay(100);
    g_bias_voltage = adc_get_avg();
    if(g_peak_mode)
        g_peak_mv = g_signal_mv;
}

// KEY2(PB2)：手动重新校准零点
if(Key_Scan(GPIOB, GPIO_PIN_2))
{
    g_bias_voltage = adc_get_avg();
    if(g_peak_mode)
        g_peak_mv = g_signal_mv; // 校准后重置峰值
}

// KEY3(PB10)：切换峰值保持模式
if(Key_Scan(GPIOB, GPIO_PIN_10))
{
    g_peak_mode = !g_peak_mode;
    if(g_peak_mode)
        g_peak_mv = g_signal_mv; // 进入峰值模式，以当前值为起点
    else
        g_peak_mv = 0;
}

// 计算信号 + 滤波 + 峰值更新
signal_calculate();

// ================== 蜂鸣器声音提示 ==================
if(g_signal_mv < 0.3f)
{
    // 信号极弱：不鸣叫
    BEEP_OFF();
    g_beep_cnt = 0;
}
else if(g_signal_mv < 1.0f)
{
    // 弱信号：500ms间隔
    g_beep_cnt += 100;
    if(g_beep_cnt >= 500)
    {
        g_beep_cnt = 0;
        HAL_GPIO_TogglePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
    }
}
else if(g_signal_mv < 3.0f)
{
    // 中等信号：200ms间隔
    g_beep_cnt += 100;
    if(g_beep_cnt >= 200)
    {
        g_beep_cnt = 0;
        HAL_GPIO_TogglePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
    }
}
else
{
    // 强信号/饱和：100ms间隔，鸣叫最急促
    g_beep_cnt += 100;
    if(g_beep_cnt >= 100)
    {
        g_beep_cnt = 0;
        HAL_GPIO_TogglePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN);
    }
}

// ================== OLED显示 ==================
OLED_Clear();

// 第0行：实时信号 / 峰值
if(g_peak_mode)
{
    OLED_ShowString(0, 0, (uint8_t *)"Peak:", 12);
    OLED_ShowNum(36, 0, g_peak_mv, 2, 12);
    OLED_ShowString(66, 0, (uint8_t *)"mV", 12);
}
else
{
    OLED_ShowString(0, 0, (uint8_t *)"Signal:", 12);
    OLED_ShowNum(42, 0, g_signal_mv, 2, 12);
    OLED_ShowString(72, 0, (uint8_t *)"mV", 12);
}

// 第1行：档位 + 峰值标识 + 饱和提示
if(g_gain_level == 0)
    OLED_ShowString(0, 1, (uint8_t *)"G:x20", 12);
else
    OLED_ShowString(0, 1, (uint8_t *)"G:x200", 12);

if(g_peak_mode)
    OLED_ShowString(36, 1, (uint8_t *)"PEAK", 12);

if(g_sat_flag)
    OLED_ShowString(72, 1, (uint8_t *)"SAT", 12);

HAL_Delay(100);

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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
// 带消抖的按键扫描，返回1表示按下一次
uint8_t Key_Scan(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    static uint8_t key_state = 1;
    if(key_state && (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == KEY_PRESSED))
    {
        HAL_Delay(20); // 20ms消抖
        if(HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == KEY_PRESSED)
        {
            key_state = 0;
            return 1;
        }
    }
    else if(HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == KEY_RELEASED)
    {
        key_state = 1;
    }
    return 0;
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

#ifdef  USE_FULL_ASSERT
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
