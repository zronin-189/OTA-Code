#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_ota_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gửi HTTP GET lấy toàn bộ nội dung text (chuỗi JSON check version).
 */
esp_err_t http_get_text(const char *url, char **out_buf, int *out_len);

/**
 * @brief Tải trực tiếp file firmware binary qua HTTP/HTTPS và ghi từng block vào OTA partition.
 * @param out_stream_sha256 Tùy chọn (NULL nếu không dùng): Buffer 32 bytes nhận mã SHA-256 của toàn bộ file binary tải về (chuẩn sha256sum).
 */
esp_err_t ota_http_download_to_partition(const char *url, esp_ota_handle_t ota_handle, int *out_size, uint8_t *out_stream_sha256);


#ifdef __cplusplus
}
#endif
