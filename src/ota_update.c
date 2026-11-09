#include "ota_update.h"
#include "ota_config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "ota_http.h"

#include "ota_crypto.h"

static const char *TAG = "ota_update";
#define NVS_NAMESPACE "storage"
#define NVS_KEY_VERSION "fw_version"
#define VERSION_MAX_LEN 32
#define SHA256_HEX_LEN 64
#define SIG_MAX_LEN 256

static char s_current_version[VERSION_MAX_LEN] = FW_VERSION_STR;

static esp_err_t nvs_read_version(char *out, size_t out_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        strncpy(out, FW_VERSION_STR, out_len - 1);
        out[out_len - 1] = '\0';
        return ESP_OK;
    }
    size_t len = out_len;
    err = nvs_get_str(h, NVS_KEY_VERSION, out, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(out, FW_VERSION_STR, out_len - 1);
        out[out_len - 1] = '\0';
        return ESP_OK;
    }
    return err;
}

static esp_err_t nvs_write_version(const char *version)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open readwrite failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_VERSION, version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void ota_update_init_version(void)
{
    // Cập nhật version theo bản firmware đang chạy
    nvs_write_version(FW_VERSION_STR);
    strncpy(s_current_version, FW_VERSION_STR, sizeof(s_current_version) - 1);
    s_current_version[sizeof(s_current_version) - 1] = '\0';
}

const char *ota_update_get_version(void)
{
    return s_current_version;
}

static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out_hex)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(out_hex + i * 2, "%02x", bytes[i]);
    }
    out_hex[len * 2] = '\0';
}

