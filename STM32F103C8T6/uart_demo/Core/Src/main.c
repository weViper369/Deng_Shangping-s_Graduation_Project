/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "maix_link.h"
#include "gate.h"
#include "sensor.h"
#include "fsm.h"
#include "parking_db.h"
#include "billing.h"
#include "ssd1306.h"
#include "ui.h"
#include "btn.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
volatile uint32_t g_ms = 0; // 1ms的状态机
volatile uint8_t sec_tick = 0; // 1s tick
volatile uint8_t lane_hint = 0; // 0=IN, 1=OUT（由对射触发决定下一次车牌属于哪边）
volatile uint32_t lane_hint_t = 0;
static uint32_t ui_t0 = 0;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define debug 0

maix_plate_event_t e;
gate_cfg_t gate_cfg = {
  .pwm_us_open  = 2000,
  .pwm_us_close = 1000,
  .move_time_ms = 800,
  .invert       = 0
};
sensor_cfg_t scfg = { 
  .debounce_ms = 30 
};
sensor_event_t ein, eout;
billing_cfg_t bc = { 
  .free_s = 0*60,
  .unit_s = 30*60,
  .unit_fee_cents = 100 
};
ocr_ctrl_t g_ocr = {
  .in.pending = 0,
  .in.expected_t0 = 0,
  .in.last_req_ms = 0,
  .out.pending = 0,
  .out.expected_t0 = 0,
  .out.last_req_ms = 0,
  .in_latch      = 0,
  .out_latch     = 0,
  .req_timeout_ms = 15000,
  .retry_interval_ms = 4500,  // 建议略大于相机 ARM_WINDOW 4000
};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
uint8_t uart1_rx_byte;
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void debug_printf_time(void)
{
  static uint32_t last = 0;
  if (g_ms - last >= 1000)
  {
      last = g_ms;
      printf("g_ms=%d\r\n", (unsigned long)g_ms);
  }
}

void ui_proc(void)
{
  if (g_ms - ui_t0 >= 200)
  {
      ui_t0 = g_ms;
      ui_tick(g_ms);
  }
}

void sensor(void)
{
  sensor_get_event(SENSOR_IN, &ein);
  if (ein.rising)  
  { 
    fsm_on_ir_in_blocked(1); 
    lane_hint = 0; 
    lane_hint_t = g_ms; 
  }
  if (ein.falling) 
  { 
    fsm_on_ir_in_blocked(0); 
  }
  
  sensor_get_event(SENSOR_OUT, &eout);
  if (eout.rising)  
  { 
    fsm_on_ir_out_blocked(1); 
    lane_hint = 1; 
    lane_hint_t = g_ms; 
  }
  if (eout.falling) 
  { 
    fsm_on_ir_out_blocked(0); 
  }    

  // ==== 入口 clear->blocked：触发一次 IN OCR 请求 ====
  if (ein.rising && g_ocr.in_latch == 0)
  {
    g_ocr.in_latch = 1;
    // 只启动IN槽（不影响OUT槽）
    g_ocr.in.pending = 1;
    g_ocr.in.expected_t0 = g_ms;
    maix_link_send_req_ocr(0);
    g_ocr.in.last_req_ms = g_ms;     
    g_ocr.in_pending = 1; 
    printf("[OCR] REQ IN\r\n");
  }
  if (ein.falling)
  {
    g_ocr.in_latch = 0;
    g_ocr.in_pending = 0; 
  } 
  // ==== 出口 clear->blocked：触发一次 OUT OCR 请求 ====
  if (eout.rising && g_ocr.out_latch == 0)
  {
    g_ocr.out_latch = 1;
    g_ocr.out.pending  = EXP_OUT;
    g_ocr.out.expected_t0 = g_ms;
    maix_link_send_req_ocr(1); // 1=OUT
    g_ocr.out.last_req_ms = g_ms;    
    g_ocr.out_pending = 1;     
    printf("[OCR] REQ OUT\r\n");
  }
  if (eout.falling) 
  {
    g_ocr.out_latch = 0;
    g_ocr.out_pending = 0; 
  }
  
  // ==== blocked期间“超时重试”：还没拿到结果，就每隔 OCR_RETRY_MS 再发一次 ====
  if (g_ocr.in_latch && g_ocr.in.pending && (g_ms - g_ocr.in.last_req_ms >= g_ocr.retry_interval_ms))
    {
      maix_link_send_req_ocr(0);
      g_ocr.in.last_req_ms = g_ms;
      printf("[OCR] REQ IN (retry)\r\n");
    }

    if (g_ocr.out_latch && g_ocr.out.pending && (g_ms - g_ocr.out.last_req_ms >= g_ocr.retry_interval_ms))
    {
      maix_link_send_req_ocr(1);
      g_ocr.out.last_req_ms = g_ms;
      printf("[OCR] REQ OUT (retry)\r\n");
    }  

  // 超时：请求发出后太久没结果，清空 expected_lane
  if (g_ocr.in.pending && (g_ms - g_ocr.in.expected_t0 > g_ocr.req_timeout_ms))
  {
    printf("[OCR] IN timeout\r\n");
    g_ocr.in.pending = 0;
  }
  
  if (g_ocr.out.pending && (g_ms - g_ocr.out.expected_t0 > g_ocr.req_timeout_ms))
  {
    printf("[OCR] OUT timeout\r\n");
    g_ocr.out.pending = 0;
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
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);

  ui_init();
  ssd1306_fill(0);
  // ssd1306_draw_str(0, 0, "HELLO");
  // ssd1306_draw_str(0, 0, "0");
  // ssd1306_draw_str(0, 16, "I2C OK 0x3C");
  // ssd1306_update();
  maix_link_init(&g_ms);
  gate_init(&g_ms, &gate_cfg);
  sensor_init(&g_ms, &scfg);
  db_init();
  billing_set_cfg(&bc);
  fsm_init(&g_ms);

  HAL_TIM_Base_Start_IT(&htim2);
  HAL_TIM_Base_Start_IT(&htim4);
  
  printf("init ok \r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    maix_link_poll();
    sensor_poll();
    sensor();
    btn_poll();
    if (maix_link_get_plate(&e))
    {
      if (e.lane == 0)  // IN
      {
        if (g_ocr.in.pending) 
        {
          fsm_on_plate(e.plate, e.conf, LANE_IN);
          g_ocr.in.pending = 0; 
        } 
        else 
        {
          printf("[OCR] got IN but not pending, drop %s\r\n", e.plate);
        }
      } 
      else if (e.lane == 1) // OUT
      { 
        if (g_ocr.out.pending) 
        {
          fsm_on_plate(e.plate, e.conf, LANE_OUT);
          g_ocr.out.pending = 0;
        } 
        else 
        {
          printf("[OCR] got OUT but not pending, drop %s\r\n", e.plate);
        }
      }    
    }

    fsm_step();
    ui_proc();
    
    #if debug
    debug_printf_time();
    #endif
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
}

/* USER CODE BEGIN 4 */
// 串口回调
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
      maix_link_on_rx_byte(uart1_rx_byte);
      HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
    }
}
// 定时器回调
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) g_ms++;
    if (htim->Instance == TIM4) sec_tick = 1;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  btn_handle_exti(GPIO_Pin, g_ms);
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
