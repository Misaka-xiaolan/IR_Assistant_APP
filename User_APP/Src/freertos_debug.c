#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "string.h"
#include "stdio.h"
#include "main.h"
#include "bsp_uart.h"

#define TAG "RTOS_DBG"

#define DEBUG_TASK_STACK_SIZE    512
#define DEBUG_TASK_PRIORITY      10
#define MONITOR_INTERVAL_MS      2000

QueueHandle_t shellQueue;
TaskHandle_t monitorTaskHandle;

static void print_task_info(void);
static void print_heap_info(void);
static void monitor_task(void *pvParameters);

void FreertosDebug_Init(void)
{
    xTaskCreate(monitor_task, "Monitor", DEBUG_TASK_STACK_SIZE, NULL, DEBUG_TASK_PRIORITY, &monitorTaskHandle);
}

static void monitor_task(void *pvParameters)
{
    (void)pvParameters;
    
    for (;;)
    {
        print_task_info();
        print_heap_info();
        
        vTaskDelay(MONITOR_INTERVAL_MS);
    }
}

static void print_task_info(void)
{
    TaskStatus_t *pxTaskStatusArray;
    UBaseType_t uxArraySize;
    uint32_t totalRuntime;

    // 1. 获取任务数量并多预留一点空间，防止在此期间有新任务创建
    uxArraySize = uxTaskGetNumberOfTasks() + 3;
    pxTaskStatusArray = (TaskStatus_t *)pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

    if (pxTaskStatusArray != NULL)
    {
        // 2. 获取系统快照
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &totalRuntime);

        printf("\r\n%-16s | State | Prio | StackMinFree\r\n", "Name");
        printf("--------------------------------------------------\r\n");

        for (UBaseType_t x = 0; x < uxArraySize; x++)
        {
            char state = 'U'; // Unknown
            // 3. 解析状态
            switch (pxTaskStatusArray[x].eCurrentState)
            {
            case eRunning:   state = 'X'; break;
            case eReady:     state = 'R'; break;
            case eBlocked:   state = 'B'; break;
            case eSuspended: state = 'S'; break;
            case eDeleted:   state = 'D'; break;
            default:         state = 'U'; break;
            }

            // 4. 直接利用结构体里的 usStackHighWaterMark，不要再调用额外函数
            printf("%-16s |   %c   |  %2u  | %7u\r\n",
                   pxTaskStatusArray[x].pcTaskName,
                   state,
                   (unsigned int)pxTaskStatusArray[x].uxCurrentPriority,
                   (unsigned int)pxTaskStatusArray[x].usStackHighWaterMark);
        }

        vPortFree(pxTaskStatusArray);
    }
    else
    {
        printf("Malloc failed for task info!\r\n");
    }
}

static void print_heap_info(void)
{
    // 获取更详细的堆统计信息 (需要 FreeRTOS 10.4.0+)
    HeapStats_t xHeapStats;
    vPortGetHeapStats(&xHeapStats);

    size_t totalHeap = configTOTAL_HEAP_SIZE;
    size_t freeHeap = xHeapStats.xAvailableHeapSpaceInBytes;
    size_t usedHeap = totalHeap - freeHeap;

    printf("\r\n========== Heap Status ==========\r\n");
    printf("Total Size:    %u bytes\r\n", (unsigned int)totalHeap);
    printf("Current Used:  %u bytes (%u%%)\r\n",
            (unsigned int)usedHeap,
            (unsigned int)((usedHeap * 100) / totalHeap));
    printf("Current Free:  %lu bytes\r\n", (unsigned int)freeHeap);

    /* 碎片化监控关键指标 */
    printf("Min Ever Free: %u bytes\r\n", (unsigned int)xHeapStats.xMinimumEverFreeBytesRemaining);
    printf("Max Block:     %u bytes\r\n", (unsigned int)xHeapStats.xSizeOfLargestFreeBlockInBytes);
    printf("Free Blocks:   %u \r\n", (unsigned int)xHeapStats.xNumberOfFreeBlocks);

    printf("=================================\r\n");
}

void Debug_PrintTaskInfo(void)
{
    print_task_info();
}

void Debug_PrintHeapInfo(void)
{
    print_heap_info();
}
