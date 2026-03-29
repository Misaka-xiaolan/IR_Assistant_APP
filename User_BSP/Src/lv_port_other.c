//
// Created by skinm on 2026/3/28.
//

#include "lv_port_other.h"
#include "elog.h"
#include "src/tick/lv_tick.h"


#define TAG "lvgl"

void lvgl_log_print(lv_log_level_t level, const char * buf)
{
    if (level == LV_LOG_LEVEL_ERROR)
        elog_error(TAG, "%s", buf);
    else if (level == LV_LOG_LEVEL_WARN)
        elog_warn(TAG, "%s", buf);
    else if (level == LV_LOG_LEVEL_INFO)
        elog_info(TAG, "%s", buf);
    else if (level == LV_LOG_LEVEL_TRACE)
        elog_debug(TAG, "%s", buf);
    else
        elog_verbose(TAG, "%s", buf);
}

void lv_port_delay_cb(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

