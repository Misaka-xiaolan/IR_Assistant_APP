#ifndef FREERTOS_DEBUG_H
#define FREERTOS_DEBUG_H

void FreertosDebug_Init(void);
void FreertosDebug_ProcessShell(void);
void Debug_PrintTaskInfo(void);
void Debug_PrintHeapInfo(void);

#endif