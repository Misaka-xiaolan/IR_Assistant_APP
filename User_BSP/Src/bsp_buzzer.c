//
// Created by skinm on 2026/3/19.
//

#include "bsp_buzzer.h"

#define TAG "Buzzer"

#define DEFAULT_BEEP_MS 200
#define DEFAULT_BEEP_FREQ 1000
#define DEFAULT_QUEUE_SIZE  10

// 内部命令结构
typedef struct
{
    uint32_t duration_ms;
    uint32_t frequency_hz;
} buzzer_cmd_t;

// 设备结构
struct Buzzer_device_t
{
    buzzer_config_t config;
    TaskHandle_t buzzer_task;
    QueueHandle_t cmd_queue;
    uint8_t initialized;
};

// 函数声明
static void Buzzer_HW_Init(Buzzer_Handle handle);
static void Buzzer_Task(void* param);
static void Buzzer_SetFrequency(Buzzer_Handle handle, uint32_t freq_hz);
static void Buzzer_HW_Start(Buzzer_Handle handle, uint32_t freq_hz);
static void Buzzer_HW_Stop(Buzzer_Handle handle);

Buzzer_Handle Buzzer_Init(buzzer_config_t* config)
{
    if (!config)
    {
        elog_error(TAG, "Config is NULL");
        return NULL;
    }

    // 分配设备内存
    Buzzer_Handle dev = (Buzzer_Handle)pvPortMalloc(sizeof(struct Buzzer_device_t));
    if (!dev)
    {
        elog_error(TAG, "Buzzer malloc failed");
        return NULL;
    }
    memset(dev, 0, sizeof(struct Buzzer_device_t));

    // 复制配置
    memcpy(&dev->config, config, sizeof(buzzer_config_t));

    // 设置默认值
    if (dev->config.default_beep_ms == 0)
    {
        dev->config.default_beep_ms = DEFAULT_BEEP_MS;
    }
    if (dev->config.default_beep_freq == 0)
    {
        dev->config.default_beep_freq = DEFAULT_BEEP_FREQ;
    }
    if (dev->config.max_queue_size == 0)
    {
        dev->config.max_queue_size = DEFAULT_QUEUE_SIZE;
    }

    // 硬件初始化
    Buzzer_HW_Init(dev);
    if (dev->config.type == BUZZER_TYPE_PASSIVE && !dev->config.pwm_tim)
    {
        elog_error(TAG, "Passive buzzer requires valid TIM handle");
        vPortFree(dev);
        return NULL;
    }

    // 创建队列和任务（如果启用）
    if (dev->config.max_queue_size > 0)
    {
        dev->cmd_queue = xQueueCreate(dev->config.max_queue_size, sizeof(buzzer_cmd_t));
        if (!dev->cmd_queue)
        {
            elog_error(TAG, "Buzzer queue create failed");
            vPortFree(dev);
            return NULL;
        }

        if (xTaskCreate(Buzzer_Task, "BuzzerTask", configMINIMAL_STACK_SIZE + 128, dev, 20, &dev->buzzer_task) !=
            pdPASS)
        {
            elog_error(TAG, "Buzzer task create failed");
            vQueueDelete(dev->cmd_queue);
            vPortFree(dev);
            return NULL;
        }
    }

    dev->initialized = 1;
    elog_info(TAG, "Buzzer device initialized (type=%s)",
              dev->config.type == BUZZER_TYPE_ACTIVE ? "Active" : "Passive");
    return dev;
}

void Buzzer_Beep(Buzzer_Handle handle, uint32_t duration_ms, uint32_t freq_hz)
{
    if (!handle || !handle->initialized)
    {
        elog_error(TAG, "Buzzer handle not initialized");
        return;
    }

    if (handle->cmd_queue)
    {
        buzzer_cmd_t cmd = {
            .duration_ms = duration_ms ? duration_ms : handle->config.default_beep_ms,
            .frequency_hz = freq_hz ? freq_hz : handle->config.default_beep_freq
        }; // 默认 1kHz
        BaseType_t ret = xQueueSendToBack(handle->cmd_queue, &cmd, 0);
        if (ret != pdTRUE)
        {
            elog_warn(TAG, "Buzzer queue full, beep dropped");
        }
    }
    else
    {
        // 无队列：直接播放（会阻塞调用者）
        uint32_t dur = duration_ms ? duration_ms : handle->config.default_beep_ms;
        Buzzer_HW_Start(handle, dur);
        vTaskDelay(pdMS_TO_TICKS(dur));
        Buzzer_HW_Stop(handle);
    }
}

