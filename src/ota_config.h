#pragma once

// =============================================================================
// CẤU HÌNH CLOUD OTA (FILE DUY NHẤT)
// =============================================================================

// --- 1. CẤU HÌNH WI-FI ---
#define OTA_WIFI_SSID           "TTHocLieuT1"
#define OTA_WIFI_PASS           "hoclieut1"

// --- 2. CẤU HÌNH SUPABASE STORAGE / CLOUD URL ---
// Thay thế bằng đường dẫn public trực tiếp tới file version.json trên Supabase Storage của bạn.
// Ví dụ: "https://abcdefghijklmnopqrst.supabase.co/storage/v1/object/public/firmware/version.json"
#define OTA_MANIFEST_URL        "https://raw.githubusercontent.com/zronin-189/OTA-Code/main/firmware_binaries/version.json"

// Ánh xạ tương thích mã nguồn
#undef CONFIG_OTA_WIFI_SSID
#define CONFIG_OTA_WIFI_SSID    OTA_WIFI_SSID

#undef CONFIG_OTA_WIFI_PASS
#define CONFIG_OTA_WIFI_PASS    OTA_WIFI_PASS

#ifndef CONFIG_OTA_SERVER_URL
#define CONFIG_OTA_SERVER_URL   OTA_MANIFEST_URL
#endif
