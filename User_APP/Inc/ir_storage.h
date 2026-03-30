#ifndef __IR_STORAGE_H
#define __IR_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 红外遥控器存储管理模块
 * @note 基于W25Q32 (4MB) NOR Flash，支持双备份掉电保护
 */

/* 存储布局常量 */
#define STORAGE_MAGIC               0x49524153  /* "IRAS" */
#define STORAGE_VERSION             0x0001

#define STORAGE_SECTOR_SIZE         4096        /* 扇区大小 4KB */
#define STORAGE_FLASH_SIZE          (4 * 1024 * 1024)  /* 4MB */

/* 扇区布局 */
#define STORAGE_SYSTEM_A_SECTOR     0           /* 系统信息A: 扇区0-1 */
#define STORAGE_SYSTEM_B_SECTOR     512         /* 系统信息B: 扇区512-513 */
#define STORAGE_INDEX_A_SECTOR      2           /* 遥控器索引A: 扇区2-3 */
#define STORAGE_INDEX_B_SECTOR      514         /* 遥控器索引B: 扇区514-515 */
#define STORAGE_DATA_A_SECTOR       4           /* 数据区域A: 扇区4-511 */
#define STORAGE_DATA_B_SECTOR       516         /* 数据区域B: 扇区516-1023 */

/* 容量限制 */
#define STORAGE_MAX_REMOTES         10          /* 最大遥控器数量 */
#define STORAGE_MAX_KEYS_PER_REMOTE 20          /* 每个遥控器最大按键数 */
#define STORAGE_MAX_KEY_DATA_LEN    1024        /* 按键数据最大长度（uint32_t个数） */

/* 名称长度限制 */
#define STORAGE_REMOTE_NAME_LEN     32
#define STORAGE_KEY_NAME_LEN        32

/**
 * @brief 系统信息结构 (64字节)
 */
typedef struct {
    uint32_t magic;                 /* 魔数 0x49524153 ("IRAS") */
    uint16_t version;               /* 版本号 */
    uint16_t remote_count;          /* 遥控器数量 */
    uint32_t total_keys;            /* 总按键数 */
    uint32_t checksum;              /* CRC32校验 */
    uint8_t  reserved[48];          /* 预留空间 */
} system_info_t;

/**
 * @brief 遥控器索引结构 (64字节)
 */
typedef struct {
    uint16_t id;                    /* 遥控器ID (0-9) */
    uint16_t key_count;             /* 按键数量 */
    char     name[STORAGE_REMOTE_NAME_LEN];  /* 遥控器名称 */
    uint32_t data_offset;           /* 数据区域偏移 (扇区号) */
    uint32_t checksum;              /* CRC32校验 */
    uint8_t  reserved[20];          /* 预留空间 */
} remote_index_t;

/**
 * @brief 按键数据结构 (4160字节)
 */
typedef struct {
    uint16_t id;                    /* 按键ID */
    char     name[STORAGE_KEY_NAME_LEN];  /* 按键名称 */
    uint32_t data_len;              /* 红外数据长度 (uint32_t个数) */
    uint32_t checksum;              /* CRC32校验 */
    uint32_t data[STORAGE_MAX_KEY_DATA_LEN];  /* 红外数据 (最大4KB) */
} key_data_t;

/**
 * @brief 遥控器信息结构（用于API返回）
 */
typedef struct {
    uint16_t id;
    char     name[STORAGE_REMOTE_NAME_LEN];
    uint16_t key_count;
} remote_info_t;

/**
 * @brief 按键信息结构（用于API返回）
 */
typedef struct {
    uint16_t id;
    char     name[STORAGE_KEY_NAME_LEN];
    uint32_t data_len;
} key_info_t;

/**
 * @brief 存储消息类型（用于LVGL通信）
 */
typedef enum {
    STORAGE_MSG_ADD_REMOTE,         /* 添加遥控器 */
    STORAGE_MSG_DELETE_REMOTE,      /* 删除遥控器 */
    STORAGE_MSG_ADD_KEY,            /* 添加按键 */
    STORAGE_MSG_DELETE_KEY,         /* 删除按键 */
    STORAGE_MSG_UPDATE_KEY,         /* 更新按键数据 */
    STORAGE_MSG_GET_LIST,           /* 获取列表 */
    STORAGE_MSG_OPERATION_COMPLETE, /* 操作完成 */
    STORAGE_MSG_ERROR               /* 错误 */
} storage_msg_type_t;

/**
 * @brief 存储消息结构（用于LVGL通信）
 */