void Buzzer_Stop(Buzzer_Handle handle)
{
    if (!handle || !handle->initialized)
    {
        return;
    }
    Buzzer_HW_Stop(handle);
}

static void Buzzer_HW_Init(Buzzer_Handle handle)
{
    struct Buzzer_device_t* dev = (struct Buzzer_device_t*)handle;

    if (dev->config.type == BUZZER_TYPE_ACTIVE)
    {
        // 有源：配置 GPIO 为推挽输出
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = dev->config.gpio_pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(dev->config.gpio_port, &GPIO_InitStruct);
        HAL_GPIO_WritePin(dev->config.gpio_port, dev->config.gpio_pin, GPIO_PIN_RESET);
    }
    else
    {
        // 注意：用户必须在调用 Buzzer_Init 前正确初始化 TIM/PWM
        if (!dev->config.pwm_tim)
        {
            elog_error(TAG, "Invalid TIM for passive buzzer");
            return;
        }
        // 初始关闭 PWM 输出
        __HAL_TIM_SET_COMPARE(dev->config.pwm_tim, dev->config.pwm_channel, 0);
        HAL_TIM_PWM_Stop(dev->config.pwm_tim, dev->config.pwm_channel);
    }
}

static void Buzzer_SetFrequency(Buzzer_Handle handle, uint32_t freq_hz)
{
    if (freq_hz == 0)
    {
        // 停止输出
        HAL_TIM_PWM_Stop(handle->config.pwm_tim, TIM_CHANNEL_1);
        return;
    }

    uint32_t timer_clk = 100000000UL; // STM32F411定时器主频为100MHz
    uint32_t prescaler = 0;
    uint32_t period = 0;

    // 目标：timer_clk / (PSC + 1) / (ARR + 1) = freq_hz
    // 先尝试让 ARR 不超过 0xFFFF
    prescaler = 1;
    period = timer_clk / freq_hz / prescaler;

    // 如果 period 太大，则增加预分频
    while (period > 0xFFFF)
    {
        prescaler++;
        period = timer_clk / freq_hz / prescaler;
        if (prescaler > 0xFFFF)
        {
            // 超出范围，频率太低，直接退出
            elog_error(TAG, "Buzzer PWM calculation FAIL");
            return;
        }
    }

    // 更新定时器配置
    __HAL_TIM_DISABLE(handle->config.pwm_tim);
    __HAL_TIM_SET_PRESCALER(handle->config.pwm_tim, prescaler - 1);
    __HAL_TIM_SET_AUTORELOAD(handle->config.pwm_tim, period - 1);
    __HAL_TIM_SET_COMPARE(handle->config.pwm_tim, handle->config.pwm_channel, (period - 1) / 2);
    __HAL_TIM_SET_COUNTER(handle->config.pwm_tim, 0);
    __HAL_TIM_ENABLE(handle->config.pwm_tim);
}

static void Buzzer_HW_Start(Buzzer_Handle handle, uint32_t freq_hz)
{
    if (handle->config.type == BUZZER_TYPE_ACTIVE)
    {
        HAL_GPIO_WritePin(handle->config.gpio_port, handle->config.gpio_pin, GPIO_PIN_SET);
    }
    else
    {
        if (handle->config.pwm_tim)
        {
            // 启动 PWM（占空比 50%，频率 1kHz 示例）
            Buzzer_SetFrequency(handle, freq_hz);
            HAL_TIM_PWM_Start(handle->config.pwm_tim, handle->config.pwm_channel);
        }
    }
}

static void Buzzer_HW_Stop(Buzzer_Handle handle)
{
    if (handle->config.type == BUZZER_TYPE_ACTIVE)
    {
        HAL_GPIO_WritePin(handle->config.gpio_port, handle->config.gpio_pin, GPIO_PIN_RESET);
    }
    else
    {
        if (handle->config.pwm_tim)
        {
            HAL_TIM_PWM_Stop(handle->config.pwm_tim, handle->config.pwm_channel);
            __HAL_TIM_SET_COMPARE(handle->config.pwm_tim, handle->config.pwm_channel, 0);
        }
    }
}

static void Buzzer_Task(void* param)
{
    Buzzer_Handle handle = param;
    buzzer_cmd_t cmd;

    elog_info(TAG, "Buzzer task started");

    while (1)
    {
        if (xQueueReceive(handle->cmd_queue, &cmd, portMAX_DELAY) == pdTRUE)
        {
            Buzzer_HW_Start(handle, cmd.frequency_hz);
            vTaskDelay(pdMS_TO_TICKS(cmd.duration_ms));
            Buzzer_HW_Stop(handle);
        }
    }
}
