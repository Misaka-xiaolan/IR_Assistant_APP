//
// Created by skinm on 2026/3/26.
//

#include "bsp_ir.h"
#include "tim.h"

#define TAG "bsp_ir"
#define MAX_SUBSCRIBERS 5
#define IR_DEFAULT_TIMEOUT_MS 500
#define IR_DEFAULT_QUEUE_DEPTH 3


/* 内部设备结构体 */
struct IR_Device_t
{
    ir_config_t config;
    volatile ir_mode_t current_mode;      // 当前工作模式
    volatile ir_status_t status;          // 设备状态

    // 发送相关状态
    uint32_t send_sequence[IR_DEFAULT_MAX_DATA_LEN];
    uint32_t send_len;
    volatile uint8_t send_in_progress;    // 发送进行标志

    // 接收相关状态
    volatile uint8_t recv_state;          // 0: IDLE, 1: RECEIVING
    uint32_t recv_sequence[IR_DEFAULT_MAX_DATA_LEN];
    uint32_t recv_cnt;
    uint32_t timeout_cnt;

    // 事件队列
    QueueHandle_t event_queue;
    QueueHandle_t subscribers[MAX_SUBSCRIBERS];
    uint8_t subscriber_count;

    // 状态统计
    uint8_t initialized;
    volatile uint8_t enabled;
};

// 内部辅助函数声明
static void ir_reset_send(IR_Handle handle);
static void ir_reset_recv(IR_Handle handle);
static void ir_notify_subscribers(IR_Handle handle, const ir_event_t* event);
static ir_status_t ir_validate_config(const ir_config_t* config);

// 全局设备句柄（用于DMA回调）
static IR_Handle g_ir_handle = NULL;

// 验证配置参数
static ir_status_t ir_validate_config(const ir_config_t* config)
{
    if (!config) return IR_INVALID_PARAM;

    // 检查发送配置
    if (!config->send_tim_handle || !config->send_gpio_port || !config->send_dma_handle)
    {
        return IR_INVALID_PARAM;
    }

    // 检查接收配置
    if (!config->recv_tim_handle || !config->recv_gpio_port)
    {
        return IR_INVALID_PARAM;
    }

    return IR_OK;
}

// 重置发送状态
static void ir_reset_send(IR_Handle handle)
{
    handle->send_len = 0;
    handle->send_in_progress = 0;
}

// 重置接收状态
static void ir_reset_recv(IR_Handle handle)
{
    handle->recv_state = 0;
    handle->recv_cnt = 0;
    handle->timeout_cnt = 0;

    // 关闭更新中断
    if (handle->config.recv_tim_handle)
    {
        __HAL_TIM_DISABLE_IT(handle->config.recv_tim_handle, TIM_IT_UPDATE);
    }
}

// 通知所有订阅者事件
static void ir_notify_subscribers(IR_Handle handle, const ir_event_t* event)
{
    for (int i = 0; i < handle->subscriber_count; i++)
    {
        xQueueSendFromISR(handle->subscribers[i], event, 0);
    }
}

// DMA发送完成中断回调
static void IR_Send_DMA_Complete_Callback(DMA_HandleTypeDef *hdma)
{
    if (!g_ir_handle || !g_ir_handle->initialized)
    {
        return;
    }

    IR_Handle handle = g_ir_handle;

    // 停止PWM和OC
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIM_OC_Stop(&htim2, TIM_CHANNEL_1);

    handle->send_in_progress = 0;

    // // 触发发送完成事件
    // ir_event_t event = {
    //     .data = NULL,
    //     .len = 0,
    //     .mode = IR_MODE_SEND,
    //     .error = 0
    // };
    // ir_notify_subscribers(handle, &event);

    // 如果当前模式是发送模式，则回到空闲模式
    if (handle->current_mode == IR_MODE_SEND)
    {
        handle->current_mode = IR_MODE_IDLE;
        handle->status = IR_OK;
    }
    elog_info(TAG, "IR_Send_DMA_Complete_Callback");
}


