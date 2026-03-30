//
// Created by skinm on 2026/3/29.
//

#ifndef IR_ASSISTANT_APP_BSP_UART_H
#define IR_ASSISTANT_APP_BSP_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h> // 确保 uint16_t 等类型被定义

// 不透明指针声明 (Opaque pointer)，隐藏内部实现细节
typedef struct uart_device_t* UART_Handle;

// 初始化配置结构体
typedef struct
{
    UART_HandleTypeDef* uart_handle;
} UART_config_t;

// 回调函数类型定义 (方便用户定义回调函数)
typedef void (*UART_RxCallback_t)(uint8_t *data, uint16_t len);

// --- 公开 API ---

/**
 * @brief 初始化 UART DMA 接收/发送驱动
 * @param config 包含 HAL 句柄的配置结构体
 * @return 成功返回设备句柄，失败返回 NULL
 */
UART_Handle UART_DMA_Init(UART_config_t* config);

/**
 * @brief 注册用户接收回调函数
 * @param dev UART 设备句柄
 * @param callback 用户回调函数指针 (原型: void func(uint8_t *data, uint16_t len))
 */
void UART_Register_RxCallback(UART_Handle dev, UART_RxCallback_t callback);

/**
 * @brief 向发送缓冲区写入数据（线程安全）
 * @param dev UART 设备句柄
 * @param data 待发送数据指针
 * @param len 待发送数据长度
 * @return 实际写入缓冲区的字节数（0表示失败/缓冲区满）
 */
uint16_t UART_Send_Data(UART_Handle dev, const uint8_t* data, uint16_t len);

/**
 * @brief 获取发送缓冲区剩余空间（线程安全）
 * @param dev UART 设备句柄
 * @return 剩余可写入字节数
 */
uint16_t UART_Get_TxBuffer_FreeSize(UART_Handle dev);

/**
 * @brief 启动两个串口的双向透传（桥接）
 * @note 此操作会覆盖两个串口原有的接收回调
 * @param uart_a 串口A句柄
 * @param uart_b 串口B句柄
 * @return 0:成功 -1:失败
 */
int UART_Start_Bridge(UART_Handle uart_a, UART_Handle uart_b);

/**
 * @brief 停止串口桥接
 * @param uart_a 串口A句柄
 * @param uart_b 串口B句柄
 * @return 0:成功 -1:失败
 */
int UART_Stop_Bridge(UART_Handle uart_a, UART_Handle uart_b);

#ifdef __cplusplus
}
#endif

#endif //IR_ASSISTANT_APP_BSP_UART_H
