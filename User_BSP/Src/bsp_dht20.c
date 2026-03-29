//
// Created by skinm on 2026/3/18.
//

#include "bsp_dht20.h"
#include "bsp_buzzer.h"

extern Buzzer_Handle buzzer_handle;

#define TAG "DHT20"

/* 寄存器定义 */
#define DHT20_I2C_ADDR      0x70
#define DHT20_CMD_TRIGGER   0xAC
#define DHT20_CMD_STATUS    0x71

#define MAX_SUBSCRIBERS 10

/* 内部数据结构 */
struct DHT20_device_t
{
    // 配置参数
    DHT20_config_t config;
    DHT20_mode_t current_mode;

    // 数据缓存
    DHT20_data_t cached_data;
    SemaphoreHandle_t data_mutex;

    // 测量任务相关
    TaskHandle_t measure_task;

    // 订阅者列表
    DHT20_subscriber_t subscribers[MAX_SUBSCRIBERS];

    // 统计信息
    uint32_t i2c_error_count;
    uint32_t crc_error_count;
    uint32_t read_count;

    // 状态标志
    uint8_t initialized;
    uint8_t measuring;
};

/* 静态函数声明 */
static DHT20_status_t read_raw_data(DHT20_Handle handle, uint8_t* buffer);
static DHT20_status_t parse_raw_data(uint8_t* raw, DHT20_data_t* data);
static DHT20_status_t trigger_measurement(DHT20_Handle handle);
static void DHT20_task(void* params);

/* 初始化 */
DHT20_Handle DHT20_Init(DHT20_config_t* config)
{
    if (!config) return NULL;
    if (!config->i2c_handle) return NULL;

    DHT20_Handle dev = (DHT20_Handle)pvPortMalloc(sizeof(struct DHT20_device_t));
    if (!dev) return NULL;

    memset(dev, 0, sizeof(struct DHT20_device_t));

    // 复制配置
    memcpy(&dev->config, config, sizeof(DHT20_config_t));
    if (dev->config.i2c_addr == 0) dev->config.i2c_addr = DHT20_I2C_ADDR;

    // 创建同步原语
    dev->data_mutex = xSemaphoreCreateMutex();

    if (!dev->data_mutex)
    {
        vPortFree(dev);
        elog_error(TAG, "DHT20 mutex allocate FAIL");
        return NULL;
    }

    // 检查设备是否校准
    if (!DHT20_IsCalibrated(dev))
    {
        elog_error(TAG, "DHT20 calibration FAIL");
        vSemaphoreDelete(dev->data_mutex);  // 释放互斥量
        vPortFree(dev);
        return NULL;
    }

    // 创建测量任务
    if (xTaskCreate(DHT20_task, "DHT20_Task", 512, dev, 5, &dev->measure_task) != pdPASS)
    {
        elog_error(TAG, "DHT20 task create FAIL");
        vSemaphoreDelete(dev->data_mutex);  // 释放互斥量
        vPortFree(dev);
        return NULL;
    }

    dev->initialized = 1;
    dev->current_mode = DHT20_MODE_CONTINUOUS;
    elog_info(TAG, "DHT20 device initialized");
    return dev;
}

// 订阅温湿度更新事件
QueueHandle_t DHT20_Event_Subscribe(DHT20_Handle handle)
{
    if (!handle || !handle->initialized) return NULL;

    for(int i = 0; i < MAX_SUBSCRIBERS; i++)
    {
        if(!handle->subscribers[i].active)
        {
            handle->subscribers[i].queue = xQueueCreate(10, sizeof(DHT20_data_t));
            handle->subscribers[i].active = 1;
            handle->subscribers[i].task_handle = xTaskGetCurrentTaskHandle();
            elog_info(TAG, "\"%s\" subscribed DHT20 event", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
            return handle->subscribers[i].queue;
        }
    }
    elog_error(TAG, "\"%s\" DHT20 event subscribe FAIL", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
    return NULL;
}

// 取消订阅温湿度更新事件
void DHT20_Event_Unsubscribe(DHT20_Handle handle, QueueHandle_t queue)
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
            elog_info(TAG, "\"%s\" unsubscribed DHT20 event", pcTaskGetTaskName(NULL));
            return;
        }
    }
    elog_warn(TAG, "queue not found in subscribers");
}

