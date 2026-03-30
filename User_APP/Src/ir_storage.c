#include "ir_storage.h"
#include "crc32.h"
#include "bsp_norflash.h"
#include <string.h>

/**
 * @brief 红外遥控器存储管理模块实现
 */

/* 扇区大小定义 */
#define SECTOR_SIZE                 STORAGE_SECTOR_SIZE
#define SYSTEM_INFO_SIZE            sizeof(system_info_t)
#define REMOTE_INDEX_SIZE           sizeof(remote_index_t)
#define KEY_DATA_SIZE               sizeof(key_data_t)

/* 计算扇区地址对应的字节地址 */
#define SECTOR_TO_ADDR(sector)      ((sector) * SECTOR_SIZE)

/**
 * @brief 计算结构体的CRC32校验值（排除checksum字段本身）
 * @param data 数据指针
 * @param size 数据大小
 * @param checksum_offset checksum字段在结构体中的偏移
 * @return CRC32校验值
 */
static uint32_t Calculate_Struct_Checksum(const void* data, size_t size, size_t checksum_offset)
{
    uint32_t crc = CRC32_Init();
    
    /* 计算checksum字段之前的数据 */
    if (checksum_offset > 0) {
        crc = CRC32_Calculate(data, checksum_offset, crc);
    }
    
    /* 跳过checksum字段（4字节），计算之后的数据 */
    size_t after_checksum = checksum_offset + sizeof(uint32_t);
    if (after_checksum < size) {
        crc = CRC32_Calculate((const uint8_t*)data + after_checksum, 
                             size - after_checksum, crc);
    }
    
    return crc ^ 0xFFFFFFFF;
}

/**
 * @brief 验证系统信息的校验和
 * @param data 系统信息指针
 * @return true: 校验成功, false: 校验失败
 */
static bool Verify_System_Info(const void* data)
{
    const system_info_t* info = (const system_info_t*)data;
    if (info->magic != STORAGE_MAGIC) {
        return false;
    }
    
    uint32_t expected_crc = Calculate_Struct_Checksum(info, SYSTEM_INFO_SIZE, 
                                                       offsetof(system_info_t, checksum));
    return (info->checksum == expected_crc);
}

/**
 * @brief 验证遥控器索引的校验和
 * @param index 遥控器索引指针
 * @return true: 校验成功, false: 校验失败
 */
static bool Verify_Remote_Index(const remote_index_t* index)
{
    uint32_t expected_crc = Calculate_Struct_Checksum(index, REMOTE_INDEX_SIZE,
                                                       offsetof(remote_index_t, checksum));
    return (index->checksum == expected_crc);
}

/**
 * @brief 验证遥控器索引的校验和（用于Read_With_Recovery的包装函数）
 * @param data 遥控器索引指针
 * @return true: 校验成功, false: 校验失败
 */
static bool Verify_Remote_Index_Wrapper(const void* data)
{
    return Verify_Remote_Index((const remote_index_t*)data);
}

/**
 * @brief 验证按键数据的校验和
 * @param data 按键数据指针
 * @return true: 校验成功, false: 校验失败
 */
static bool Verify_Key_Data(const void* data)
{
    const key_data_t* key = (const key_data_t*)data;
    uint32_t expected_crc = Calculate_Struct_Checksum(key, KEY_DATA_SIZE,
                                                       offsetof(key_data_t, checksum));
    return (key->checksum == expected_crc);
}

/**
 * @brief 写入数据到指定扇区（带双备份）
 * @param sector_a 主存储扇区
 * @param sector_b 备份存储扇区
 * @param data 数据指针
 * @param size 数据大小
 * @return 操作结果
 */
static storage_status_t Write_With_Backup(uint32_t sector_a, uint32_t sector_b,
                                          const void* data, size_t size)
{
    uint8_t verify_buf[SECTOR_SIZE];
    
    /* 写入主存储区域 */
    Norflash_Write((uint8_t*)data, SECTOR_TO_ADDR(sector_a), size);
    
    /* 验证主存储写入 */
    Norflash_Read(verify_buf, SECTOR_TO_ADDR(sector_a), size);
    if (memcmp(data, verify_buf, size) != 0) {
        return STORAGE_ERROR_WRITE;
    }
    
    /* 写入备份存储区域 */
    Norflash_Write((uint8_t*)data, SECTOR_TO_ADDR(sector_b), size);
    
    /* 验证备份存储写入 */
    Norflash_Read(verify_buf, SECTOR_TO_ADDR(sector_b), size);
    if (memcmp(data, verify_buf, size) != 0) {
        return STORAGE_ERROR_WRITE;
    }
    
    return STORAGE_OK;
}

