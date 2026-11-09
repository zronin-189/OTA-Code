#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FW_VERSION_STR
#define FW_VERSION_STR "1.0.0"
#endif

/**
 * @brief Khoi tao phien ban trong NVS flash.
 */
void ota_update_init_version(void);

/**
 * @brief Lay phien ban firmware hien tai.
 */
const char *ota_update_get_version(void);

/**
 * @brief Kiem tra ban cap nhat tu server va tien hanh cap nhat neu co.
 */
esp_err_t ota_update_check_and_apply(void);

#ifdef __cplusplus
}
#endif