// 初始化红外设备
IR_Handle IR_Init(const ir_config_t* config)
{
    ir_status_t status = ir_validate_config(config);
    if (status != IR_OK)
    {
        return NULL;
    }

    // 分配设备内存
    IR_Handle handle = (IR_Handle)pvPortMalloc(sizeof(struct IR_Device_t));
    if (!handle)
    {
        elog_error(TAG, "Failed to alloc memory");
        return NULL;
    }
    memset(handle, 0, sizeof(struct IR_Device_t));

    // 复制配置
    memcpy(&handle->config, config, sizeof(ir_config_t));

    // 设置默认值
    if (handle->config.timeout_ms == 0)
    {
        handle->config.timeout_ms = IR_DEFAULT_TIMEOUT_MS;
    }
    if (handle->config.queue_depth == 0)
    {
        handle->config.queue_depth = IR_DEFAULT_QUEUE_DEPTH;
    }
    if (handle->config.max_data_len == 0)
    {
        handle->config.max_data_len = IR_DEFAULT_MAX_DATA_LEN;
    }

    // 创建事件队列
    handle->event_queue = xQueueCreate(handle->config.queue_depth, sizeof(ir_event_t));
    if (!handle->event_queue)
    {
        vPortFree(handle);
        elog_error(TAG, "Failed to create event queue");
        return NULL;
    }

    // 初始化状态
    handle->current_mode = IR_MODE_IDLE;
    handle->status = IR_OK;
    ir_reset_send(handle);
    ir_reset_recv(handle);

    // 初始化硬件（复用原发送初始化）
    MX_TIM1_Init();
    MX_TIM2_Init();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);

    handle->initialized = 1;
    handle->enabled = 1;

    // 设置全局句柄供DMA回调使用
    g_ir_handle = handle;
    elog_info(TAG, "IR Initialized");
    return handle;
}

// 反初始化红外设备
ir_status_t IR_Deinit(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return IR_INVALID_PARAM;
    }

    // 停止发送和接收
    IR_Receive_Stop(handle);

    // 清理队列
    if (handle->event_queue)
    {
        vQueueDelete(handle->event_queue);
    }

    // 清理订阅者
    for (int i = 0; i < handle->subscriber_count; i++)
    {
        handle->subscribers[i] = NULL;
    }

    // 释放内存
    vPortFree(handle);

    return IR_OK;
}

// 发送红外数据
ir_status_t IR_Send(IR_Handle handle, const uint32_t* data, uint32_t len)
{
    if (!handle || !handle->initialized || !data || len == 0)
    {
        return IR_INVALID_PARAM;
    }

    if (handle->current_mode != IR_MODE_IDLE)
    {
        return IR_BUSY;
    }

    if (len > IR_DEFAULT_MAX_DATA_LEN)
    {
        return IR_INVALID_PARAM;
    }


    // 更新模式
    handle->current_mode = IR_MODE_SEND;
    handle->status = IR_BUSY;

    // // 生成发送序列（复用原发送逻辑）
    // handle->send_sequence[0] = 9000;  // 引导码高电平
    // handle->send_sequence[1] = 4500;  // 引导码低电平
    //
    // // 生成数据码
    // for (int i = 0; i < len; i++)
    // {
    //     // 脉冲宽度固定 (560us)
    //     handle->send_sequence[2 + i * 2] = 560;
    //
    //     // 根据位值 (0 或 1) 确定间隔宽度
    //     if (data[i] == 1)
    //     {
    //         handle->send_sequence[3 + i * 2] = 1690; // Logic 1
    //     }
    //     else
    //     {
    //         handle->send_sequence[3 + i * 2] = 560;  // Logic 0
    //     }
    // }
    //
    // // 结束码
    // handle->send_sequence[2 + len * 2] = 560;
    // handle->send_sequence[2 + len * 2 + 1] = 560;
    // handle->send_len = 2 + len * 2 + 3;

    for (int i = 0; i < len; ++i)
    {
        handle->send_sequence[i] = data[i];
    }
    handle->send_len = len;

    // 启动DMA传输
    HAL_DMA_RegisterCallback(handle->config.send_dma_handle, HAL_DMA_XFER_CPLT_CB_ID,
                             IR_Send_DMA_Complete_Callback);
    HAL_DMA_Start_IT(handle->config.send_dma_handle,
                     (uint32_t)&handle->send_sequence[1],
                     (uint32_t)&TIM2->ARR,
                     handle->send_len + 1);

    __HAL_TIM_ENABLE_DMA(handle->config.send_tim_handle, TIM_DMA_UPDATE);
    TIM2->ARR = handle->send_sequence[0];

    // 启动PWM和OC
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_GenerateEvent(handle->config.send_tim_handle, TIM_EVENTSOURCE_UPDATE);
    HAL_TIM_OC_Start(handle->config.send_tim_handle, TIM_CHANNEL_1);

    handle->send_in_progress = 1;

    return IR_OK;
}

