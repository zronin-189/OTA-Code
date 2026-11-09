#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "ota_update.h"
#include "ota_config.h"
#include "esp_ota_ops.h"

#if defined(FW_FEATURE_BLE)
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#endif

#if defined(FW_FEATURE_WIFI_SCAN)
#include "esp_wifi.h"
#endif

#include "esp_sntp.h"

static const char *TAG = "app_main";

#ifndef FW_VERSION_STR
#define FW_VERSION_STR "1.0.0"
#endif

/* =============================================================================
 * 1. TÍNH NĂNG 1: BLUETOOTH BLE TEST (v1.0.0)
 * ============================================================================= */
#if defined(FW_FEATURE_BLE)
#define BLE_DEVICE_NAME         "ESP32S3_BLE_N16R8"
#define GATTS_SERVICE_UUID_TEST 0x00FF
#define GATTS_CHAR_UUID_TEST    0xFF01
#define GATTS_NUM_HANDLE_TEST   4

static uint8_t s_char_val[32] = "ESP32S3_BLE_READY";
static uint16_t s_char_handle = 0;

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20, // 20ms
    .adv_int_max        = 0x40, // 40ms
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0,
    .max_interval = 0,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = 0x00,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            printf("[BLE THÀNH CÔNG] Đang phát sóng BLE: '%s'\n", BLE_DEVICE_NAME);
            printf(">> Mở Bluetooth trên điện thoại (hoặc app nRF Connect / LightBlue) để kết nối!\n");
        } else {
            ESP_LOGE(TAG, "Bắt đầu phát sóng BLE thất bại: 0x%x", param->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        printf("[BLE BẢO MẬT] Nhận yêu cầu ghép đôi (Pairing) từ điện thoại, tự động chấp nhận (Just Works)...\n");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            printf("\n==================================================\n");
            printf("[BLE BẢO MẬT] >> Ghép đôi (Pairing & Bonding) THÀNH CÔNG!\n");
            printf("==================================================\n");
        } else {
            printf("[BLE BẢO MẬT] Ghép đôi không thành công, mã lý do: 0x%x\n", param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
        esp_ble_gap_config_adv_data(&adv_data);
        esp_ble_gap_config_adv_data(&scan_rsp_data);

        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = {
                .inst_id = 0,
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = GATTS_SERVICE_UUID_TEST }
                }
            }
        };
        esp_ble_gatts_create_service(gatts_if, &service_id, GATTS_NUM_HANDLE_TEST);
        break;
    case ESP_GATTS_CREATE_EVT: {
        esp_ble_gatts_start_service(param->create.service_handle);

        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = GATTS_CHAR_UUID_TEST }
        };
        esp_attr_value_t char_val = {
            .attr_max_len = sizeof(s_char_val),
            .attr_len = strlen((char *)s_char_val),
            .attr_value = s_char_val,
        };
        esp_ble_gatts_add_char(param->create.service_handle,
                               &char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                               &char_val,
                               NULL);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT:
        s_char_handle = param->add_char.attr_handle;
        ESP_LOGI(TAG, "Đã tạo GATT Characteristic (handle=%d)", s_char_handle);
        break;
    case ESP_GATTS_CONNECT_EVT: {
        printf("\n==================================================\n");
        printf("[BLE KẾT NỐI] Điện thoại ĐÃ KẾT NỐI THÀNH CÔNG vào ESP32-S3! (conn_id: %d)\n", param->connect.conn_id);
        printf("==================================================\n");

        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    // 40ms
        conn_params.min_int = 0x10;    // 20ms
        conn_params.timeout = 400;     // 4000ms
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    }
    case ESP_GATTS_READ_EVT: {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = strlen((char *)s_char_val);
        memcpy(rsp.attr_value.value, s_char_val, rsp.attr_value.len);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        printf("[BLE READ] Điện thoại đọc dữ liệu: '%s'\n", (char *)s_char_val);
        break;
    }
    case ESP_GATTS_WRITE_EVT: {
        if (param->write.len > 0) {
            int copy_len = param->write.len < (sizeof(s_char_val) - 1) ? param->write.len : (sizeof(s_char_val) - 1);
            memcpy(s_char_val, param->write.value, copy_len);
            s_char_val[copy_len] = '\0';
            printf("\n[BLE WRITE] Nhận dữ liệu từ điện thoại: '%s'\n", (char *)s_char_val);
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        printf("[BLE NGẮT KẾT NỐI] Điện thoại đã ngắt kết nối. Đang tiếp tục phát sóng BLE...\n");
        esp_ble_gap_start_advertising(&adv_params);
        break;
    default:
        break;
    }
}

