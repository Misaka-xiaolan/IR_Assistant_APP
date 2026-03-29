//
// Created by skinm on 2026/3/28.
//

#ifndef IR_ASSISTANT_TEST_LV_PORT_LOG_H
#define IR_ASSISTANT_TEST_LV_PORT_LOG_H

#include "main.h"
#include "src/misc/lv_log.h"

void lvgl_log_print(lv_log_level_t level, const char * buf);
void lv_port_delay_cb(uint32_t ms);

#endif //IR_ASSISTANT_TEST_LV_PORT_LOG_H