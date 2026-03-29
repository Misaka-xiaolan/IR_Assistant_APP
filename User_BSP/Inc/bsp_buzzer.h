//
// Created by skinm on 2026/3/19.
//

#ifndef IR_ASSISTANT_TEST_BSP_BUZZER_H
#define IR_ASSISTANT_TEST_BSP_BUZZER_H

#include "main.h"

typedef enum {
    BUZZER_TYPE_ACTIVE = 0,   ///< 有源蜂鸣器（只需开关）
    BUZZER_TYPE_PASSIVE       ///< 无源蜂鸣器（需 PWM 驱动）
} buzzer_type_t;

typedef struct
{
    buzzer_type_t type;               ///< 蜂鸣器类型

    // --- 有源蜂鸣器配置 ---
    GPIO_TypeDef* gpio_port;          ///< GPIO 端口（如 GPIOA）
    uint16_t gpio_pin;                ///< GPIO 引脚（如 GPIO_PIN_5）

    // --- 无源蜂鸣器配置 ---
    TIM_HandleTypeDef* pwm_tim;       ///< 定时器句柄（如 &htim3）
    uint32_t pwm_channel;             ///< PWM 通道（如 TIM_CHANNEL_1）

    // --- 通用行为配置 ---
    uint32_t default_beep_ms;         ///< 默认蜂鸣时长（ms），0 表示使用内部默认值（200ms）
    uint32_t default_beep_freq;       //默认蜂鸣频率（hz）
    uint32_t max_queue_size;          ///< 命令队列大小（0=禁用队列，直接阻塞播放；建议 ≥1）
} buzzer_config_t;

typedef struct Buzzer_device_t* Buzzer_Handle;

Buzzer_Handle Buzzer_Init(buzzer_config_t* config);
void Buzzer_Beep(Buzzer_Handle handle, uint32_t duration_ms, uint32_t freq_hz);
void Buzzer_Stop(Buzzer_Handle handle);

#endif //IR_ASSISTANT_TEST_BSP_BUZZER_H