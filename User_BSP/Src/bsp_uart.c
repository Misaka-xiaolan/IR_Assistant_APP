//
// Created by skinm on 2026/3/29.
//

#include "bsp_uart.h"
#include "semphr.h"
#include <string.h>

#define UART_RX_DMA_BUF_SIZE    512
#define UART_TX_DMA_BUF_SIZE    512  // 发送DMA单次最大长度
#define RX_RING_BUFFER_SIZE     1024
#define TX_RING_BUFFER_SIZE     1024 // 发送环形缓冲区大小
#define MAX_UART_DEVICE         10

#define TAG "uart"

// 环形缓冲区结构体
typedef struct
{
    uint8_t buffer[RX_RING_BUFFER_SIZE];
    volatile uint16_t head; // 写入指针 (ISR 只写 head，读 tail)
    volatile uint16_t tail; // 读取指针 (Task 只写 tail，读 head)
} RingBuffer_t;

// 发送环形缓冲区结构体（独立定义，便于扩展）
typedef struct
{
    uint8_t buffer[TX_RING_BUFFER_SIZE];
    volatile uint16_t head; // 写入指针 (Task写，ISR读)
    volatile uint16_t tail; // 读取指针 (ISR写，Task读)
    SemaphoreHandle_t lock; // 写锁（保证多线程写入安全）
} TxRingBuffer_t;

// 串口设备结构体
struct uart_device_t
{
    UART_HandleTypeDef* uart_device;
    RingBuffer_t uart_ringbuf;
    uint8_t uart_dma_rx_buf[UART_RX_DMA_BUF_SIZE];

    // 优化2：将临时缓冲区从栈移到这里，避免栈溢出
    uint8_t process_buf[UART_RX_DMA_BUF_SIZE];

    TaskHandle_t task;
    SemaphoreHandle_t rx_sem;

    // 回调函数指针
    void (*user_rx_callback)(uint8_t* data, uint16_t len);

    uint8_t initialised;

    // 新增发送相关成员
    TxRingBuffer_t tx_ringbuf;
    uint8_t uart_dma_tx_buf[UART_TX_DMA_BUF_SIZE]; // 发送DMA临时缓冲区
    volatile uint8_t tx_busy; // 发送忙标志（0:空闲 1:发送中）
};

static UART_Handle uart_device_list[MAX_UART_DEVICE] = {0};

static void UART_PerInstance_Task(void* params);
static void UART_Start_DMATx(UART_Handle dev); // 启动DMA发送

// --- 优化3：环形缓冲区操作 (高性能 memcpy 版本) ---

// 获取环形缓冲区中当前可读的数据长度
static inline uint16_t RingBuffer_GetLen(RingBuffer_t* rb)
{
    // 这里不需要关中断，因为 head 和 tail 是 uint16_t，在 32位机上读写是原子的
    return (rb->head - rb->tail + RX_RING_BUFFER_SIZE) % RX_RING_BUFFER_SIZE;
}

// 向环形缓冲区写入数据 (ISR专用，无锁，使用 memcpy)
static bool RingBuffer_Write_ISR(RingBuffer_t* rb, const uint8_t* data, uint16_t len)
{
    // 1. 检查空间是否足够
    uint16_t current_len = (rb->head - rb->tail + RX_RING_BUFFER_SIZE) % RX_RING_BUFFER_SIZE;
    if (current_len + len > RX_RING_BUFFER_SIZE - 1)
    {
        elog_warn(TAG, "RX ringbuffer full, dropped %d bytes", len);
        return false; // 空间不足
    }

    // 2. 计算写入位置
    uint16_t free_len_after_head = RX_RING_BUFFER_SIZE - rb->head;

    // 3. 分两段拷贝 (如果不需要回绕，则第二段长度为0，memcpy不执行)
    if (len <= free_len_after_head)
    {
        // 数据全部在 head 后面
        memcpy(&rb->buffer[rb->head], data, len);
    }
    else
    {
        // 数据分为两段：先填满 head 到末尾，再从开头写
        memcpy(&rb->buffer[rb->head], data, free_len_after_head);
        memcpy(rb->buffer, data + free_len_after_head, len - free_len_after_head);
    }

    // 4. 最后更新 head 指针 (内存屏障确保数据先写完，指针再更新)
    // 对于 STM32 (Cortex-M)，通常不需要显式 dmb，因为 M3/M4/M7 对普通内存的读写是按序的，
    // 但如果是多核或特定优化场景，这里需要加上 __DSB()。
    rb->head = (rb->head + len) % RX_RING_BUFFER_SIZE;

    elog_verbose(TAG, "Wrote %d bytes to RX ringbuffer", len);
    return true;
}

