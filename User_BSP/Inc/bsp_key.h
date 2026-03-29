//
// Created by skinm on 2026/3/17.
//

#ifndef IR_ASSISTANT_TEST_BSP_KEY_H
#define IR_ASSISTANT_TEST_BSP_KEY_H

#include "main.h"

#define KEY_ISR_GPIO_PORT   GPIOB
#define KEY_ISR_GPIO_PIN    GPIO_PIN_1

#define KEY_I2C_HANDLER &hi2c1
#define KEY_I2C_ADDR    0x40

/* Key 设备结构体（不透明指针） */
typedef struct Key_device_t* Key_Handle;

// 按键事件枚举
typedef enum
{
    KEY_UP = 0,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_ENTER,
    KEY_UNKOWN,
}key_type_t;

typedef enum
{
    KEY_PRESS = 0,
    KEY_RELEASE,
}key_action_t;

typedef struct
{
    key_type_t key_type;
    key_action_t key_action;
    uint32_t key_pressed_time;
}key_event_t;

// 订阅者信息
typedef struct
{
    TaskHandle_t task_handle;
    QueueHandle_t queue;  // 每个订阅者独立的队列
    uint8_t active;
} key_subscriber_t;

/* 配置参数 */
typedef struct
{
    I2C_HandleTypeDef* i2c_handle; // I2C句柄
    uint8_t i2c_addr;              // 设备地址
    uint32_t timeout_ms;           // 超时时间
    uint32_t long_press_time_ms;   // 长按判定时间
} key_config_t;

Key_Handle Key_Init(key_config_t* config);
void Key_I2C_ISR_Callback(Key_Handle handle);
QueueHandle_t Key_Event_Subscribe(Key_Handle handle);
void Key_Event_Unsubscribe(Key_Handle handle, QueueHandle_t queue);


#endif //IR_ASSISTANT_TEST_BSP_KEY_H