// 启动红外接收
ir_status_t IR_Receive_Start(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return IR_INVALID_PARAM;
    }

    if (handle->current_mode != IR_MODE_IDLE)
    {
        return IR_BUSY;
    }

    // 更新模式
    handle->current_mode = IR_MODE_RECEIVE;
    handle->status = IR_BUSY;

    // 重置接收状态
    ir_reset_recv(handle);

    // 启动输入捕获中断
    if (HAL_TIM_IC_Start_IT(handle->config.recv_tim_handle, handle->config.recv_tim_channel) != HAL_OK)
    {
        handle->status = IR_ERROR;
        elog_error(TAG, "IR capture start FAIL");
        return IR_ERROR;
    }
    elog_info(TAG, "IR capture start");
    return IR_OK;
}

// 停止红外接收
ir_status_t IR_Receive_Stop(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return IR_INVALID_PARAM;
    }

    // 停止输入捕获
    HAL_TIM_IC_Stop_IT(handle->config.recv_tim_handle, handle->config.recv_tim_channel);

    // 重置接收状态
    ir_reset_recv(handle);

    // 如果当前是接收模式，则回到空闲模式
    if (handle->current_mode == IR_MODE_RECEIVE)
    {
        handle->current_mode = IR_MODE_IDLE;
        handle->status = IR_OK;
    }
    elog_info(TAG, "IR capture stop");
    return IR_OK;
}

// 设置工作模式
ir_status_t IR_Set_Mode(IR_Handle handle, ir_mode_t mode)
{
    if (!handle || !handle->initialized)
    {
        return IR_INVALID_PARAM;
    }

    if (handle->current_mode == mode)
    {
        return IR_OK;
    }

    // 根据当前模式停止相应操作
    switch (handle->current_mode)
    {
        case IR_MODE_SEND:
            // 停止发送操作
            HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
            HAL_TIM_OC_Stop(&htim2, TIM_CHANNEL_1);
            handle->send_in_progress = 0;
            break;

        case IR_MODE_RECEIVE:
            // 停止接收操作
            IR_Receive_Stop(handle);
            break;

        default:
            break;
    }

    handle->current_mode = mode;
    handle->status = IR_OK;

    return IR_OK;
}

// 获取当前工作模式
ir_mode_t IR_Get_Mode(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return IR_MODE_IDLE;
    }

    return handle->current_mode;
}

// 获取设备状态
ir_status_t IR_Get_Status(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return IR_INVALID_PARAM;
    }

    return handle->status;
}

TIM_HandleTypeDef* IR_Get_Receive_TIM_Handle(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return NULL;
    }
    return handle->config.recv_tim_handle;
}

TIM_HandleTypeDef* IR_Get_Send_TIM_Handle(IR_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return NULL;
    }
    return handle->config.send_tim_handle;
}

