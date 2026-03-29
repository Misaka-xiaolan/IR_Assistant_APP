//
// Created by skinm on 2026/3/18.
//

#ifndef IR_ASSISTANT_TEST_BSP_DHT20_H
#define IR_ASSISTANT_TEST_BSP_DHT20_H

#include "main.h"

/* DHT20 设备结构体（不透明指针） */
typedef struct DHT20_device_t* DHT20_Handle;

// 订阅者信息
typedef struct
{
    TaskHandle_t task_handle;
    QueueHandle_t queue;  // 每个订阅者独立的队列
    uint8_t active;
} DHT20_subscriber_t;

/* 温湿度数据 */
typedef struct
{
    float temperature;
    float humidity;
    uint32_t timestamp; // 测量时间戳
} DHT20_data_t;

/* 设备状态 */
typedef enum
{
    DHT20_OK = 0,
    DHT20_BUSY,
    DHT20_CRC_ERROR,
    DHT20_TIMEOUT,
    DHT20_I2C_ERROR,
    DHT20_NOT_CALIBRATED,
    DHT20_INVALID_PARAM
} DHT20_status_t;

/* 测量模式 */
typedef enum
{
    DHT20_MODE_CONTINUOUS, // 连续测量
    DHT20_MODE_SLEEP // 睡眠模式
} DHT20_mode_t;

/* 配置参数 */
typedef struct
{
    I2C_HandleTypeDef* i2c_handle; // I2C句柄
    uint8_t i2c_addr; // 设备地址 (默认0x38)
    uint32_t timeout_ms; // 超时时间
    uint32_t measure_interval_ms; // 测量间隔（连续模式）
    void (*delay_ms)(uint32_t ms); // 延时函数指针
} DHT20_config_t;

/* 初始化 */
DHT20_Handle DHT20_Init(DHT20_config_t* config);

/* 高级功能接口 */
DHT20_status_t DHT20_SetMode(DHT20_Handle handle, DHT20_mode_t mode);
DHT20_status_t DHT20_GetStatus(DHT20_Handle handle, uint8_t* status);
bool DHT20_IsCalibrated(DHT20_Handle handle);

/* 多任务支持接口 */
DHT20_data_t DHT20_GetCachedData(DHT20_Handle handle);
QueueHandle_t DHT20_Event_Subscribe(DHT20_Handle handle);
void DHT20_Event_Unsubscribe(DHT20_Handle handle, QueueHandle_t queue);

/* 诊断接口 */
void DHT20_GetErrorCount(DHT20_Handle handle, uint32_t* i2c_err, uint32_t* crc_err);
void DHT20_ResetErrorCount(DHT20_Handle handle);


#endif //IR_ASSISTANT_TEST_BSP_DHT20_H
