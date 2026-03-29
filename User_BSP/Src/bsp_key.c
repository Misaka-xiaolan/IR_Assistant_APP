//
// Created by skinm on 2026/3/17.
//

#include "bsp_key.h"
#include "i2c.h"

#define TAG "Key"

#define MAX_SUBSCRIBERS 10

/* 内部数据结构 */
struct Key_device_t
{
    // 硬件接口
    I2C_HandleTypeDef* i2c_handle; // 实际的I2C句柄

    // 配置参数
    key_config_t config;

    // 任务相关
    TaskHandle_t key_task;

    // 订阅者
    key_subscriber_t subscribers[MAX_SUBSCRIBERS];

    // 统计信息
    uint32_t i2c_error_count;

    // 状态标志
    uint8_t initialized;
};


/* 静态函数声明 */
static uint8_t Key_I2C_Read_Value(Key_Handle handle);
static void Key_Task(void* params);

/* 初始化 */
Key_Handle Key_Init(key_config_t* config)
{
    if (!config) return NULL;
    if (!config->i2c_handle) return NULL;

    Key_Handle dev = (Key_Handle)pvPortMalloc(sizeof(struct Key_device_t));
    if (!dev) return NULL;

    memset(dev, 0, sizeof(struct Key_device_t));

    // 复制配置
    memcpy(&dev->config, config, sizeof(key_config_t));
    dev->i2c_handle = config->i2c_handle;
    // 设置默认值
    if (dev->config.i2c_addr == 0) dev->config.i2c_addr = 0x40;
    if (dev->config.timeout_ms == 0) dev->config.timeout_ms = 1000;
    if (dev->config.long_press_time_ms == 0) dev->config.long_press_time_ms = 500;

    // 创建按键任务
    if (xTaskCreate(Key_Task, "Key_Task", 128 * 8, dev, 28, &dev->key_task) != pdPASS)
    {
        vPortFree(dev);
        elog_error(TAG, "Key task create FAIL");
        return NULL;
    }

    dev->initialized = 1;
    elog_info(TAG, "Key device initialized");

    return dev;
}

/* I2C读取按键值 */
static uint8_t Key_I2C_Read_Value(Key_Handle handle)
{
    uint8_t value = 0xFF;
    if (HAL_I2C_Mem_Read(handle->i2c_handle,
                         handle->config.i2c_addr,
                         0x00,
                         I2C_MEMADD_SIZE_8BIT,
                         &value, 1,
                         handle->config.timeout_ms) != HAL_OK)
    {
        handle->i2c_error_count++;
        elog_error(TAG, "I2C read FAIL");
        return 0xFF;
    }
    return value;
}

void Key_I2C_ISR_Callback(Key_Handle handle)
{
    if (!handle || !handle->initialized || handle->key_task == NULL) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(handle->key_task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* 订阅按键事件 */
QueueHandle_t Key_Event_Subscribe(Key_Handle handle)
{
    if (!handle || !handle->initialized) return NULL;

    for(int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if(!handle->subscribers[i].active)
        {
            handle->subscribers[i].queue = xQueueCreate(10, sizeof(key_event_t));
            handle->subscribers[i].active = 1;
            handle->subscribers[i].task_handle = xTaskGetCurrentTaskHandle();
            elog_info(TAG, "\"%s\" subscribed key event", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
            return handle->subscribers[i].queue;
        }
    }
    elog_error(TAG, "\"%s\" key event subscribe FAIL", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
    return NULL;
}

/* 取消订阅按键事件 */
void Key_Event_Unsubscribe(Key_Handle handle, QueueHandle_t queue)
{
    if (!handle || !handle->initialized) return;
    if (!queue)
    {
        elog_warn(TAG, "unsubscribe with NULL queue");
        return;
    }

    for(int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if(handle->subscribers[i].active && handle->subscribers[i].queue == queue)
        {
            vQueueDelete(handle->subscribers[i].queue);
            handle->subscribers[i].queue = NULL;
            handle->subscribers[i].active = 0;
            handle->subscribers[i].task_handle = NULL;
            elog_info(TAG, "\"%s\" unsubscribed key event", pcTaskGetTaskName(NULL));
            return;
        }
    }
    elog_warn(TAG, "queue not found in subscribers");
}

/* 按键任务函数 */
static void Key_Task(void* params)
{
    Key_Handle handle = (Key_Handle)params;
    uint32_t key_keeptime = 0;
    uint8_t key_value = 0;
    uint8_t key_last = 0;
    key_event_t key_event;

    elog_info(TAG, "Key task started");

    while (1)
    {
        // 等待中断通知

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        key_value = Key_I2C_Read_Value(handle);

        // 检查 I2C 读取是否成功
        if (key_value == 0xFF)
        {
            elog_warn(TAG, "I2C read error, skip this cycle");
            continue;
        }

        if (key_value != 0x1F)  // 按键按下
        {
            elog_verbose(TAG, "Some Key pressed, value: 0x%02X", key_value);
            key_keeptime = xTaskGetTickCount();
            key_last = key_value;
            key_event.key_action = KEY_PRESS;
            key_event.key_pressed_time = 0;
            switch (key_value & 0x1F)
            {
            case 0x1E:
                elog_info(TAG, "Key UP long pressed");
                key_event.key_type = KEY_UP;
                break;
            case 0x1D:
                elog_info(TAG, "Key LEFT long pressed");
                key_event.key_type = KEY_LEFT;
                break;
            case 0x1b:
                elog_info(TAG, "Key DOWN long pressed");
                key_event.key_type = KEY_DOWN;
                break;
            case 0x17:
                elog_info(TAG, "Key RIGHT long pressed");
                key_event.key_type = KEY_RIGHT;
                break;
            case 0x0F:
                elog_info(TAG, "Key ENTER long pressed");
                key_event.key_type = KEY_ENTER;
                break;
            default:
                key_event.key_type = KEY_UNKOWN;
                break; // 不识别的按键
            }
        }
        else
        {
            elog_verbose(TAG, "Some Key released");
            key_keeptime = xTaskGetTickCount() - key_keeptime;
            key_event.key_action = KEY_RELEASE;
            key_event.key_pressed_time = key_keeptime;
        }
        // 发送事件给所有订阅者
        for(int i = 0; i < MAX_SUBSCRIBERS; i++)
        {
            if(handle->subscribers[i].active)
            {
                if (xQueueSend(handle->subscribers[i].queue, &key_event, 0) == pdFAIL)
                {
                    elog_warn(TAG, "\"%s\" subscribe queue full", pcTaskGetTaskName(handle->subscribers[i].task_handle));
                }
            }
        }
    }
}