// 从环形缓冲区读取数据 (Task专用，无锁，使用 memcpy)
static uint16_t RingBuffer_Read(RingBuffer_t* rb, uint8_t* data, uint16_t max_len)
{
    uint16_t current_len = (rb->head - rb->tail + RX_RING_BUFFER_SIZE) % RX_RING_BUFFER_SIZE;
    if (current_len == 0) return 0;

    uint16_t read_len = (current_len < max_len) ? current_len : max_len;
    uint16_t free_len_after_tail = RX_RING_BUFFER_SIZE - rb->tail;

    // 分两段读取
    if (read_len <= free_len_after_tail)
    {
        memcpy(data, &rb->buffer[rb->tail], read_len);
    }
    else
    {
        memcpy(data, &rb->buffer[rb->tail], free_len_after_tail);
        memcpy(data + free_len_after_tail, rb->buffer, read_len - free_len_after_tail);
    }

    // 更新 tail 指针
    rb->tail = (rb->tail + read_len) % RX_RING_BUFFER_SIZE;

    elog_verbose(TAG, "Read %d bytes from RX ringbuffer", read_len);
    return read_len;
}

// --- 新增：发送环形缓冲区操作 ---

// 获取发送缓冲区剩余空间（线程安全）
static uint16_t TxRingBuffer_GetFreeSize(TxRingBuffer_t* rb)
{
    uint16_t free_size = 0;
    xSemaphoreTake(rb->lock, portMAX_DELAY); // 加锁保证原子性
    free_size = (TX_RING_BUFFER_SIZE - 1) - ((rb->head - rb->tail + TX_RING_BUFFER_SIZE) % TX_RING_BUFFER_SIZE);
    xSemaphoreGive(rb->lock);
    return free_size;
}

// 向发送缓冲区写入数据（线程安全）
static uint16_t TxRingBuffer_Write(TxRingBuffer_t* rb, const uint8_t* data, uint16_t len)
{
    if (len == 0 || !data) return 0;

    xSemaphoreTake(rb->lock, portMAX_DELAY); // 加锁保护多线程写入

    // 1. 计算剩余空间
    uint16_t current_len = (rb->head - rb->tail + TX_RING_BUFFER_SIZE) % TX_RING_BUFFER_SIZE;
    uint16_t free_size = (TX_RING_BUFFER_SIZE - 1) - current_len;
    uint16_t write_len = (len < free_size) ? len : free_size;

    if (write_len == 0)
    {
        elog_warn(TAG, "TX ringbuffer full, cannot write %d bytes", len);
        xSemaphoreGive(rb->lock);
        return 0;
    }

    if (write_len < len)
    {
        elog_warn(TAG, "TX ringbuffer low space, only wrote %d/%d bytes", write_len, len);
    }

    // 2. 分两段写入
    uint16_t free_after_head = TX_RING_BUFFER_SIZE - rb->head;
    if (write_len <= free_after_head)
    {
        memcpy(&rb->buffer[rb->head], data, write_len);
    }
    else
    {
        memcpy(&rb->buffer[rb->head], data, free_after_head);
        memcpy(rb->buffer, data + free_after_head, write_len - free_after_head);
    }

    // 3. 更新head指针
    rb->head = (rb->head + write_len) % TX_RING_BUFFER_SIZE;

    xSemaphoreGive(rb->lock);
    elog_verbose(TAG, "Wrote %d bytes to TX ringbuffer", write_len);
    return write_len;
}

// 从发送缓冲区读取数据（ISR/Task通用，无锁，单次最多读UART_TX_DMA_BUF_SIZE）
static uint16_t TxRingBuffer_Read(TxRingBuffer_t* rb, uint8_t* data, uint16_t max_len)
{
    uint16_t current_len = (rb->head - rb->tail + TX_RING_BUFFER_SIZE) % TX_RING_BUFFER_SIZE;
    if (current_len == 0) return 0;

    uint16_t read_len = (current_len < max_len) ? current_len : max_len;
    uint16_t free_after_tail = TX_RING_BUFFER_SIZE - rb->tail;

    // 分两段读取
    if (read_len <= free_after_tail)
    {
        memcpy(data, &rb->buffer[rb->tail], read_len);
    }
    else
    {
        memcpy(data, &rb->buffer[rb->tail], free_after_tail);
        memcpy(data + free_after_tail, rb->buffer, read_len - free_after_tail);
    }

    // 更新tail指针
    rb->tail = (rb->tail + read_len) % TX_RING_BUFFER_SIZE;

    elog_verbose(TAG, "Read %d bytes from TX ringbuffer", read_len);
    return read_len;
}

// --- API 实现 ---

