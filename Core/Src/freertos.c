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
#include "lvgl.h"
#include "lv_port_indev.h"
#include "lv_port_lcd_stm32.h"
#include "lv_port_other.h"
#include "usart.h"
#include "ir_storage.h"
#include "src/misc/lv_event_private.h"
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
  .stack_size = 768,
  .priority = (osPriority_t) osPriorityNormal,
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
  lv_tick_inc(1); // 每 1ms 触发一次
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

  lvglTaskHandle = osThreadNew(lvgl_main_task, NULL, &lvglTask_attributes);
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
  Buzzer_Beep(buzzer_handle, 20, 1000);
  vTaskDelay(500);
  // QueueHandle_t key_queue = Key_Event_Subscribe(key_handle);
  // QueueHandle_t ir_queue = IR_Event_Subscribe(ir_handle);
  // key_event_t key_event;
  // ir_event_t ir_event = {0};
  IR_Receive_Start(ir_handle);
  /* Infinite loop */
  for(;;)
  {
    // if (xQueueReceive(key_queue, &key_event, 0) == pdTRUE)
    // {
    //
    //   if (key_event.key_type == KEY_ENTER && key_event.key_action == KEY_RELEASE)
    //   {
    //     xQueueReceive(ir_queue, &ir_event, 0);
    //     if (ir_event.len == 0)
    //     {
    //       Buzzer_Beep(buzzer_handle, 50, 262);
    //       elog_info(TAG, "No IR data");
    //     }
    //     else
    //     {
    //       IR_Receive_Stop(ir_handle);
    //       IR_Send(ir_handle, ir_event.data, ir_event.len);
    //       Buzzer_Beep(buzzer_handle, 50, 392);
    //     }
    //   }
    // }
    osDelay(10);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static void btn2_event_handler(lv_event_t* e)
{
  ir_event_t ir_event = {0};
  lv_event_code_t code = lv_event_get_code(e);
  uint16_t key_id;
  if (code == LV_EVENT_CLICKED)
  {
    xQueueReceive(e->user_data, &ir_event, 0);
    if (ir_event.len == 0)
    {
      Buzzer_Beep(buzzer_handle, 50, 262);
      elog_info(TAG, "No IR data");
    }
    else
    {
      IR_Receive_Stop(ir_handle);
      Storage_AddKey(storage_handle, 1, "ir_data", ir_event.data, ir_event.len, &key_id);
      Buzzer_Beep(buzzer_handle, 50, 392);
      IR_Event_Unsubscribe(ir_handle, e->user_data);
    }
    // 在此处添加你的业务逻辑，如跳转页面、开关功能等
  }
}

static void btn3_event_handler(lv_event_t* e)
{
  static uint8_t id = 0;
  uint16_t remote_id;
  char name[10];
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
  {
    sprintf(name, "remote%d", id);
    Storage_AddRemote(storage_handle, name, &remote_id);
    elog_info(TAG, "add remote id = %d, name = %s", remote_id, name);
  }
}

static void btn1_event_handler(lv_event_t* e)
{
  remote_info_t list[10];
  uint16_t count;
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
  {
    Storage_GetRemoteList(storage_handle, list, 10, &count);
    printf("total count = %d\r\n",  count);
    for (int i = 0; i < count; ++i)
    {
      printf("list id: %d, name: %s, count: %d\r\n", list[i].id, list[i].name, list[i].key_count);
    }
  }
}

static void btn4_event_handler(lv_event_t* e)
{
  key_info_t list[10];
  uint16_t count;
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
  {
    // Storage_GetRemoteList(storage_handle, list, 10, &count);
    // Storage_DeleteRemote(storage_handle, count);
    Storage_GetKeyList(storage_handle, 1, list, 10, &count);
    Storage_DeleteKey(storage_handle, 1, list[count - 1].id);
  }
}

static void btn5_event_handler(lv_event_t* e)
{
  key_info_t list[10];
  uint16_t count;
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED)
  {
    Storage_GetKeyList(storage_handle, 1, list, 10, &count);
    if (count > 0)
    {
      key_data_t* temp_data = pvPortMalloc(sizeof(key_data_t));
      Storage_GetKeyData(storage_handle, 1, list[count - 1].id, temp_data);
      IR_Send(ir_handle, temp_data->data, temp_data->data_len);
      vPortFree(temp_data);
    }
  }
}

