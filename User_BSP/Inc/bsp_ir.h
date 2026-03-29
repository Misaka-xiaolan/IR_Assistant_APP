//
// Created by skinm on 2026/3/26.
//

#ifndef IR_ASSISTANT_TEST_BSP_IR_SEND_H
#define IR_ASSISTANT_TEST_BSP_IR_SEND_H

#include "main.h"

#define IR_DEFAULT_MAX_DATA_LEN 1024

// 红外设备工作模式
typedef enum
{
    IR_MODE_IDLE = 0,
    IR_MODE_SEND,
    IR_MODE_RECEIVE
} ir_mode_t;

// 红外设备错误码
typedef enum
{
    IR_OK = 0,
    IR_ERROR,
    IR_BUSY,
    IR_TIMEOUT,
    IR_INVALID_PARAM
} ir_status_t;

// 红外配置结构体
typedef struct
{
    // 发送相关配置
    TIM_HandleTypeDef* send_tim_handle;   // 发送定时器句柄
    uint32_t send_tim_channel;           // 发送定时器通道
    GPIO_TypeDef* send_gpio_port;        // 发送GPIO端口
    uint16_t send_gpio_pin;              // 发送GPIO引脚
    DMA_HandleTypeDef* send_dma_handle;  // 发送DMA句柄

    // 接收相关配置
    TIM_HandleTypeDef* recv_tim_handle;   // 接收定时器句柄
    uint32_t recv_tim_channel;           // 接收定时器通道
    GPIO_TypeDef* recv_gpio_port;        // 接收GPIO端口
    uint16_t recv_gpio_pin;              // 接收GPIO引脚
    uint32_t timeout_ms;                 // 帧间超时时间(ms)

    // 通用配置
    uint8_t queue_depth;                 // 事件队列深度
    uint32_t max_data_len;               // 最大数据长度
} ir_config_t;

// 红外事件结构体
typedef struct
{
    uint32_t* data;                       // 数据缓冲区
    uint32_t len;                        // 数据长度
    ir_mode_t mode;                      // 事件所属模式
    int32_t error;                       // 错误码: 0=OK, <0=错误
} ir_event_t;

// 不透明设备句柄
typedef struct IR_Device_t* IR_Handle;

// API函数声明
IR_Handle IR_Init(const ir_config_t* config);
ir_status_t IR_Deinit(IR_Handle handle);

ir_status_t IR_Send(IR_Handle handle, const uint32_t* data, uint32_t len);
ir_status_t IR_Receive_Start(IR_Handle handle);
ir_status_t IR_Receive_Stop(IR_Handle handle);

ir_status_t IR_Set_Mode(IR_Handle handle, ir_mode_t mode);
ir_mode_t IR_Get_Mode(IR_Handle handle);
ir_status_t IR_Get_Status(IR_Handle handle);

QueueHandle_t IR_Event_Subscribe(IR_Handle handle);
void IR_Event_Unsubscribe(IR_Handle handle, QueueHandle_t queue);

TIM_HandleTypeDef* IR_Get_Receive_TIM_Handle(IR_Handle handle);
TIM_HandleTypeDef* IR_Get_Send_TIM_Handle(IR_Handle handle);

// 中断回调函数（由外部调用）
void IR_Capture_IC_ISR_Callback(IR_Handle handle);
void IR_Capture_Update_ISR_Callback(IR_Handle handle);


#endif //IR_ASSISTANT_TEST_BSP_IR_SEND_H