esp_err_t ota_update_check_and_apply(void)
{
    char current_version[VERSION_MAX_LEN] = { 0 };
    nvs_read_version(current_version, sizeof(current_version));
    strncpy(s_current_version, current_version, sizeof(s_current_version) - 1);

    const char *manifest_url = OTA_MANIFEST_URL;
    printf("\n[OTA CHECK] Đang kiểm tra phiên bản từ Cloud:\n  URL: %s\n", manifest_url);

    /* Chờ đồng bộ thời gian Internet (SNTP) để đảm bảo xác thực thời hạn chứng chỉ SSL chính xác */
    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2020 - 1900)) {
        printf("[SNTP] Đang đợi đồng bộ thời gian thực từ Internet để xác thực chứng chỉ SSL...\n");
        int retry = 0;
        while (timeinfo.tm_year < (2020 - 1900) && ++retry <= 10) {
            vTaskDelay(pdMS_TO_TICKS(500));
            time(&now);
            localtime_r(&now, &timeinfo);
        }
        if (timeinfo.tm_year >= (2020 - 1900)) {
            printf("[SNTP] Đồng bộ giờ thành công: %04d-%02d-%02d %02d:%02d:%02d\n",
                   timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
    }

    char *resp_buf = NULL;
    esp_err_t err = http_get_text(manifest_url, &resp_buf, NULL);
    if (err != ESP_OK) {
        printf("[OTA LỖI] Không thể tải thông tin cập nhật từ Cloud: %s\n", esp_err_to_name(err));
        return err;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (root == NULL) {
        printf("[OTA LỖI] Phản hồi từ Cloud không phải định dạng JSON hợp lệ!\n");
        return ESP_FAIL;
    }

    // Hỗ trợ cả 2 chuẩn trường "version" hoặc "latest_version"
    cJSON *j_version = cJSON_GetObjectItem(root, "version");
    if (!j_version) {
        j_version = cJSON_GetObjectItem(root, "latest_version");
    }
    cJSON *j_url = cJSON_GetObjectItem(root, "url");
    cJSON *j_sha = cJSON_GetObjectItem(root, "sha256");
    cJSON *j_sig = cJSON_GetObjectItem(root, "signature");

    if (!cJSON_IsString(j_version) || !cJSON_IsString(j_url)) {
        printf("[OTA LỖI] JSON từ Cloud thiếu trường 'version' hoặc 'url'!\n");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // [BẢO MẬT: BẮT BUỘC CHỮ KÝ SỐ ĐỂ CHỐNG HACK DATABASE/STORAGE]
    if (!cJSON_IsString(j_sig) || strlen(j_sig->valuestring) < 64) {
        printf("\n====================================================================\n");
        printf("[BẢO MẬT TỪ CHỐI] version.json THIẾU CHỮ KÝ SỐ ('signature')!\n");
        printf("=> CẢNH BÁO: Database hoặc Storage có thể đã bị chiếm quyền/giả mạo.\n");
        printf("=> ESP32 TỪ CHỐI CẬP NHẬT để bảo vệ tuyệt đối an toàn cho thiết bị.\n");
        printf("====================================================================\n\n");
        cJSON_Delete(root);
        return ESP_ERR_IMAGE_INVALID;
    }

    // [BẢO MẬT: BẮT BUỘC MÃ SHA-256 ĐỦ 64 KÝ TỰ]
    if (!cJSON_IsString(j_sha) || strlen(j_sha->valuestring) != SHA256_HEX_LEN) {
        printf("[BẢO MẬT TỪ CHỐI] version.json thiếu mã SHA-256 (64 hex characters) hợp lệ!\n");
        cJSON_Delete(root);
        return ESP_ERR_IMAGE_INVALID;
    }

    char latest_version[VERSION_MAX_LEN];
    strncpy(latest_version, j_version->valuestring, sizeof(latest_version) - 1);
    latest_version[sizeof(latest_version) - 1] = '\0';

    char download_url[512];
    strncpy(download_url, j_url->valuestring, sizeof(download_url) - 1);
    download_url[sizeof(download_url) - 1] = '\0';

    char expected_sha[SHA256_HEX_LEN + 1];
    strncpy(expected_sha, j_sha->valuestring, sizeof(expected_sha) - 1);
    expected_sha[sizeof(expected_sha) - 1] = '\0';

    char expected_sig[SIG_MAX_LEN + 1];
    strncpy(expected_sig, j_sig->valuestring, sizeof(expected_sig) - 1);
    expected_sig[sizeof(expected_sig) - 1] = '\0';

    // Kiểm tra xem đã là phiên bản mới nhất chưa
    cJSON *j_update = cJSON_GetObjectItem(root, "update_available");
    if ((j_update && cJSON_IsBool(j_update) && !j_update->valueint) ||
        (strcasecmp(latest_version, current_version) == 0)) {
        printf("\n==================================================\n");
        printf("[OTA THÔNG BÁO] Thiết bị đã ở phiên bản mới nhất!\n");
        printf("  + Phiên bản hiện tại : %s\n", current_version);
        printf("  + Phiên bản trên Cloud: %s\n", latest_version);
        printf("  + Trạng thái         : Không cần cập nhật.\n");
        printf("==================================================\n\n");
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON_Delete(root);

    printf("\n==================================================\n");
    printf("[OTA PHÁT HIỆN BẢN MỚI] Sẵn sàng cập nhật từ Cloud!\n");
    printf("  + Phiên bản mới : %s (Hiện tại: %s)\n", latest_version, current_version);
    printf("  + URL Tải về    : %s\n", download_url);
    printf("  + SHA256 kỳ vọng: %s\n", expected_sha);
    printf("  + Chữ ký số     : %.16s... (Đã ký bằng ECDSA P-256)\n", expected_sig);
    printf("==================================================\n");

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        printf("[OTA LỖI] Không tìm thấy phân vùng OTA tiếp theo trên Flash!\n");
        return ESP_FAIL;
    }
    printf("[OTA] Chuẩn bị ghi vào phân vùng: '%s' (Kích thước: %lu KB)...\n",
           update_partition->label, update_partition->size / 1024);

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        printf("[OTA LỖI] esp_ota_begin thất bại: %s\n", esp_err_to_name(err));
        return err;
    }

    uint8_t stream_sha[32];
    int downloaded_size = 0;
    err = ota_http_download_to_partition(download_url, ota_handle, &downloaded_size, stream_sha);
    if (err != ESP_OK) {
        printf("[OTA LỖI] Tải firmware thất bại: %s\n", esp_err_to_name(err));
        esp_ota_abort(ota_handle);
        return err;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        printf("[OTA LỖI] esp_ota_end kết thúc ghi thất bại: %s\n", esp_err_to_name(err));
        return err;
    }

    // [BẢO MẬT TẦNG 1]: Xác thực mã băm SHA-256 (Hỗ trợ cả SHA-256 toàn bộ file và Image Digest)
    printf("[OTA BẢO MẬT 1/2] Đang xác thực mã băm SHA-256...\n");
    uint8_t part_sha[32];
    esp_err_t part_err = esp_partition_get_sha256(update_partition, part_sha);

    char stream_sha_hex[SHA256_HEX_LEN + 1];
    bytes_to_hex(stream_sha, 32, stream_sha_hex);

    char part_sha_hex[SHA256_HEX_LEN + 1] = { 0 };
    if (part_err == ESP_OK) {
        bytes_to_hex(part_sha, 32, part_sha_hex);
    }

    printf("  + SHA-256 toàn bộ file (chuẩn sha256sum) : %s\n", stream_sha_hex);
    if (part_err == ESP_OK) {
        printf("  + SHA-256 Flash Image (ESP-IDF Digest)   : %s\n", part_sha_hex);
    }
    printf("  + SHA-256 kỳ vọng từ Cloud               : %s\n", expected_sha);

    const uint8_t *matched_sha = NULL;
    if (strcasecmp(stream_sha_hex, expected_sha) == 0) {
        matched_sha = stream_sha;
        printf("[BẢO MẬT] >> Khớp chính xác với SHA-256 toàn bộ file (chuẩn sha256sum)!\n");
    } else if (part_err == ESP_OK && strcasecmp(part_sha_hex, expected_sha) == 0) {
        matched_sha = part_sha;
        printf("[BẢO MẬT] >> Khớp chính xác với SHA-256 Flash Image Digest!\n");
    } else {
        printf("\n====================================================================\n");
        printf("[BẢO MẬT NGUY HIỂM] Mã SHA-256 KHÔNG KHỚP với bất kỳ chuẩn nào!\n");
        printf("=> Có thể file .bin trên Supabase khác phiên bản với file version.json.\n");
        printf("=> Hủy bỏ cập nhật để bảo vệ an toàn cho thiết bị.\n");
        printf("====================================================================\n\n");
        return ESP_ERR_INVALID_CRC;
    }

    // [BẢO MẬT TẦNG 2]: Xác thực Chữ ký số mã hóa ECDSA P-256 của Developer
    printf("[OTA BẢO MẬT 2/2] Đang xác minh Chữ ký số mã hóa ECDSA P-256 từ Public Key...\n");
    err = ota_crypto_verify_signature(matched_sha, expected_sig);
    if (err != ESP_OK && matched_sha != part_sha && part_err == ESP_OK) {
        err = ota_crypto_verify_signature(part_sha, expected_sig);
    }
    if (err != ESP_OK) {
        printf("\n====================================================================\n");
        printf("[BẢO MẬT TỪ CHỐI] CHỮ KÝ SỐ KHÔNG HỢP LỆ! PHÁT HIỆN GIAN LẬN FIRMWARE!\n");
        printf("=> Kẻ tấn công có thể đã hack Database/Storage nhưng KHÔNG CÓ Private Key.\n");
        printf("=> ESP32 TỪ CHỐI CHUYỂN BOOT PARTITION. Thiết bị hoàn toàn an toàn!\n");
        printf("====================================================================\n\n");
        return ESP_ERR_IMAGE_INVALID;
    }
    printf("[BẢO MẬT XÁC NHẬN] Chữ ký số Developer hoàn toàn chính xác và tin cậy!\n");

    // Ghi phiên bản mới vào NVS
    nvs_write_version(latest_version);

    // Kích hoạt phân vùng khởi động mới
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        printf("[OTA LỖI] Chuyển đổi boot partition thất bại: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("\n**************************************************\n");
    printf("[OTA THÀNH CÔNG] ĐÃ CẬP NHẬT LÊN PHIÊN BẢN: %s!\n", latest_version);
    printf("Phân vùng khởi động mới: '%s'\n", update_partition->label);
    printf("Thiết bị sẽ tự động khởi động lại sau 3 giây...\n");
    printf("**************************************************\n\n");

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}
