#include "ota_crypto.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"

static const char *TAG = "ota_crypto";

// =============================================================================
// PUBLIC KEY CỦA DEVELOPER (ĐƯỢC NHÚNG CỐ ĐỊNH TRONG CHIP FLASH)
// Khóa riêng tư (Private Key) được lưu offline trên máy tính cá nhân của Developer.
// Dù hacker chiếm được toàn bộ quyền truy cập Database / Supabase,
// hacker cũng KHÔNG THỂ giả mạo chữ ký này!
// =============================================================================
static const char s_developer_public_key_pem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE8Y4BJy83GMWNW7jm4Yb+HZpkAgsj\n"
    "80o6KxZ+ZCUFxn1sGyKDrk/L7dRMJoQXDrMemo+GukMvNaDrRxXrF5k+NQ==\n"
    "-----END PUBLIC KEY-----\n";

esp_err_t ota_crypto_verify_signature(const uint8_t *sha256_hash, const char *sig_hex)
{
    if (sha256_hash == NULL || sig_hex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t sig_hex_len = strlen(sig_hex);
    if (sig_hex_len < 64 || (sig_hex_len % 2) != 0 || sig_hex_len > 256) {
        ESP_LOGE(TAG, "[BẢO MẬT] Độ dài chữ ký số không hợp lệ: %zu ký tự", sig_hex_len);
        return ESP_ERR_INVALID_ARG;
    }

    size_t sig_len = sig_hex_len / 2;
    uint8_t sig_bytes[128];
    for (size_t i = 0; i < sig_len; i++) {
        unsigned int val = 0;
        if (sscanf(sig_hex + i * 2, "%02x", &val) != 1) {
            ESP_LOGE(TAG, "[BẢO MẬT] Ký tự chữ ký số chứa giá trị không phải hex");
            return ESP_ERR_INVALID_ARG;
        }
        sig_bytes[i] = (uint8_t)val;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(&pk,
                                         (const unsigned char *)s_developer_public_key_pem,
                                         strlen(s_developer_public_key_pem) + 1);
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "Lỗi đọc Developer Public Key: %s (-0x%04x)", err_buf, -ret);
        mbedtls_pk_free(&pk);
        return ESP_FAIL;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, sha256_hash, 32, sig_bytes, sig_len);
    mbedtls_pk_free(&pk);

    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        ESP_LOGE(TAG, "[CẢNH BÁO AN NINH CỰC ĐỘ] XÁC THỰC CHỮ KÝ SỐ THẤT BẠI (-0x%04x)!", -ret);
        ESP_LOGE(TAG, "=> Firmware KHÔNG ĐƯỢC KÝ bởi Private Key hợp lệ của Developer!");
        ESP_LOGE(TAG, "=> CẢNH BÁO: Database hoặc Server có thể đã bị chiếm quyền/sửa đổi!");
        ESP_LOGE(TAG, "=> HỦY BỎ NẠP NGAY LẬP TỨC ĐỂ BẢO VỆ THIẾT BỊ.");
        ESP_LOGE(TAG, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        return ESP_ERR_IMAGE_INVALID;
    }

    ESP_LOGI(TAG, "[BẢO MẬT THÀNH CÔNG] Chữ ký số ECDSA P-256 hoàn toàn hợp lệ! Xác thực đúng nguồn gốc Developer.");
    return ESP_OK;
}
