#ifndef __CRC32_H
#define __CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CRC32校验模块
 * @note 使用标准CRC32多项式 0xEDB88320
 */

/**
 * @brief 初始化CRC32计算
 * @return 初始CRC值
 */
uint32_t CRC32_Init(void);

/**
 * @brief 计算CRC32校验值
 * @param data 数据指针
 * @param length 数据长度（字节数）
 * @param crc 初始CRC值（第一次调用时使用CRC32_Init()的返回值）
 * @return 计算后的CRC值
 */
uint32_t CRC32_Calculate(const void* data, size_t length, uint32_t crc);

/**
 * @brief 计算数据的CRC32校验值（便捷函数）
 * @param data 数据指针
 * @param length 数据长度（字节数）
 * @return CRC32校验值
 */
uint32_t CRC32_Compute(const void* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* __CRC32_H */