/**
 * @brief 读取数据（带双备份恢复）
 * @param sector_a 主存储扇区
 * @param sector_b 备份存储扇区
 * @param data 输出数据缓冲区
 * @param size 数据大小
 * @param verify_func 校验函数指针
 * @return 操作结果
 */
static storage_status_t Read_With_Recovery(uint32_t sector_a, uint32_t sector_b,
                                           void* data, size_t size,
                                           bool (*verify_func)(const void*))
{
    uint8_t backup_buf[SECTOR_SIZE];
    
    /* 读取主存储 */
    Norflash_Read((uint8_t*)data, SECTOR_TO_ADDR(sector_a), size);
    
    /* 验证主存储数据 */
    if (verify_func == NULL || verify_func(data)) {
        return STORAGE_OK;
    }
    
    /* 主存储校验失败，读取备份存储 */
    Norflash_Read(backup_buf, SECTOR_TO_ADDR(sector_b), size);
    
    /* 验证备份存储数据 */
    if (verify_func(backup_buf)) {
        /* 备份数据有效，恢复主存储 */
        memcpy(data, backup_buf, size);
        Norflash_Write((uint8_t*)data, SECTOR_TO_ADDR(sector_a), size);
        return STORAGE_OK;
    }
    
    /* 主存储和备份存储都损坏 */
    return STORAGE_ERROR_CHECKSUM;
}

/**
 * @brief 查找空闲的遥控器索引槽位
 * @param handle 存储句柄
 * @return 空闲槽位索引，-1表示没有空闲槽位
 */