UART_Handle UART_DMA_Init(UART_config_t* config)
{
    if (!config || !config->uart_handle)
    {
        elog_error(TAG, "Invalid config or uart_handle in UART_DMA_Init");
        return NULL;
    }

    UART_Handle dev = (UART_Handle)pvPortMalloc(sizeof(struct uart_device_t));
    if (!dev)
    {
        elog_error(TAG, "Failed to allocate memory for UART device");
        return NULL;
    }

    memset(dev, 0, sizeof(struct uart_device_t));
    dev->uart_device = config->uart_handle;
    dev->uart_ringbuf.head = 0;
    dev->uart_ringbuf.tail = 0;
    dev->user_rx_callback = NULL;
    dev->tx_busy = 0; // 初始化为空闲

    // 初始化发送缓冲区
    dev->tx_ringbuf.head = 0;
    dev->tx_ringbuf.tail = 0;
    dev->tx_ringbuf.lock = xSemaphoreCreateMutex(); // 创建发送缓冲区互斥锁
    if (dev->tx_ringbuf.lock == NULL)
    {
        elog_error(TAG, "Failed to create TX ringbuffer mutex");
        vPortFree(dev);
        return NULL;
    }

    // 创建该实例私有的信号量
    dev->rx_sem = xSemaphoreCreateBinary();
    if (dev->rx_sem == NULL)
    {
        elog_error(TAG, "Failed to create RX binary semaphore");
        vSemaphoreDelete(dev->tx_ringbuf.lock);
        vPortFree(dev);
        return NULL;
    }

    // 创建任务
    if (xTaskCreate(UART_PerInstance_Task, "UARTx_Task", 512, dev, 28, &dev->task) != pdPASS)
    {
        elog_error(TAG, "Failed to create UART task");
        vSemaphoreDelete(dev->rx_sem);
        vSemaphoreDelete(dev->tx_ringbuf.lock);
        vPortFree(dev);
        return NULL;
    }

    // 启动 DMA 接收
    if (HAL_UARTEx_ReceiveToIdle_DMA(dev->uart_device, dev->uart_dma_rx_buf, UART_RX_DMA_BUF_SIZE) != HAL_OK)
    {
        elog_error(TAG, "Failed to start UART RX DMA");
        // 初始化失败清理
        vTaskDelete(dev->task);
        vSemaphoreDelete(dev->rx_sem);
        vSemaphoreDelete(dev->tx_ringbuf.lock);
        vPortFree(dev);
        return NULL;
    }
    __HAL_DMA_DISABLE_IT(dev->uart_device->hdmarx, DMA_IT_HT);
    dev->initialised = 1;

    // 加入设备列表
    int list_index = -1;
    for (int i = 0; i < MAX_UART_DEVICE; ++i)
    {
        if (uart_device_list[i] == NULL)
        {
            uart_device_list[i] = dev;
            list_index = i;
            break;
        }
    }

    if (list_index == -1)
    {
        elog_error(TAG, "UART device list full, cannot add new device");
        // 这里需要补充清理逻辑，防止内存泄漏
        vTaskDelete(dev->task);
        vSemaphoreDelete(dev->rx_sem);
        vSemaphoreDelete(dev->tx_ringbuf.lock);
        vPortFree(dev);
        return NULL;
    }

    elog_info(TAG, "UART device initialized successfully, added to list at index %d", list_index);
    return dev;
}

void UART_Register_RxCallback(UART_Handle dev, void (*callback)(uint8_t* data, uint16_t len))
{
    if (dev)
    {
        dev->user_rx_callback = callback;
        elog_debug(TAG, "RX callback registered for UART device");
    }
    else
    {
        elog_warn(TAG, "Attempted to register RX callback on NULL device");
    }
}

// 新增：向发送缓冲区写入数据（对外API）
uint16_t UART_Send_Data(UART_Handle dev, const uint8_t* data, uint16_t len)
{
    if (!dev || !data || len == 0 || !dev->initialised)
    {
        elog_warn(TAG, "Invalid parameters in UART_Send_Data");
        return 0;
    }

    elog_debug(TAG, "Request to send %d bytes via UART", len);
    uint16_t write_len = TxRingBuffer_Write(&dev->tx_ringbuf, data, len);

    // 如果当前未在发送，则立即启动DMA发送
    if (dev->tx_busy == 0)
    {
        elog_debug(TAG, "UART is idle, starting DMA transmission immediately");
        UART_Start_DMATx(dev);
    }
    else
    {
        elog_verbose(TAG, "UART is busy, data queued in TX ringbuffer");
    }

    return write_len;
}

// 新增：获取发送缓冲区剩余空间（对外API）
uint16_t UART_Get_TxBuffer_FreeSize(UART_Handle dev)
{
    if (!dev || !dev->initialised)
    {
        elog_warn(TAG, "Invalid device in UART_Get_TxBuffer_FreeSize");
        return 0;
    }
    return TxRingBuffer_GetFreeSize(&dev->tx_ringbuf);
}