static void ble_init_test(void)
{
    printf("\n==================================================\n");
    printf("[BLE TEST] Đang khởi tạo Bluetooth Low Energy...\n");
    printf("==================================================\n");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bluedroid_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bluedroid_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Thiết lập công suất phát sóng tối đa (+9dBm) để sóng khỏe, ổn định
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);

    // Cấu hình bảo mật BLE SMP (Chế độ Just Works - tự động chấp nhận ghép đôi từ điện thoại)
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t oob_support = ESP_BLE_OOB_DISABLE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(uint8_t));

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(0);
}
#endif

/* =============================================================================
 * 2. TÍNH NĂNG 2: WI-FI SCANNER TEST (v1.1.0)
 * ============================================================================= */
#if defined(FW_FEATURE_WIFI_SCAN)
static void wifi_scanner_task(void *pvParameters)
{
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        printf("\n==================================================\n");
        printf("[WIFI SCANNER v1.1.0] Đang quét các mạng Wi-Fi xung quanh (2.4 GHz)...\n");
        printf("==================================================\n");

        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time.active.min = 100,
            .scan_time.active.max = 300,
        };

        esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
        if (ret == ESP_OK) {
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            printf("[WIFI SCANNER] Tìm thấy %u mạng Wi-Fi xung quanh:\n", ap_count);
            if (ap_count > 0) {
                uint16_t max_aps = (ap_count > 15) ? 15 : ap_count;
                wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * max_aps);
                if (ap_records) {
                    if (esp_wifi_scan_get_ap_records(&max_aps, ap_records) == ESP_OK) {
                        for (int i = 0; i < max_aps; i++) {
                            const char *auth_mode = "OPEN";
                            switch (ap_records[i].authmode) {
                                case WIFI_AUTH_WEP: auth_mode = "WEP"; break;
                                case WIFI_AUTH_WPA_PSK: auth_mode = "WPA"; break;
                                case WIFI_AUTH_WPA2_PSK: auth_mode = "WPA2"; break;
                                case WIFI_AUTH_WPA_WPA2_PSK: auth_mode = "WPA/WPA2"; break;
                                case WIFI_AUTH_WPA3_PSK: auth_mode = "WPA3"; break;
                                case WIFI_AUTH_WPA2_WPA3_PSK: auth_mode = "WPA2/WPA3"; break;
                                default: break;
                            }
                            printf("  [%2d] SSID: %-22.22s | RSSI: %4d dBm | Kênh: %2d | %s\n",
                                   i + 1, (char *)ap_records[i].ssid, ap_records[i].rssi,
                                   ap_records[i].primary, auth_mode);
                        }
                    }
                    free(ap_records);
                }
            }
        } else {
            ESP_LOGW(TAG, "Quét Wi-Fi thất bại: %s", esp_err_to_name(ret));
        }
        printf(">> Gõ 'upd' trên terminal rồi bấm ENTER để cập nhật OTA <<\n");

        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
#endif

/* =============================================================================
 * 3. TÍNH NĂNG 3: RTC WEB SNTP TIME TEST (v1.2.0)
 * ============================================================================= */
