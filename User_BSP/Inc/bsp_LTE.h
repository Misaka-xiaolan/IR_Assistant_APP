//
// Created by skinm on 2026/3/29.
//

#ifndef IR_ASSISTANT_APP_BSP_LTE_H
#define IR_ASSISTANT_APP_BSP_LTE_H

#include "main.h"

typedef struct LTE_device_t *LTE_Handle; // LTE设备句柄

typedef struct
{
    // 硬件接口
    USART_TypeDef* lte_uart_handle;
    USART_TypeDef* debug_uart_handle;

    // 配置参数
    uint32_t timeout_ms;

}LTE_config_t;

// 通信模式
typedef enum
{
    COMM_BRIDGE,    // 串口桥接模式（UART2 ↔ UART1）
    COMM_AT         // AT命令模式（UART2直接输入AT命令）
} LTE_comm_mode_t;


LTE_Handle LTE_Init(LTE_config_t* lte_config);


#endif //IR_ASSISTANT_APP_BSP_LTE_H