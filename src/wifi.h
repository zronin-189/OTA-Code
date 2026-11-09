#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo Wi-Fi STA và đợi kết nối có IP.
 * @return ESP_OK nếu thành công, mã lỗi nếu thất bại.
 */
esp_err_t wifi_init_sta(void);

/**
 * @brief Hủy tài nguyên Wi-Fi để có thể init lại.
 */
void wifi_deinit_driver(void);

/**
 * @brief Kiểm tra Wi-Fi đang kết nối hay không.
 */
bool wifi_is_connected(void);

/**
 * @brief Lấy địa chỉ IP dạng chuỗi string ("x.x.x.x").
 */
const char *wifi_get_ip_str(void);

#ifdef __cplusplus
}
#endif