typedef struct {
    storage_msg_type_t type;        /* 消息类型 */
    uint32_t remote_id;             /* 遥控器ID */
    uint32_t key_id;                /* 按键ID */
    void*    data;                  /* 数据指针 */
    uint32_t data_len;              /* 数据长度 */
    int32_t  result;                /* 操作结果 */
} storage_msg_t;

/**
 * @brief 存储操作结果
 */
typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERROR,
    STORAGE_ERROR_INVALID_PARAM,
    STORAGE_ERROR_NOT_FOUND,
    STORAGE_ERROR_FULL,
    STORAGE_ERROR_CHECKSUM,
    STORAGE_ERROR_WRITE,
    STORAGE_ERROR_READ,
    STORAGE_ERROR_BUSY
} storage_status_t;

/**
 * @brief 存储管理句柄
 */
typedef struct {
    SemaphoreHandle_t mutex;        /* 互斥锁 */
    QueueHandle_t msg_queue;        /* LVGL消息队列 */
    system_info_t   sys_info;       /* 系统信息缓存 */
    remote_index_t  index_table[STORAGE_MAX_REMOTES]; /* 索引表缓存 */
} storage_handle_t;

/**
 * @brief 初始化存储系统
 * @return 存储句柄，NULL表示失败
 */
storage_handle_t* Storage_Init(void);

/**
 * @brief 格式化存储，清除所有数据
 * @param handle 存储句柄
 * @return 操作结果
 */
storage_status_t Storage_Format(storage_handle_t* handle);

/**
 * @brief 添加新遥控器
 * @param handle 存储句柄
 * @param name 遥控器名称
 * @param[out] remote_id 返回的遥控器ID
 * @return 操作结果
 */
storage_status_t Storage_AddRemote(storage_handle_t* handle, const char* name, uint16_t* remote_id);

/**
 * @brief 删除遥控器
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @return 操作结果
 */
storage_status_t Storage_DeleteRemote(storage_handle_t* handle, uint16_t remote_id);

/**
 * @brief 获取遥控器列表
 * @param handle 存储句柄
 * @param[out] list 遥控器信息数组
 * @param max_count 数组最大容量
 * @param[out] count 实际遥控器数量
 * @return 操作结果
 */
storage_status_t Storage_GetRemoteList(storage_handle_t* handle, remote_info_t* list, 
                                       uint16_t max_count, uint16_t* count);

/**
 * @brief 获取遥控器详细信息
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @param[out] info 遥控器信息
 * @return 操作结果
 */
storage_status_t Storage_GetRemoteInfo(storage_handle_t* handle, uint16_t remote_id, 
                                       remote_info_t* info);

/**
 * @brief 添加按键
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @param name 按键名称
 * @param data 红外数据
 * @param len 数据长度（uint32_t个数）
 * @param[out] key_id 返回的按键ID
 * @return 操作结果
 */
storage_status_t Storage_AddKey(storage_handle_t* handle, uint16_t remote_id, 
                                const char* name, const uint32_t* data, uint32_t len,
                                uint16_t* key_id);

/**
 * @brief 删除按键
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @param key_id 按键ID
 * @return 操作结果
 */
storage_status_t Storage_DeleteKey(storage_handle_t* handle, uint16_t remote_id, 
                                   uint16_t key_id);

/**
 * @brief 获取按键数据
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @param key_id 按键ID
 * @param[out] key_data 按键数据
 * @return 操作结果
 */
storage_status_t Storage_GetKeyData(storage_handle_t* handle, uint16_t remote_id, 
                                    uint16_t key_id, key_data_t* key_data);

/**
 * @brief 更新按键数据
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @param key_id 按键ID
 * @param data 新的红外数据
 * @param len 数据长度（uint32_t个数）
 * @return 操作结果
 */
storage_status_t Storage_UpdateKey(storage_handle_t* handle, uint16_t remote_id, 
                                   uint16_t key_id, const uint32_t* data, uint32_t len);

/**
 * @brief 获取按键列表
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @param[out] list 按键信息数组
 * @param max_count 数组最大容量
 * @param[out] count 实际按键数量
 * @return 操作结果
 */
storage_status_t Storage_GetKeyList(storage_handle_t* handle, uint16_t remote_id,
                                    key_info_t* list, uint16_t max_count, uint16_t* count);

/**
 * @brief 获取LVGL消息队列
 * @param handle 存储句柄
 * @return 消息队列句柄
 */
QueueHandle_t Storage_GetMsgQueue(storage_handle_t* handle);

/**
 * @brief 发送消息到存储任务
 * @param handle 存储句柄
 * @param msg 消息指针
 * @return 操作结果
 */
storage_status_t Storage_SendMsg(storage_handle_t* handle, const storage_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif /* __IR_STORAGE_H */
