# ESP32-S3-N16R8 OTA & Multi-Feature Test Firmware

Dự án firmware cập nhật OTA tương thích với board **ESP32-S3-N16R8** (16MB Flash, 8MB Octal PSRAM, không camera), chạy trên **VS Code + PlatformIO (ESP-IDF Framework)**.

---

## 1. Cấu hình phần cứng & Phân vùng (Flash 16MB)

- **Vi điều khiển**: ESP32-S3 Dual-Core Xtensa LX7 @ 240MHz.
- **Flash**: 16MB (Chế độ `DIO` 80MHz an toàn chống lỗi bus).
- **PSRAM**: 8MB Octal SPI (`CONFIG_SPIRAM_MODE_OCT=y`).
- **Phân vùng (Dual OTA 4MB mỗi bản)**:
  - `ota_0` (4MB): Ứng dụng chính.
  - `ota_1` (4MB): Ứng dụng dự phòng / nạp mới OTA.
  - `storage` (7.6MB): SPIFFS lưu trữ file.
  - `otadata`, `nvs`, `phy_init`.
- **Cơ chế an toàn**: Bật Rollback tự động (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) và kiểm tra mã băm phần cứng SHA-256 trước khi reboot.

---

## 2. Ba phiên bản Firmware trong dự án

Tất cả các bản firmware đều duy trì kết nối Wi-Fi ngầm và **chỉ tiến hành kiểm tra & tải OTA khi người dùng gõ lệnh `upd`** trên Serial Monitor.

| Môi trường PlatformIO | Phiên bản | Tên file binary | Tính năng kiểm thử |
| :--- | :---: | :--- | :--- |
| `[env:esp32s3_n16r8_ble]` | **1.0.0** | `firmware_ble_v1.0.0.bin` | **Bluetooth BLE Test**: Phát sóng BLE tên `ESP32S3_BLE_N16R8`, chấp nhận kết nối từ điện thoại, duy trì Wi-Fi & chờ lệnh `upd`. |
| `[env:esp32s3_n16r8_wifi_scan]` | **1.1.0** | `firmware_wifi_scan_v1.1.0.bin` | **Wi-Fi Scanner Test**: Quét các AP 2.4GHz xung quanh mỗi 15s, in bảng chi tiết SSID, RSSI, Kênh, Bảo mật & chờ lệnh `upd`. |
| `[env:esp32s3_n16r8_rtc_web]` | **1.2.0** | `firmware_rtc_web_v1.2.0.bin` | **RTC Web SNTP Test**: Đồng bộ giờ thực tế qua Internet từ máy chủ NTP (`pool.ntp.org`), in đồng hồ chuẩn Việt Nam (ICT UTC+7) & chờ lệnh `upd`. |

---

## 3. Quy trình nạp và kiểm thử OTA từng bước

### Bước 1: Nạp bản gốc (v1.0.0 - Bluetooth BLE) vào Board qua USB
1. Cắm cáp USB Type-C từ ESP32-S3-N16R8 vào máy tính.
2. Mở terminal tại thư mục dự án và chạy:
   ```bash
   pio run -e esp32s3_n16r8_ble -t upload -t monitor
   ```
3. Board khởi động:
   - Kết nối Wi-Fi vào SSID `Mạng Chùa` và nhận IP.
   - Bắt đầu phát sóng BLE: `ESP32S3_BLE_N16R8`. (Bạn có thể mở điện thoại quét Bluetooth để thấy ngay).
   - Console hiển thị: `>> Khi bạn muốn kiểm tra & nạp OTA, hãy gõ: upd và nhấn ENTER <<`.

### Bước 2: Thử nghiệm OTA lần 1 (Cập nhật lên v1.1.0 - Quét Wi-Fi)
1. Mở trình duyệt vào trang quản trị:
   [`https://system-browsing-delegate.ngrok-free.dev/admin`](https://system-browsing-delegate.ngrok-free.dev/admin) (hoặc `http://localhost:5000/admin`).
2. Chọn:
   - **Device Type**: `esp32-s3-n16r8`
   - **Version**: `1.1.0`
   - **Firmware Binary**: Chọn file `firmware_binaries/firmware_wifi_scan_v1.1.0.bin`
3. Nhấn **Tải lên Firmware**.
4. Quay lại màn hình Serial Monitor của ESP32-S3, gõ:
   ```text
   upd
   ```
   rồi nhấn **ENTER**.
5. **Quan sát**:
   - ESP32 gửi request lên server Ngrok, phát hiện có bản `1.1.0`.
   - Hiển thị tiến trình tải 0% -> 100%.
   - Xác thực SHA-256 và reboot vào phân vùng mới.
   - Sau khi khởi động lại, board chạy bản `v1.1.0` và bắt đầu quét danh sách Wi-Fi xung quanh!

### Bước 3: Thử nghiệm OTA lần 2 (Cập nhật tiếp lên v1.2.0 - RTC Web)
1. Vào trang Web Admin tải file thứ 2 lên:
   - **Device Type**: `esp32-s3-n16r8`
   - **Version**: `1.2.0`
   - **Firmware Binary**: Chọn file `firmware_binaries/firmware_rtc_web_v1.2.0.bin`
2. Nhấn **Tải lên Firmware**.
3. Tại Serial Monitor, gõ tiếp:
   ```text
   upd
   ```
   rồi nhấn **ENTER**.
4. ESP32 tự động tải về, nạp vào phân vùng đối ứng và reboot vào bản `v1.2.0`.
5. Sau khi reboot, board tự động kết nối máy chủ NTP Internet và hiển thị giờ thực chuẩn Việt Nam!

---

## 4. Các lệnh Console hỗ trợ
- `upd` hoặc `update`: Kiểm tra và nạp bản OTA mới từ server.
- `info`: Xem thông tin địa chỉ IP, loại board, phiên bản hiện tại, URL server.
- `help`: Danh sách các lệnh điều khiển.
- `reboot`: Khởi động lại thiết bị.
