/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "freertos_debug.h"
#include "bsp_key.h"
#include "bsp_dht20.h"
#include "bsp_buzzer.h"
#include "bsp_ir.h"
#include "bsp_norflash.h"
#include "bsp_uart.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "ir_storage.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TAG "main"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
Key_Handle key_handle;
DHT20_Handle dht20_handle;
Buzzer_Handle buzzer_handle;
IR_Handle ir_handle;
UART_Handle uart_lte_handle;
UART_Handle uart_debug_handle;
Storage_Handle storage_handle;
extern DMA_HandleTypeDef hdma_tim2_ch3_up;
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
osThreadId_t lvglTaskHandle;
const osThreadAttr_t lvglTask_attributes = {
  .name = "lvglTask",
  .stack_size = 4096,
  .priority = (osPriority_t) 12,
};
void lvgl_main_task(void *pvParameters);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationTickHook(void);

/* USER CODE BEGIN 3 */
void vApplicationTickHook( void )
{
   /* This function will be called by each tick interrupt if
   configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
   added here, but the tick hook is called from an interrupt context, so
   code must not attempt to block, and only the interrupt safe FreeRTOS API
   functions can be used (those that end in FromISR()). */
}
/* USER CODE END 3 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  UART_config_t uart_config =
  {
    .uart_handle = &huart2
  };
  uart_debug_handle = UART_DMA_Init(&uart_config);
  uart_config.uart_handle = &huart1;
  uart_lte_handle = UART_DMA_Init(&uart_config);

  key_config_t key_config =
  {
    .i2c_handle = &hi2c1,
    .timeout_ms = 100,
    .long_press_time_ms = 500
  };
  key_handle = Key_Init(&key_config);

  DHT20_config_t dht20_config =
  {
    .delay_ms = vTaskDelay,
    .i2c_handle = &hi2c1,
    .measure_interval_ms = 1000,
    .timeout_ms = 500
  };
  dht20_handle = DHT20_Init(&dht20_config);

  buzzer_config_t buzzer_config =
  {
    .pwm_tim = &htim3,
    .default_beep_freq = 1000,
    .default_beep_ms = 100,
    .max_queue_size = 10,
    .pwm_channel = TIM_CHANNEL_3,
    .type = BUZZER_TYPE_PASSIVE
  };
  buzzer_handle = Buzzer_Init(&buzzer_config);

  ir_config_t ir_config =
  {
    .recv_tim_handle = &htim11,
    .recv_tim_channel = TIM_CHANNEL_1,
    .recv_gpio_port = GPIOB,
    .recv_gpio_pin = GPIO_PIN_9,
    .send_tim_handle = &htim2,
    .send_dma_handle = &hdma_tim2_ch3_up,
    .send_tim_channel = TIM_CHANNEL_1,
    .send_gpio_port = GPIOA,
    .send_gpio_pin = GPIO_PIN_5,
    .timeout_ms = 400,
    .queue_depth = 3
  };
  ir_handle = IR_Init(&ir_config);

  storage_handle = Storage_Init();
  // Storage_Format(storage_handle);
  FreertosDebug_Init();
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */

  /* Infinite loop */
  for(;;)
  {
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/


/* USER CODE END Application */

