//
// Created by skinm on 2026/3/29.
//

#include "bsp_LTE.h"
#include "usart.h"

#define TAG "LTE"

QueueHandle_t LTEATCmdQueue = NULL;   // AT命令队列
QueueHandle_t LTEATRespQueue = NULL;  // AT响应队列

// 串口桥接任务
static void LTE_Task_SerialBridge(void *pvParameters);

// AT命令处理任务
static void LTETask_AT_Command_Handler(void *pvParameters);

/* 内部数据结构 */
struct LTE_device_t
{
    // 硬件接口
    USART_TypeDef* lte_uart_handle;
    USART_TypeDef* debug_uart_handle;

    // 配置参数
    uint32_t timeout_ms;
    LTE_comm_mode_t comm_mode;
    uint8_t initialised;
};

LTE_Handle LTE_Init(LTE_config_t* lte_config)
{
    if (lte_config == NULL)
    {
        return NULL;
    }
    if (!lte_config->debug_uart_handle || !lte_config->lte_uart_handle)
    {
        return NULL;
    }
    LTE_Handle dev = (LTE_Handle)pvPortMalloc(sizeof(struct LTE_device_t));

    memset(dev, 0, sizeof(struct LTE_device_t));
    // 复制配置
    dev->debug_uart_handle = lte_config->debug_uart_handle;
    dev->lte_uart_handle = lte_config->lte_uart_handle;
    // 设置默认值
    if (lte_config->timeout_ms == 0) lte_config->timeout_ms = 3000;
    dev->initialised = 1;
    dev->comm_mode = COMM_BRIDGE;
    return dev;
}