void lvgl_main_task(void *pvParameters)
{
  extern lv_indev_t * indev_keypad;
  // 1. LVGL 核心初始化
  lv_init();
  lv_delay_set_cb(lv_port_delay_cb);
  // 2. 初始化显示驱动 (你需要根据你的屏幕驱动芯片实现 flush_cb)
  lv_port_display_init();
  lv_port_indev_init();

  lv_obj_t * label1 = lv_label_create(lv_screen_active());
  lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
  lv_label_set_text(label1, "Misaka LVGL Demo");
  lv_obj_set_width(label1, 150);  /*Set smaller width to make the lines wrap*/
  lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* button1 = lv_button_create(lv_screen_active());
  lv_obj_set_width(button1, 60);
  lv_obj_set_height(button1, 30);
  lv_obj_align(button1, LV_ALIGN_CENTER, 0, 40);

  lv_obj_t * button1_label = lv_label_create(button1);
  lv_label_set_text(button1_label, "Find");
  lv_obj_align(button1_label, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* button2 = lv_button_create(lv_screen_active());
  lv_obj_set_width(button2, 60);
  lv_obj_set_height(button2, 30);
  lv_obj_align(button2, LV_ALIGN_CENTER, 0, -40);

  lv_obj_t * button2_label = lv_label_create(button2);
  lv_label_set_text(button2_label, "Save");
  lv_obj_align(button2_label, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* button3 = lv_button_create(lv_screen_active());
  lv_obj_set_width(button3, 60);
  lv_obj_set_height(button3, 30);
  lv_obj_align(button3, LV_ALIGN_CENTER, -80, 40);

  lv_obj_t * button3_label = lv_label_create(button3);
  lv_label_set_text(button3_label, "Add");
  lv_obj_align(button3_label, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* button4 = lv_button_create(lv_screen_active());
  lv_obj_set_width(button4, 60);
  lv_obj_set_height(button4, 30);
  lv_obj_align(button4, LV_ALIGN_CENTER, 80, 40);

  lv_obj_t * button4_label = lv_label_create(button4);
  lv_label_set_text(button4_label, "Remove");
  lv_obj_align(button4_label, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* button5 = lv_button_create(lv_screen_active());
  lv_obj_set_width(button5, 60);
  lv_obj_set_height(button5, 30);
  lv_obj_align(button5, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t * button5_label = lv_label_create(button5);
  lv_label_set_text(button5_label, "Send");
  lv_obj_align(button5_label, LV_ALIGN_CENTER, 0, 0);

  lv_group_t *my_group = lv_group_create();
  lv_group_add_obj(my_group, button1);
  lv_group_add_obj(my_group, button2);
  lv_group_add_obj(my_group, button3);
  lv_group_add_obj(my_group, button4);
  lv_group_add_obj(my_group, button5);
  lv_indev_set_group(indev_keypad, my_group);

  QueueHandle_t ir_queue = IR_Event_Subscribe(ir_handle);

  lv_obj_add_event_cb(button2, btn2_event_handler, LV_EVENT_CLICKED, ir_queue);
  lv_obj_add_event_cb(button1, btn1_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(button3, btn3_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(button4, btn4_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(button5, btn5_event_handler, LV_EVENT_CLICKED, NULL);

  while(1) {
    // 4. 每 5ms~30ms 调用一次处理函数
      lv_lock();
      lv_timer_handler();
      lv_unlock();
    vTaskDelay(pdMS_TO_TICKS(3));
  }
}
/* USER CODE END Application */