static void DHT20_task(void* params)
{
    DHT20_Handle dev = (DHT20_Handle)params;
    uint8_t raw_data[7];
    DHT20_data_t new_data;
    TickType_t last_wake_time = xTaskGetTickCount();
    elog_info(TAG, "DHT20 task started");
    while (1)
    {
        if (dev->current_mode == DHT20_MODE_CONTINUOUS)
        {
            // 连续模式：定时测量
            vTaskDelayUntil(&last_wake_time,
                            pdMS_TO_TICKS(dev->config.measure_interval_ms));

            // 触发测量
            if (trigger_measurement(dev) == DHT20_OK)
            {
                dev->config.delay_ms(80); // 等待测量完成

                // 读取数据
                if (read_raw_data(dev, raw_data) == DHT20_OK)
                {
                    parse_raw_data(raw_data, &new_data);
                    elog_verbose(TAG, "DHT20 Temp: %.2f, Humi: %.2f", new_data.temperature, new_data.humidity);
                    // 更新缓存
                    xSemaphoreTake(dev->data_mutex, portMAX_DELAY);
                    dev->cached_data = new_data;
                    dev->cached_data.timestamp = xTaskGetTickCount();
                    xSemaphoreGive(dev->data_mutex);
                    for(int i = 0; i < MAX_SUBSCRIBERS; i++)
                    {
                        if(dev->subscribers[i].active)
                        {
                            if (xQueueSend(dev->subscribers[i].queue, &new_data, 0) == pdFAIL)
                            {
                                elog_warn(TAG, "\"%s\" subscribe queue full", pcTaskGetTaskName(dev->subscribers[i].task_handle));
                            }
                        }
                    }
                }
            }
        }
        else if (dev->current_mode == DHT20_MODE_SLEEP)
        {
            vTaskDelay(1);
        }
    }
}

/* 触发测量 */
static DHT20_status_t trigger_measurement(DHT20_Handle handle)
{
    uint8_t cmd[3] = {DHT20_CMD_TRIGGER, 0x33, 0x00};

    // 发送触发命令
    if (HAL_I2C_Master_Transmit(handle->config.i2c_handle,
                                handle->config.i2c_addr, cmd, 3,
                                handle->config.timeout_ms) != HAL_OK)
    {
        handle->i2c_error_count++;
        return DHT20_I2C_ERROR;
    }

    return DHT20_OK;
}

/* 读取原始数据 */
static DHT20_status_t read_raw_data(DHT20_Handle handle, uint8_t* buffer)
{
    // 检查设备状态
    uint8_t status;
    if (DHT20_GetStatus(handle, &status) != DHT20_OK)
    {
        return DHT20_BUSY;
    }

    // 读取6字节数据
    if (HAL_I2C_Master_Receive(handle->config.i2c_handle,
                               handle->config.i2c_addr, buffer, 6,
                               handle->config.timeout_ms) != HAL_OK)
    {
        handle->i2c_error_count++;
        return DHT20_I2C_ERROR;
    }


    return DHT20_OK;
}

bool DHT20_IsCalibrated(DHT20_Handle handle)
{
    if (!handle)
    {
        return false;
    }

    uint8_t status;
    DHT20_status_t ret;

    for (int i = 0; i < 3; i++)
    {
        ret = DHT20_GetStatus(handle, &status);
        if (ret == DHT20_OK)
        {
            if (status & (1 << 3))
            {
                return true;
            }
            else
            {
                return false;
            }
        }

        if (handle->config.delay_ms)
        {
            handle->config.delay_ms(10);
        }
    }
    // 多次读取失败，认为设备未校准或不存在
    return false;
}

/* 解析原始数据 */
static DHT20_status_t parse_raw_data(uint8_t* raw, DHT20_data_t* data)
{
    uint32_t temp_raw, humi_raw;

    // 湿度: 20位数据
    humi_raw = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | ((raw[3] & 0xF0) >> 4);
    data->humidity = humi_raw * 100.0f / 0x100000;

    // 温度: 20位数据
    temp_raw = ((uint32_t)(raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];
    data->temperature = temp_raw * 200.0f / 0x100000 - 50.0f;

    return DHT20_OK;
}


/* 获取缓存数据（非阻塞） */
DHT20_data_t DHT20_GetCachedData(DHT20_Handle handle)
{
    DHT20_data_t data = {0};
    if (handle && xSemaphoreTake(handle->data_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        data = handle->cached_data;
        xSemaphoreGive(handle->data_mutex);
    }
    return data;
}

/* 获取设备状态 */
DHT20_status_t DHT20_GetStatus(DHT20_Handle handle, uint8_t* status)
{
    uint8_t cmd = DHT20_CMD_STATUS;

    if (HAL_I2C_Master_Transmit(handle->config.i2c_handle,
                                handle->config.i2c_addr, &cmd, 1,
                                handle->config.timeout_ms) != HAL_OK)
    {
        return DHT20_I2C_ERROR;
    }

    if (HAL_I2C_Master_Receive(handle->config.i2c_handle,
                               handle->config.i2c_addr, status, 1,
                               handle->config.timeout_ms) != HAL_OK)
    {
        return DHT20_I2C_ERROR;
    }

    return DHT20_OK;
}