// 订阅事件
QueueHandle_t IR_Event_Subscribe(IR_Handle handle)
{
    if (!handle || !handle->initialized || handle->subscriber_count >= MAX_SUBSCRIBERS)
    {
        elog_error(TAG, "\"%s\" ir event subscribe FAIL", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
        return NULL;
    }

    QueueHandle_t queue = xQueueCreate(handle->config.queue_depth, sizeof(ir_event_t));
    if (!queue)
    {
        elog_error(TAG, "\"%s\" ir event subscribe FAIL", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
        return NULL;
    }
    elog_info(TAG, "\"%s\" subscribed ir event", pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
    handle->subscribers[handle->subscriber_count++] = queue;
    return queue;
}

// 取消订阅
void IR_Event_Unsubscribe(IR_Handle handle, QueueHandle_t queue)
{
    if (!handle || !handle->initialized || !queue)
    {
        return;
    }

    for (int i = 0; i < handle->subscriber_count; i++)
    {
        if (handle->subscribers[i] == queue)
        {
            vQueueDelete(queue);
            // 移除该订阅者
            for (int j = i; j < handle->subscriber_count - 1; j++)
            {
                handle->subscribers[j] = handle->subscribers[j + 1];
            }
            handle->subscriber_count--;
            break;
        }
    }
}

// 捕获中断回调（接收部分）
void IR_Capture_IC_ISR_Callback(IR_Handle handle)
{
    if (!handle || !handle->initialized || !handle->enabled ||
        handle->current_mode != IR_MODE_RECEIVE)
    {
        return;
    }

    TIM_HandleTypeDef* htim = handle->config.recv_tim_handle;

    if (handle->recv_state == 0)
    {
        handle->recv_state = 1;
        __HAL_TIM_SET_COUNTER(htim, 0);
        __HAL_TIM_ENABLE_IT(htim, TIM_IT_UPDATE);
        return;
    }

    uint16_t dval = 0;
    dval = HAL_TIM_ReadCapturedValue(htim, handle->config.recv_tim_channel);
    handle->recv_sequence[handle->recv_cnt] = dval;
    handle->recv_cnt = handle->recv_cnt + 1;
    __HAL_TIM_SET_COUNTER(htim, 0);

    // if (pin_state == GPIO_PIN_SET)
    // {
    //     // 上升沿：准备捕获下降沿
    //     __HAL_TIM_SET_CAPTUREPOLARITY(htim, handle->config.recv_tim_channel,
    //                                   TIM_INPUTCHANNELPOLARITY_FALLING);
    //     __HAL_TIM_SET_COUNTER(htim, 0);
    //     handle->captured_edge = 1;
    // }
    // else
    // {
    //     // 下降沿：处理脉宽
    //     dval = HAL_TIM_ReadCapturedValue(htim, handle->config.recv_tim_channel);
    //     __HAL_TIM_SET_CAPTUREPOLARITY(htim, handle->config.recv_tim_channel,
    //                                   TIM_INPUTCHANNELPOLARITY_RISING);
    //
    //     if (handle->captured_edge == 1)
    //     {
    //         if (handle->recv_state == 1)
    //         {
    //             // 接收中
    //             if (dval > 300 && dval < 800)
    //             {
    //                 if (handle->recv_cnt < IR_DEFAULT_MAX_DATA_LEN)
    //                     handle->recv_sequence[handle->recv_cnt++] = 0;
    //             }
    //             else if (dval > 1400 && dval < 1800)
    //             {
    //                 if (handle->recv_cnt < IR_DEFAULT_MAX_DATA_LEN)
    //                     handle->recv_sequence[handle->recv_cnt++] = 1;
    //             }
    //             handle->timeout_cnt = 0;
    //
    //             if (handle->recv_cnt >= IR_DEFAULT_MAX_DATA_LEN)
    //             {
    //                 // 接收缓冲区满，触发事件
    //                 ir_event_t event = {
    //                     .data = handle->recv_sequence,
    //                     .len = handle->recv_cnt,
    //                     .mode = IR_MODE_RECEIVE,
    //                     .error = 0
    //                 };
    //                 ir_notify_subscribers(handle, &event);
    //
    //                 ir_reset_recv(handle);
    //                 return;
    //             }
    //         }
    //         else if (handle->recv_state == 0)
    //         {
    //             // 空闲状态 - 检测引导码
    //             if (dval > 4200 && dval < 4700)
    //             {
    //                 handle->recv_state = 1; // 开始接收
    //                 handle->recv_cnt = 0;
    //                 handle->timeout_cnt = 0;
    //                 __HAL_TIM_ENABLE_IT(htim, TIM_IT_UPDATE);
    //             }
    //         }
    //     }
    //     handle->captured_edge = 0;
    // }
}

// 更新中断回调（接收超时处理）
void IR_Capture_Update_ISR_Callback(IR_Handle handle)
{
    if (!handle || !handle->initialized || !handle->enabled ||
        handle->current_mode != IR_MODE_RECEIVE || handle->recv_state != 1)
    {
        return;
    }

    handle->timeout_cnt++;
    uint32_t timeout_ticks = pdMS_TO_TICKS(handle->config.timeout_ms) / 65;

    if (handle->timeout_cnt > timeout_ticks)
    {
        __HAL_TIM_DISABLE_IT(handle->config.recv_tim_handle, TIM_IT_UPDATE);

        if (handle->recv_cnt > 0)
        {
            // 成功接收到数据
            ir_event_t event = {
                .data = handle->recv_sequence,
                .len = handle->recv_cnt,
                .mode = IR_MODE_RECEIVE,
                .error = 0
            };
            elog_info(TAG, "IR signal capture success, len = %d", handle->recv_cnt);
            IR_Receive_Stop(handle);
            ir_notify_subscribers(handle, &event);
        }
        else
        {
            // 接收超时
            ir_event_t event = {
                .data = NULL,
                .len = 0,
                .mode = IR_MODE_RECEIVE,
                .error = IR_TIMEOUT
            };
            elog_error(TAG, "IR signal capture timeout");
            ir_notify_subscribers(handle, &event);
        }

        ir_reset_recv(handle);
    }
}