#if defined(FW_FEATURE_RTC_WEB)
static void rtc_web_task(void *pvParameters)
{
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("\n==================================================\n");
    printf("[RTC WEB v1.2.0] Khởi tạo đồng bộ giờ Internet (SNTP / NTP)...\n");
    printf("==================================================\n");

    setenv("TZ", "ICT-7", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_init();

    printf("[RTC WEB] Đã cấu hình NTP: pool.ntp.org, time.google.com\n");

    int print_count = 0;
    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year < (2025 - 1900)) {
            printf("[RTC WEB] Đang đồng bộ thời gian từ Internet...\n");
        } else {
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%A, %d/%m/%Y %H:%M:%S", &timeinfo);
            printf("[RTC WEB CLOCK] %s (Múi giờ ICT UTC+7)\n", time_str);
        }

        print_count++;
        if (print_count % 3 == 0) {
            printf(">> Gõ 'upd' trên terminal rồi bấm ENTER để cập nhật OTA <<\n");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif

static bool s_ota_running = false;

static void ota_worker_task(void *pvParameters)
{
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_DEBUG);
    esp_log_level_set("ota_http", ESP_LOG_DEBUG);
    esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
    esp_log_level_set("transport_base", ESP_LOG_DEBUG);

    printf("\n[OTA TASK] >>> Bắt đầu kiểm tra và nạp firmware OTA...\n");
    printf("[OTA MEM] Free Heap: %lu bytes | Min Free Heap: %lu bytes\n",
           (unsigned long)esp_get_free_heap_size(),
           (unsigned long)esp_get_minimum_free_heap_size());

    esp_err_t err = ota_update_check_and_apply();
    if (err == ESP_OK) {
        printf("[OTA TASK] Hoàn tất quá trình OTA thành công!\n");
    } else {
        printf("[OTA TASK] Quá trình OTA kết thúc: %s\n", esp_err_to_name(err));
    }
    s_ota_running = false;
    vTaskDelete(NULL);
}

/* =============================================================================
 * 4. CONSOLE LẮNG NGHE LỆNH 'upd' TỪ NGƯỜI DÙNG
 * ============================================================================= */
static void console_task(void *pvParameters)
{
    char line_buf[64];
    int line_len = 0;

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    while (1) {
        int c = fgetc(stdin);
        if (c == EOF || c == 0xFF || c == 0x00) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Echo ký tự để người dùng nhìn thấy trên terminal
        if (c >= 32 && c <= 126) {
            putchar(c);
            fflush(stdout);
        }

        if (c == '\r' || c == '\n') {
            putchar('\n');
            line_buf[line_len] = '\0';

            char *p = line_buf;
            while (*p == ' ' || *p == '\t') p++;
            int len = strlen(p);
            while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n')) {
                p[--len] = '\0';
            }

            if (strcasecmp(p, "upd") == 0 || strcasecmp(p, "update") == 0) {
                if (s_ota_running) {
                    printf("\n[OTA CẢNH BÁO] Quá trình OTA đang diễn ra, vui lòng đợi...\n");
                } else if (!wifi_is_connected()) {
                    printf("\n[OTA LỖI] Wi-Fi chưa kết nối! Vui lòng chờ thiết bị kết nối Wi-Fi.\n");
                } else {
                    printf("\n[LỆNH NHẬN ĐƯỢC: upd] >>> Khởi tạo tác vụ OTA riêng biệt (Stack: 24KB)...\n");
                    s_ota_running = true;
                    BaseType_t res = xTaskCreate(ota_worker_task, "ota_worker", 24576, NULL, 5, NULL);
                    if (res != pdPASS) {
                        printf("[OTA LỖI] Không thể tạo tác vụ OTA (Thiếu bộ nhớ FreeRTOS)!\n");
                        s_ota_running = false;
                    }
                }
            } else if (strcasecmp(p, "help") == 0) {
                printf("\n--- DANH SÁCH LỆNH CONSOLE ---\n");
                printf("  upd   : Kiểm tra và tiến hành cập nhật OTA từ server\n");
                printf("  info  : Xem thông tin IP, phiên bản, thông số board\n");
                printf("  reboot: Khởi động lại thiết bị\n");
                printf("------------------------------\n");
            } else if (strcasecmp(p, "info") == 0) {
                printf("\n[THÔNG TIN THIẾT BỊ]\n");
                printf("  - Board      : ESP32-S3-N16R8 (16MB Flash, 8MB PSRAM)\n");
                printf("  - Phiên bản  : %s\n", ota_update_get_version());
                printf("  - Địa chỉ IP : %s\n", wifi_get_ip_str());
                printf("  - OTA Cloud  : %s\n", OTA_MANIFEST_URL);
            } else if (strcasecmp(p, "reboot") == 0) {
                printf("\n[SYSTEM] Đang khởi động lại...\n");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            } else if (len > 0) {
                printf("\n[?] Không nhận dạng được lệnh '%s'. Hãy gõ 'upd' để cập nhật OTA hoặc 'help'.\n", p);
            }
            line_len = 0;
        } else if (c == '\b' || c == 0x7F) {
            if (line_len > 0) {
                line_len--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (line_len < (int)sizeof(line_buf) - 1) {
            line_buf[line_len++] = (char)c;
        }
    }
}

static void init_sntp_time(void)
{
    ESP_LOGI(TAG, "Khởi tạo SNTP đồng bộ giờ Internet...");
    setenv("TZ", "ICT-7", 1);
    tzset();
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_setservername(2, "time.cloudflare.com");
        esp_sntp_init();
    }
}

/* =============================================================================
 * 5. HÀM MAIN
 * ============================================================================= */
void app_main(void)
{
    /* Chờ cổng USB CDC / Serial kết nối và ổn định trên máy tính khi boot */
    vTaskDelay(pdMS_TO_TICKS(1500));

    printf("\n\n======================================================================\n");
    printf("=== KHỞI ĐỘNG ESP32-S3-N16R8 OTA CLIENT ===\n");
    printf("Phiên bản firmware : v%s\n", FW_VERSION_STR);
#if defined(FW_FEATURE_BLE)
    printf("Chức năng test     : [1] Bluetooth BLE Test (Server: %s)\n", BLE_DEVICE_NAME);
#elif defined(FW_FEATURE_WIFI_SCAN)
    printf("Chức năng test     : [2] Wi-Fi Scanner Test (Quét AP xung quanh)\n");
#elif defined(FW_FEATURE_RTC_WEB)
    printf("Chức năng test     : [3] RTC Web SNTP Test (Đồng bộ giờ Internet)\n");
#endif
    printf("======================================================================\n");

    /* 1. Khởi tạo NVS Flash */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Khởi tạo TCP/IP Netif và Event Loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 3. Cập nhật version NVS */
    ota_update_init_version();

    /* 4. Khởi động Wi-Fi kết nối tới AP */
    printf("[WIFI] Đang kết nối tới điểm phát Wi-Fi: [%s]...\n", OTA_WIFI_SSID);
    esp_err_t wifi_err = wifi_init_sta();
    if (wifi_err == ESP_OK) {
        printf("[WIFI KẾT NỐI] Đã cấp IP: %s\n", wifi_get_ip_str());
        init_sntp_time();
    } else {
        printf("[WIFI CẢNH BÁO] Kết nối lần đầu chưa thành công. Sẽ tự động kết nối lại ngầm.\n");
    }

    /* Kiểm tra và xác nhận firmware mới (hủy cơ chế rollback) */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            printf("[OTA ROLLBACK] Firmware mới đang trong trạng thái kiểm thử (PENDING_VERIFY)...\n");
            esp_err_t ota_val_err = esp_ota_mark_app_valid_cancel_rollback();
            if (ota_val_err == ESP_OK) {
                printf("[OTA ROLLBACK] >> ĐÃ XÁC NHẬN FIRMWARE MỚI HOẠT ĐỘNG TỐT (VALID)! Hủy cơ chế Rollback.\n");
            } else {
                ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback thất bại: %s", esp_err_to_name(ota_val_err));
            }
        }
    }

    /* 5. Khởi tạo tính năng cụ thể */
#if defined(FW_FEATURE_BLE)
    ble_init_test();
#elif defined(FW_FEATURE_WIFI_SCAN)
    xTaskCreate(wifi_scanner_task, "wifi_scan", 4096, NULL, 5, NULL);
#elif defined(FW_FEATURE_RTC_WEB)
    xTaskCreate(rtc_web_task, "rtc_web", 4096, NULL, 5, NULL);
#endif

    /* 6. Tạo task lắng nghe console lệnh 'upd' */
    xTaskCreate(console_task, "console_task", 10240, NULL, 5, NULL);

    printf("\n======================================================================\n");
    printf(">> HỆ THỐNG ĐÃ SẴN SÀNG! <<\n");
    printf(">> Khi bạn muốn kiểm tra & nạp OTA, hãy gõ: upd và nhấn ENTER <<\n");
    printf("======================================================================\n\n");

    /* Loop giám sát Wi-Fi nếu bị rớt mạng */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "Wi-Fi mất kết nối, đang thử kết nối lại...");
            wifi_deinit_driver();
            wifi_init_sta();
        }
    }
}
