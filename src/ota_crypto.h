#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_image_format.h"

#ifndef ESP_ERR_IMAGE_INVALID
#define ESP_ERR_IMAGE_INVALID (0x2002)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Xác minh chữ ký số ECDSA P-256 (SHA-256) của firmware.
 * @param sha256_hash Mảng 32 bytes SHA-256 thực tế của firmware đọc từ Flash.
 * @param sig_hex Chuỗi hex đại diện cho chữ ký số DER (từ version.json).
 * @return ESP_OK nếu chữ ký số hợp lệ và được ký bởi Developer có Private Key.
 *         ESP_ERR_IMAGE_INVALID nếu chữ ký sai, giả mạo, hoặc database bị hack.
 */
esp_err_t ota_crypto_verify_signature(const uint8_t *sha256_hash, const char *sig_hex);

#ifdef __cplusplus
}
#endif
