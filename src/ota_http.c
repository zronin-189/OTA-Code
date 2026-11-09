#include "ota_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "mbedtls/md.h"

static const char *TAG = "ota_http";

/* Chứng chỉ Root CA của Google Trust Services (GTS Root R4) và Let's Encrypt (ISRG Root X1)
 * Supabase Storage / Cloudflare sử dụng 2 Root CA này (Hiệu lực tới năm 2036) */
static const char s_cloud_root_cas_pem[] =
    /* 1. Google Trust Services GTS Root R4 (Hạn tới 2036 - Supabase Cloudflare) */
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
    "AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
    "QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
    "HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
    "BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
    "9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
    "p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
    "-----END CERTIFICATE-----\n"
    /* 2. Let's Encrypt ISRG Root X1 (Hạn tới 2035) */
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

typedef struct {
    char *buf;
    int len;
    int capacity;
} http_rx_buffer_t;

static esp_err_t http_rx_event_handler(esp_http_client_event_t *evt)
{
    http_rx_buffer_t *hb = (http_rx_buffer_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "[EVENT] HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "[EVENT] HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG, "[EVENT] HTTP_EVENT_HEADERS_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "[EVENT] Header: %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "[EVENT] Data received: %d bytes", evt->data_len);
            if (hb && evt->data_len > 0) {
                if (hb->len + evt->data_len + 1 > hb->capacity) {
                    int new_cap = (hb->capacity == 0) ? 4096 : (hb->capacity * 2 + evt->data_len + 128);
                    char *tmp = (char *)realloc(hb->buf, new_cap);
                    if (!tmp) {
                        ESP_LOGE(TAG, "realloc failed in event handler");
                        return ESP_FAIL;
                    }
                    hb->buf = tmp;
                    hb->capacity = new_cap;
                }
                memcpy(hb->buf + hb->len, evt->data, evt->data_len);
                hb->len += evt->data_len;
                hb->buf[hb->len] = '\0';
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "[EVENT] HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[EVENT] HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t http_get_text(const char *url, char **out_buf, int *out_len)
{
    if (url == NULL || out_buf == NULL) {
        ESP_LOGE(TAG, "invalid args");
        return ESP_ERR_INVALID_ARG;
    }
    *out_buf = NULL;

    http_rx_buffer_t hb = { .buf = NULL, .len = 0, .capacity = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_rx_event_handler,
        .user_data = &hb,
        .timeout_ms = 20000,
        .buffer_size = 8192,
        .buffer_size_tx = 2048,
        .max_redirection_count = 5,
        .keep_alive_enable = false,
        .cert_pem = s_cloud_root_cas_pem,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "User-Agent", "ESP32-OTA/1.0");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "esp_http_client_perform ket qua: err=%s, status=%d, nhan=%d bytes",
             esp_err_to_name(err), status, hb.len);

    int esp_tls_code = 0, esp_tls_flags = 0;
    if (esp_http_client_get_and_clear_last_tls_error(client, &esp_tls_code, &esp_tls_flags) == ESP_OK) {
        if (esp_tls_code != 0) {
            ESP_LOGW(TAG, "Chi tiet TLS Error: mbedtls_code=-0x%x, flags=0x%x",
                     -esp_tls_code, esp_tls_flags);
        }
    }

    if (err == ESP_OK && status == 200 && hb.buf != NULL) {
        *out_buf = hb.buf;
        if (out_len) {
            *out_len = hb.len;
        }
        esp_http_client_cleanup(client);
        return ESP_OK;
    }

    if (hb.buf) {
        free(hb.buf);
    }
    esp_http_client_cleanup(client);
    return (err != ESP_OK) ? err : ESP_FAIL;
}

#include "esp_app_format.h"

esp_err_t ota_http_download_to_partition(const char *url, esp_ota_handle_t ota_handle, int *out_size, uint8_t *out_stream_sha256)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // [BẢO MẬT 1: Bắt buộc HTTPS - Chống Man-in-the-Middle và nghe lén/sửa đổi gói tin]
    if (strncasecmp(url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "[BẢO MẬT TỪ CHỐI] URL không dùng giao thức HTTPS mã hóa an toàn: %s", url);
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = 8192,
        .buffer_size_tx = 2048,
        .max_redirection_count = 5,
        .keep_alive_enable = false,
        .cert_pem = s_cloud_root_cas_pem,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "User-Agent", "ESP32-OTA/1.0");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "http status %d for binary url %s", status, url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // [BẢO MẬT 2: Giới hạn kích thước file chống tràn bộ nhớ hoặc file rỗng / mã độc]
    if (content_len > 0) {
        if (content_len < 65536) { // File firmware chuẩn tối thiểu phải > 64KB
            ESP_LOGE(TAG, "[BẢO MẬT TỪ CHỐI] Dung lượng file quá nhỏ (%d bytes), nghi vấn file lỗi hoặc giả mạo!", content_len);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_ERR_IMAGE_INVALID;
        }
        if (content_len > 4194304) { // Giới hạn phân vùng OTA 4MB (4,194,304 bytes)
            ESP_LOGE(TAG, "[BẢO MẬT TỪ CHỐI] Dung lượng file (%d bytes) vượt quá kích thước phân vùng OTA (4MB)!", content_len);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_ERR_IMAGE_INVALID;
        }
    }
    ESP_LOGI(TAG, "Bắt đầu tải binary hợp lệ, tổng dung lượng: %d bytes", content_len);

    const int rx_buf_size = 4096;
    char *buf = (char *)malloc(rx_buf_size);
    if (buf == NULL) {
        ESP_LOGE(TAG, "malloc rx buffer failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    mbedtls_md_context_t sha_ctx;
    bool calc_sha = (out_stream_sha256 != NULL);
    if (calc_sha) {
        mbedtls_md_init(&sha_ctx);
        mbedtls_md_setup(&sha_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&sha_ctx);
    }

    int total = 0;
    int data_read = 0;
    int retry_count = 0;
    const int max_retries = 10;
    int last_reported_percent = -1;

    while (1) {
        data_read = esp_http_client_read(client, buf, rx_buf_size);
        if (data_read < 0) {
            if (data_read == -ESP_ERR_HTTP_EAGAIN && retry_count < max_retries) {
                retry_count++;
                ESP_LOGW(TAG, "socket EAGAIN, retry (%d/%d)...", retry_count, max_retries);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGE(TAG, "http read error: code=%d, da tai=%d/%d bytes",
                     data_read, total, content_len);
            free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (data_read == 0) {
            break;
        }

        retry_count = 0;

        // [BẢO MẬT 3: Xác thực Magic Byte (0xE9) & Chip ID ESP32-S3 ngay ở khối dữ liệu đầu tiên]
        if (total == 0) {
            if (data_read < sizeof(esp_image_header_t)) {
                ESP_LOGE(TAG, "[BẢO MẬT TỪ CHỐI] Khối dữ liệu đầu tiên quá ngắn để chứa header ESP32!");
                free(buf);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_IMAGE_INVALID;
            }

            esp_image_header_t *img_hdr = (esp_image_header_t *)buf;
            if (img_hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "[BẢO MẬT TỪ CHỐI] Magic Byte = 0x%02X KHÔNG HỢP LỆ (Kỳ vọng 0xE9)! Dừng nạp ngay.",
                         img_hdr->magic);
                free(buf);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_IMAGE_INVALID;
            }

            if (img_hdr->chip_id != ESP_CHIP_ID_ESP32S3) {
                ESP_LOGE(TAG, "[BẢO MẬT TỪ CHỐI] Firmware dành cho Chip ID 0x%04X, KHÔNG PHẢI ESP32-S3 (0x0009)! Ngăn chặn brick chip.",
                         img_hdr->chip_id);
                free(buf);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_IMAGE_INVALID;
            }
            ESP_LOGI(TAG, "[BẢO MẬT XÁC THỰC] Khối header hợp lệ: Magic 0xE9, đúng chip ESP32-S3.");
        }

        err = esp_ota_write(ota_handle, buf, data_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s tai offset %d", esp_err_to_name(err), total);
            if (calc_sha) {
                mbedtls_md_free(&sha_ctx);
            }
            free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }

        if (calc_sha) {
            mbedtls_md_update(&sha_ctx, (const unsigned char *)buf, data_read);
        }

        total += data_read;

        if (content_len > 0) {
            int current_percent = (total * 100) / content_len;
            if (current_percent >= last_reported_percent + 10 || current_percent == 100) {
                last_reported_percent = current_percent;
                printf("[OTA TIEN TRINH] >> %d%% (Da ghi %d / %d bytes vao Flash)\n",
                       current_percent, total, content_len);
                fflush(stdout);
            }
        }
    }
    free(buf);

    if (calc_sha) {
        mbedtls_md_finish(&sha_ctx, out_stream_sha256);
        mbedtls_md_free(&sha_ctx);
    }

    if (content_len > 0 && total != content_len) {
        ESP_LOGE(TAG, "incomplete download: %d / %d bytes", total, content_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tai binary hoan tat 100%% (%d bytes)", total);
    if (out_size) {
        *out_size = total;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}