// --- 任务与中断 ---

static void UART_PerInstance_Task(void* params)
{
    UART_Handle dev = (UART_Handle)params;
    uint16_t read_len;

    elog_info(TAG, "UART task started");
    while (1)
    {
        // 阻塞等待接收信号量
        if (xSemaphoreTake(dev->rx_sem, portMAX_DELAY) == pdTRUE)
        {
            elog_verbose(TAG, "RX semaphore taken, processing received data");
            // 循环处理直到接收缓冲区为空
            do
            {
                // 使用结构体里的 process_buf，不再占用栈空间
                read_len = RingBuffer_Read(&dev->uart_ringbuf, dev->process_buf, sizeof(dev->process_buf));

                if (read_len > 0)
                {
                    // 执行接收回调
                    if (dev->user_rx_callback)
                    {
                        elog_verbose(TAG, "Calling user RX callback with %d bytes", read_len);
                        dev->user_rx_callback(dev->process_buf, read_len);
                    }
                    else
                    {
                        elog_verbose(TAG, "No RX callback registered, dropping %d bytes", read_len);
                    }
                }
            }
            while (read_len > 0);
        }
    }
}

// 新增：启动DMA发送（核心发送逻辑）
static void UART_Start_DMATx(UART_Handle dev)
{
    if (!dev || dev->tx_busy)
    {
        elog_verbose(TAG, "UART_Start_DMATx skipped: dev=%p, tx_busy=%d", (void*)dev, dev->tx_busy);
        return;
    }

    // 从发送缓冲区读取数据到DMA发送缓冲区
    uint16_t send_len = TxRingBuffer_Read(&dev->tx_ringbuf, dev->uart_dma_tx_buf, UART_TX_DMA_BUF_SIZE);
    if (send_len == 0)
    {
        elog_verbose(TAG, "No data in TX ringbuffer to send");
        return;
    }

    // 标记为发送中
    dev->tx_busy = 1;
    elog_debug(TAG, "Starting UART TX DMA with %d bytes", send_len);

    // 启动DMA非阻塞发送
    if (HAL_UART_Transmit_DMA(dev->uart_device, dev->uart_dma_tx_buf, send_len) != HAL_OK)
    {
        elog_error(TAG, "Failed to start UART TX DMA");
        dev->tx_busy = 0; // 发送失败，重置忙标志
        return;
    }
}

// 新增：DMA发送完成回调（需要在stm32xxxx_it.c中调用，或直接挂载HAL回调）
void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    for (int i = 0; i < MAX_UART_DEVICE; ++i)
    {
        if (uart_device_list[i] != NULL && uart_device_list[i]->initialised)
        {
            if (uart_device_list[i]->uart_device->Instance == huart->Instance)
            {
                UART_Handle dev = uart_device_list[i];
                elog_verbose(TAG, "UART TX DMA completed");
                dev->tx_busy = 0; // 清除发送忙标志

                // 检查发送缓冲区是否还有数据，有则继续发送
                uint16_t tx_len = (dev->tx_ringbuf.head - dev->tx_ringbuf.tail + TX_RING_BUFFER_SIZE) %
                    TX_RING_BUFFER_SIZE;
                if (tx_len > 0)
                {
                    elog_verbose(TAG, "TX ringbuffer has %d bytes left, continuing transmission", tx_len);
                    UART_Start_DMATx(dev);
                }
                else
                {
                    elog_verbose(TAG, "TX ringbuffer empty, UART entering idle state");
                }

                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                break;
            }
        }
    }
}

// 原有接收回调保持不变
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size)
{
    for (int i = 0; i < MAX_UART_DEVICE; ++i)
    {
        if (uart_device_list[i] != NULL && uart_device_list[i]->initialised)
        {
            if (uart_device_list[i]->uart_device->Instance == huart->Instance)
            {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                elog_verbose(TAG, "UART RX idle interrupt triggered, received %d bytes", size);

                // 优化3：快速写入 RingBuffer
                RingBuffer_Write_ISR(&uart_device_list[i]->uart_ringbuf,
                                     uart_device_list[i]->uart_dma_rx_buf,
                                     size);

                // 释放信号量
                xSemaphoreGiveFromISR(uart_device_list[i]->rx_sem, &xHigherPriorityTaskWoken);

                // 重新开启 DMA 接收
                elog_verbose(TAG, "Restarting UART RX DMA");
                HAL_UARTEx_ReceiveToIdle_DMA(uart_device_list[i]->uart_device,
                                             uart_device_list[i]->uart_dma_rx_buf,
                                             UART_RX_DMA_BUF_SIZE);

                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                break;
            }
        }
    }
}