static int16_t Find_Free_Remote_Slot(const storage_handle_t* handle)
{
    for (int16_t i = 0; i < STORAGE_MAX_REMOTES; i++) {
        if (handle->index_table[i].id == 0xFFFF || 
            handle->index_table[i].key_count == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 查找遥控器在索引表中的位置
 * @param handle 存储句柄
 * @param remote_id 遥控器ID
 * @return 索引位置，-1表示未找到
 */
static int16_t Find_Remote_Index(const storage_handle_t* handle, uint16_t remote_id)
{
    for (int16_t i = 0; i < STORAGE_MAX_REMOTES; i++) {
        if (handle->index_table[i].id == remote_id && 
            handle->index_table[i].key_count > 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 保存系统信息
 * @param handle 存储句柄
 * @return 操作结果
 */
static storage_status_t Save_System_Info(storage_handle_t* handle)
{
    handle->sys_info.checksum = Calculate_Struct_Checksum(&handle->sys_info, 
                                                           SYSTEM_INFO_SIZE,
                                                           offsetof(system_info_t, checksum));
    
    return Write_With_Backup(STORAGE_SYSTEM_A_SECTOR, STORAGE_SYSTEM_B_SECTOR,
                            &handle->sys_info, SYSTEM_INFO_SIZE);
}

/**
 * @brief 保存遥控器索引表
 * @param handle 存储句柄
 * @return 操作结果
 */
static storage_status_t Save_Remote_Index(storage_handle_t* handle)
{
    /* 计算每个索引的校验和 */
    for (int i = 0; i < STORAGE_MAX_REMOTES; i++) {
        if (handle->index_table[i].key_count > 0) {
            handle->index_table[i].checksum = 
                Calculate_Struct_Checksum(&handle->index_table[i], REMOTE_INDEX_SIZE,
                                         offsetof(remote_index_t, checksum));
        }
    }
    
    return Write_With_Backup(STORAGE_INDEX_A_SECTOR, STORAGE_INDEX_B_SECTOR,
                            handle->index_table, sizeof(handle->index_table));
}

/**
 * @brief 读取按键数据
 * @param data_sector 数据区域起始扇区
 * @param remote_offset 遥控器在数据区域的偏移（扇区数）
 * @param key_id 按键ID
 * @param[out] key_data 按键数据
 * @return 操作结果
 */
static storage_status_t Read_Key_Data(uint32_t data_sector, uint32_t remote_offset,
                                      uint16_t key_id, key_data_t* key_data)
{
    /* 每个按键占用2个扇区（主存储+备份），每个遥控器最多20个按键
     * 所以每个遥控器需要40个扇区来存储按键数据 */
    uint32_t key_sectors_per_remote = STORAGE_MAX_KEYS_PER_REMOTE * 2;
    uint32_t base_sector_a = data_sector + remote_offset * key_sectors_per_remote;
    uint32_t base_sector_b = data_sector + (STORAGE_DATA_B_SECTOR - STORAGE_DATA_A_SECTOR) + 
                             remote_offset * key_sectors_per_remote;
    
    uint32_t key_sector_a = base_sector_a + key_id * 2;
    uint32_t key_sector_b = base_sector_b + key_id * 2;
    
    return Read_With_Recovery(key_sector_a, key_sector_b, key_data, KEY_DATA_SIZE,
                             Verify_Key_Data);
}

/**
 * @brief 写入按键数据
 * @param data_sector 数据区域起始扇区
 * @param remote_offset 遥控器在数据区域的偏移（扇区数）
 * @param key_id 按键ID
 * @param key_data 按键数据
 * @return 操作结果
 */
static storage_status_t Write_Key_Data(uint32_t data_sector, uint32_t remote_offset,
                                       uint16_t key_id, const key_data_t* key_data)
{
    uint32_t key_sectors_per_remote = STORAGE_MAX_KEYS_PER_REMOTE * 2;
    uint32_t base_sector_a = data_sector + remote_offset * key_sectors_per_remote;
    uint32_t base_sector_b = data_sector + (STORAGE_DATA_B_SECTOR - STORAGE_DATA_A_SECTOR) + 
                             remote_offset * key_sectors_per_remote;
    
    uint32_t key_sector_a = base_sector_a + key_id * 2;
    uint32_t key_sector_b = base_sector_b + key_id * 2;
    
    return Write_With_Backup(key_sector_a, key_sector_b, key_data, KEY_DATA_SIZE);
}

storage_handle_t* Storage_Init(void)
{
    static storage_handle_t handle;
    
    /* 创建互斥锁 */
    handle.mutex = xSemaphoreCreateMutex();
    if (handle.mutex == NULL) {
        return NULL;
    }
    
    /* 创建LVGL消息队列 */
    handle.msg_queue = xQueueCreate(10, sizeof(storage_msg_t));
    if (handle.msg_queue == NULL) {
        vSemaphoreDelete(handle.mutex);
        return NULL;
    }
    
    /* 初始化NOR Flash */
    Norflash_Init();
    
    /* 读取系统信息（带双备份恢复） */
    storage_status_t status = Read_With_Recovery(STORAGE_SYSTEM_A_SECTOR, 
                                                  STORAGE_SYSTEM_B_SECTOR,
                                                  &handle.sys_info, 
                                                  SYSTEM_INFO_SIZE,
                                                  Verify_System_Info);
    
    if (status != STORAGE_OK) {
        /* 系统信息损坏，执行格式化 */
        Storage_Format(&handle);
    }
    
    /* 读取遥控器索引表（带双备份恢复） */
    status = Read_With_Recovery(STORAGE_INDEX_A_SECTOR, STORAGE_INDEX_B_SECTOR,
                                handle.index_table, sizeof(handle.index_table),
                                Verify_Remote_Index_Wrapper); /* 索引表单独验证 */
    
    if (status != STORAGE_OK) {
        /* 索引表损坏，清空 */
        memset(handle.index_table, 0xFF, sizeof(handle.index_table));
        Save_Remote_Index(&handle);
    }
    
    return &handle;
}

storage_status_t Storage_Format(storage_handle_t* handle)
{
    if (handle == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 擦除系统信息区域 */
    Norflash_Erase_Sector(STORAGE_SYSTEM_A_SECTOR);
    Norflash_Erase_Sector(STORAGE_SYSTEM_A_SECTOR + 1);
    Norflash_Erase_Sector(STORAGE_SYSTEM_B_SECTOR);
    Norflash_Erase_Sector(STORAGE_SYSTEM_B_SECTOR + 1);
    
    /* 擦除索引表区域 */
    Norflash_Erase_Sector(STORAGE_INDEX_A_SECTOR);
    Norflash_Erase_Sector(STORAGE_INDEX_A_SECTOR + 1);
    Norflash_Erase_Sector(STORAGE_INDEX_B_SECTOR);
    Norflash_Erase_Sector(STORAGE_INDEX_B_SECTOR + 1);
    
    /* 擦除数据区域（完整擦除） */
    /* 计算数据区域总扇区数 */
    uint32_t data_sectors = STORAGE_DATA_B_SECTOR - STORAGE_DATA_A_SECTOR;
    for (uint32_t i = 0; i < data_sectors; i++) {
        Norflash_Erase_Sector(STORAGE_DATA_A_SECTOR + i);
        Norflash_Erase_Sector(STORAGE_DATA_B_SECTOR + i);
    }
    
    /* 初始化系统信息 */
    memset(&handle->sys_info, 0, SYSTEM_INFO_SIZE);
    handle->sys_info.magic = STORAGE_MAGIC;
    handle->sys_info.version = STORAGE_VERSION;
    handle->sys_info.remote_count = 0;
    handle->sys_info.total_keys = 0;
    
    /* 初始化索引表 */
    memset(handle->index_table, 0xFF, sizeof(handle->index_table));
    
    /* 保存系统信息 */
    storage_status_t status = Save_System_Info(handle);
    if (status == STORAGE_OK) {
        status = Save_Remote_Index(handle);
    }
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_AddRemote(storage_handle_t* handle, const char* name, 
                                   uint16_t* remote_id)
{
    if (handle == NULL || name == NULL || remote_id == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 检查是否已达到最大遥控器数量 */
    if (handle->sys_info.remote_count >= STORAGE_MAX_REMOTES) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_FULL;
    }
    
    /* 查找空闲槽位 */
    int16_t slot = Find_Free_Remote_Slot(handle);
    if (slot < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_FULL;
    }
    
    /* 分配遥控器ID（使用槽位索引+1作为ID，0保留为无效） */
    uint16_t new_id = (uint16_t)(slot + 1);
    
    /* 初始化遥控器索引 */
    remote_index_t* index = &handle->index_table[slot];
    memset(index, 0, REMOTE_INDEX_SIZE);
    index->id = new_id;
    index->key_count = 0;
    strncpy(index->name, name, STORAGE_REMOTE_NAME_LEN - 1);
    index->name[STORAGE_REMOTE_NAME_LEN - 1] = '\0';
    
    /* 计算数据区域偏移（每个遥控器占用40个扇区） */
    index->data_offset = slot * (STORAGE_MAX_KEYS_PER_REMOTE * 2);
    
    /* 更新系统信息 */
    handle->sys_info.remote_count++;
    
    /* 保存数据 */
    storage_status_t status = Save_System_Info(handle);
    if (status == STORAGE_OK) {
        status = Save_Remote_Index(handle);
    }
    
    if (status == STORAGE_OK) {
        *remote_id = new_id;
    }
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_DeleteRemote(storage_handle_t* handle, uint16_t remote_id)
{
    if (handle == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 查找遥控器 */
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    /* 擦除数据区域 */
    remote_index_t* index = &handle->index_table[idx];
    uint32_t key_sectors_per_remote = STORAGE_MAX_KEYS_PER_REMOTE * 2;
    uint32_t base_sector_a = STORAGE_DATA_A_SECTOR + index->data_offset;
    uint32_t base_sector_b = STORAGE_DATA_B_SECTOR + index->data_offset;
    
    for (uint32_t i = 0; i < key_sectors_per_remote; i++) {
        Norflash_Erase_Sector(base_sector_a + i);
        Norflash_Erase_Sector(base_sector_b + i);
    }
    
    /* 更新系统信息 */
    handle->sys_info.remote_count -= index->key_count;
    handle->sys_info.total_keys -= index->key_count;
    
    /* 清空索引 */
    memset(index, 0xFF, REMOTE_INDEX_SIZE);
    
    /* 保存数据 */
    storage_status_t status = Save_System_Info(handle);
    if (status == STORAGE_OK) {
        status = Save_Remote_Index(handle);
    }
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_GetRemoteList(storage_handle_t* handle, remote_info_t* list,
                                       uint16_t max_count, uint16_t* count)
{
    if (handle == NULL || list == NULL || count == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    *count = 0;
    for (int16_t i = 0; i < STORAGE_MAX_REMOTES && *count < max_count; i++) {
        if (handle->index_table[i].key_count > 0) {
            list[*count].id = handle->index_table[i].id;
            list[*count].key_count = handle->index_table[i].key_count;
            strncpy(list[*count].name, handle->index_table[i].name, STORAGE_REMOTE_NAME_LEN);
            (*count)++;
        }
    }
    
    xSemaphoreGive(handle->mutex);
    return STORAGE_OK;
}

storage_status_t Storage_GetRemoteInfo(storage_handle_t* handle, uint16_t remote_id,
                                       remote_info_t* info)
{
    if (handle == NULL || info == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    info->id = handle->index_table[idx].id;
    info->key_count = handle->index_table[idx].key_count;
    strncpy(info->name, handle->index_table[idx].name, STORAGE_REMOTE_NAME_LEN);
    
    xSemaphoreGive(handle->mutex);
    return STORAGE_OK;
}

storage_status_t Storage_AddKey(storage_handle_t* handle, uint16_t remote_id,
                                const char* name, const uint32_t* data, uint32_t len,
                                uint16_t* key_id)
{
    if (handle == NULL || name == NULL || data == NULL || key_id == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (len == 0 || len > STORAGE_MAX_KEY_DATA_LEN) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 查找遥控器 */
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    remote_index_t* remote = &handle->index_table[idx];
    
    /* 检查按键数量是否已满 */
    if (remote->key_count >= STORAGE_MAX_KEYS_PER_REMOTE) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_FULL;
    }
    
    /* 查找空闲按键ID */
    uint16_t new_key_id = 0;
    for (uint16_t i = 0; i < STORAGE_MAX_KEYS_PER_REMOTE; i++) {
        key_data_t temp_key;
        storage_status_t status = Read_Key_Data(STORAGE_DATA_A_SECTOR, 
                                                remote->data_offset, i, &temp_key);
        if (status != STORAGE_OK || temp_key.id == 0xFFFF) {
            new_key_id = i;
            break;
        }
    }
    
    /* 准备按键数据 */
    key_data_t key_data;
    memset(&key_data, 0, KEY_DATA_SIZE);
    key_data.id = new_key_id;
    strncpy(key_data.name, name, STORAGE_KEY_NAME_LEN - 1);
    key_data.name[STORAGE_KEY_NAME_LEN - 1] = '\0';
    key_data.data_len = len;
    memcpy(key_data.data, data, len * sizeof(uint32_t));
    key_data.checksum = Calculate_Struct_Checksum(&key_data, KEY_DATA_SIZE,
                                                   offsetof(key_data_t, checksum));
    
    /* 写入按键数据 */
    storage_status_t status = Write_Key_Data(STORAGE_DATA_A_SECTOR, 
                                             remote->data_offset, 
                                             new_key_id, &key_data);
    
    if (status == STORAGE_OK) {
        /* 更新遥控器索引 */
        remote->key_count++;
        
        /* 更新系统信息 */
        handle->sys_info.total_keys++;
        
        /* 保存索引和系统信息 */
        status = Save_System_Info(handle);
        if (status == STORAGE_OK) {
            status = Save_Remote_Index(handle);
        }
        
        if (status == STORAGE_OK) {
            *key_id = new_key_id;
        }
    }
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_DeleteKey(storage_handle_t* handle, uint16_t remote_id,
                                   uint16_t key_id)
{
    if (handle == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 查找遥控器 */
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    remote_index_t* remote = &handle->index_table[idx];
    
    /* 检查按键是否存在 */
    key_data_t temp_key;
    storage_status_t status = Read_Key_Data(STORAGE_DATA_A_SECTOR, 
                                            remote->data_offset, key_id, &temp_key);
    if (status != STORAGE_OK || temp_key.id == 0xFFFF) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    /* 擦除按键数据区域 */
    uint32_t key_sectors_per_remote = STORAGE_MAX_KEYS_PER_REMOTE * 2;
    uint32_t base_sector_a = STORAGE_DATA_A_SECTOR + remote->data_offset;
    uint32_t base_sector_b = STORAGE_DATA_B_SECTOR + remote->data_offset;
    uint32_t key_sector_a = base_sector_a + key_id * 2;
    uint32_t key_sector_b = base_sector_b + key_id * 2;
    
    Norflash_Erase_Sector(key_sector_a);
    Norflash_Erase_Sector(key_sector_a + 1);
    Norflash_Erase_Sector(key_sector_b);
    Norflash_Erase_Sector(key_sector_b + 1);
    
    /* 更新遥控器索引 */
    remote->key_count--;
    
    /* 更新系统信息 */
    handle->sys_info.total_keys--;
    
    /* 保存索引和系统信息 */
    status = Save_System_Info(handle);
    if (status == STORAGE_OK) {
        status = Save_Remote_Index(handle);
    }
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_GetKeyData(storage_handle_t* handle, uint16_t remote_id,
                                    uint16_t key_id, key_data_t* key_data)
{
    if (handle == NULL || key_data == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 查找遥控器 */
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    remote_index_t* remote = &handle->index_table[idx];
    
    /* 读取按键数据 */
    storage_status_t status = Read_Key_Data(STORAGE_DATA_A_SECTOR, 
                                            remote->data_offset, key_id, key_data);
    
    if (status == STORAGE_OK && key_data->id == 0xFFFF) {
        status = STORAGE_ERROR_NOT_FOUND;
    }
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_UpdateKey(storage_handle_t* handle, uint16_t remote_id,
                                   uint16_t key_id, const uint32_t* data, uint32_t len)
{
    if (handle == NULL || data == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (len == 0 || len > STORAGE_MAX_KEY_DATA_LEN) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 查找遥控器 */
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    remote_index_t* remote = &handle->index_table[idx];
    
    /* 读取现有按键数据 */
    key_data_t key_data;
    storage_status_t status = Read_Key_Data(STORAGE_DATA_A_SECTOR, 
                                            remote->data_offset, key_id, &key_data);
    
    if (status != STORAGE_OK || key_data.id == 0xFFFF) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    /* 更新按键数据 */
    key_data.data_len = len;
    memcpy(key_data.data, data, len * sizeof(uint32_t));
    key_data.checksum = Calculate_Struct_Checksum(&key_data, KEY_DATA_SIZE,
                                                   offsetof(key_data_t, checksum));
    
    /* 写入更新后的数据 */
    status = Write_Key_Data(STORAGE_DATA_A_SECTOR, remote->data_offset, key_id, &key_data);
    
    xSemaphoreGive(handle->mutex);
    return status;
}

storage_status_t Storage_GetKeyList(storage_handle_t* handle, uint16_t remote_id,
                                    key_info_t* list, uint16_t max_count, uint16_t* count)
{
    if (handle == NULL || list == NULL || count == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    /* 查找遥控器 */
    int16_t idx = Find_Remote_Index(handle, remote_id);
    if (idx < 0) {
        xSemaphoreGive(handle->mutex);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    remote_index_t* remote = &handle->index_table[idx];
    *count = 0;
    
    /* 遍历所有可能的按键ID */
    for (uint16_t i = 0; i < STORAGE_MAX_KEYS_PER_REMOTE && *count < max_count; i++) {
        key_data_t key_data;
        storage_status_t status = Read_Key_Data(STORAGE_DATA_A_SECTOR, 
                                                remote->data_offset, i, &key_data);
        
        if (status == STORAGE_OK && key_data.id != 0xFFFF) {
            list[*count].id = key_data.id;
            list[*count].data_len = key_data.data_len;
            strncpy(list[*count].name, key_data.name, STORAGE_KEY_NAME_LEN);
            (*count)++;
        }
    }
    
    xSemaphoreGive(handle->mutex);
    return STORAGE_OK;
}

QueueHandle_t Storage_GetMsgQueue(storage_handle_t* handle)
{
    if (handle == NULL) {
        return NULL;
    }
    return handle->msg_queue;
}

storage_status_t Storage_SendMsg(storage_handle_t* handle, const storage_msg_t* msg)
{
    if (handle == NULL || msg == NULL) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    if (xQueueSend(handle->msg_queue, msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        return STORAGE_ERROR_BUSY;
    }
    
    return STORAGE_OK;
}
