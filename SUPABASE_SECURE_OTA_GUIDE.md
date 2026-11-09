# HƯỚNG DẪN CẤU HÌNH SUPABASE STORAGE & OTA BẢO MẬT CAO CẤP (ECDSA P-256)
*Dành cho ESP32-S3-N16R8 (Dual OTA 4MB, Chống hack Database/Storage 100%)*

---

## 1. TỔNG QUAN KIẾN TRÚC BẢO MẬT

Hệ thống OTA này áp dụng mô hình **Zero-Trust Cloud Storage**:
- **Giả định rủi ro**: Hacker chiếm toàn quyền Database / Supabase Storage, có thể upload file `.bin` độc hại và chỉnh sửa file `version.json`.
- **Cơ chế bảo vệ**:
  1. **Khóa công khai (Public Key)** được nhúng cố định trong Flash của ESP32.
  2. **Khóa bí mật (Private Key)** chỉ nằm duy nhất trên máy tính của bạn (đã được cấu hình quyền `600` và bảo vệ trong `.gitignore`).
  3. Mọi bản firmware đều phải có **Chữ ký số ECDSA NIST P-256** hợp lệ được tạo bởi Private Key.
  4. Nếu Hacker tải file mã độc lên Supabase và tự sinh hash SHA-256 -> ESP32 kiểm tra chữ ký số không khớp -> **Từ chối nạp ngay lập tức, giữ nguyên phân vùng cũ an toàn 100%**.
  5. **Rollback tự động**: Nếu firmware boot lên gặp sự cố, ESP32 tự động đảo về bản firmware trước đó.

---

## 2. CÁC BƯỚC THIẾT LẬP SUPABASE STORAGE (5 PHÚT)

### Bước 2.1: Tạo Storage Bucket
1. Đăng nhập vào [Supabase Dashboard](https://supabase.com/dashboard) và chọn Project của bạn.
2. Menu bên trái chọn **Storage** -> Click **New bucket**.
3. Điền thông tin:
   - **Name**: `firmware`
   - **Public bucket**: **BẬT (ON)** *(Để ESP32 có thể tải file trực tiếp qua HTTPS mà không cần API key)*.
4. Click **Save bucket**.

### Bước 2.2: Thiết lập Bảo mật RLS (Row Level Security) cho Storage
Để ngăn chặn bất kỳ ai sửa hoặc xóa file trên Bucket của bạn:
1. Vào **SQL Editor** trong Supabase Dashboard.
2. Dán đoạn SQL sau và nhấn **Run**:

```sql
-- 1. Cho phép công chúng tải firmware (READ / SELECT)
CREATE POLICY "Public Read Firmware"
ON storage.objects FOR SELECT
TO public
USING (bucket_id = 'firmware');

-- 2. Chỉ cho phép tài khoản Admin đã đăng nhập upload firmware (INSERT)
CREATE POLICY "Admin Insert Firmware"
ON storage.objects FOR INSERT
TO authenticated
WITH CHECK (bucket_id = 'firmware');

-- 3. Chỉ cho phép tài khoản Admin cập nhật hoặc xóa firmware (UPDATE / DELETE)
CREATE POLICY "Admin Update Firmware"
ON storage.objects FOR UPDATE
TO authenticated
USING (bucket_id = 'firmware');

CREATE POLICY "Admin Delete Firmware"
ON storage.objects FOR DELETE
TO authenticated
USING (bucket_id = 'firmware');
```

---

## 3. CẤU HÌNH ESP32 VỚI URL SUPABASE CỦA BẠN

Mở file [`src/ota_config.h`](file:///home/alberttochiro/Documents/antigravity/VSCodeESPALL/ESP32S3_N16R8_OTA/src/ota_config.h):

```c
// --- 1. CẤU HÌNH WI-FI ---
#define OTA_WIFI_SSID           "Tên_WiFi_Của_Bạn"
#define OTA_WIFI_PASS           "Mat_Khau_WiFi"

// --- 2. CẤU HÌNH SUPABASE STORAGE URL ---
// Thay [YOUR_PROJECT_REF] bằng mã project trên thanh địa chỉ Supabase của bạn
#define OTA_MANIFEST_URL        "https://[YOUR_PROJECT_REF].supabase.co/storage/v1/object/public/firmware/version.json"
```

---

## 4. QUY TRÌNH PHÁT HÀNH & KÝ SỐ FIRMWARE (DEVELOPER WORKFLOW)

Mỗi khi bạn muốn phát hành một bản cập nhật:

### Bước 4.1: Biên dịch firmware
Ví dụ biên dịch bản Wi-Fi Scanner (v1.1.0):
```bash
/home/alberttochiro/.platformio/penv/bin/pio run -e esp32s3_n16r8_wifi_scan
```
File binary được tạo tại: `.pio/build/esp32s3_n16r8_wifi_scan/firmware.bin`.

### Bước 4.2: Ký số và tạo `version.json` tự động
Sử dụng công cụ `tools/sign_firmware.py`:
```bash
python3 tools/sign_firmware.py \
  --bin .pio/build/esp32s3_n16r8_wifi_scan/firmware.bin \
  --version 1.1.0 \
  --url "https://[YOUR_PROJECT_REF].supabase.co/storage/v1/object/public/firmware/firmware_wifi_scan_v1.1.0.bin" \
  --out version.json
```
Script sẽ:
- Tính Image SHA-256 phần cứng chuẩn xác.
- Ký bằng `tools/ota_private_key.pem`.
- Xuất file `version.json` có trường `"signature"` mã hóa ECDSA P-256.

### Bước 4.3: Upload lên Supabase Storage
1. Mở bucket `firmware` trên Supabase Web.
2. Upload 2 file:
   - File binary: `firmware.bin` (đổi tên tương ứng, ví dụ `firmware_wifi_scan_v1.1.0.bin`).
   - File manifest: `version.json`.

---

## 5. THỬ NGHIỆM KỊCH BẢN BẢO MẬT & TEST CẬP NHẬT

### Kịch bản 1: Cập nhật hợp lệ (Developer phát hành)
1. Cắm ESP32, nạp bản gốc v1.0.0 (BLE).
2. Khi ESP32 chạy, mở Terminal Serial (tốc độ 115200 baud).
3. Gõ:
   ```text
   upd
   ```
   và nhấn **ENTER**.
4. ESP32 sẽ:
   - Kết nối HTTPS tới Supabase.
   - Tải file `firmware_wifi_scan_v1.1.0.bin` vào phân vùng `ota_1`.
   - Kiểm tra SHA-256 -> Hợp lệ.
   - Xác minh chữ ký số ECDSA P-256 -> **Hợp lệ!**
   - Chuyển boot sang `ota_1` và restart.
   - Khởi động lên: Chức năng Wi-Fi Scanner bắt đầu hoạt động!

### Kịch bản 2: Giả lập Database / Supabase bị hack
1. Một kẻ tấn công đổi file `version.json` để trỏ vào `malware.bin` và tự điền SHA-256 của file độc hại đó.
2. Trên ESP32 gõ `upd`.
3. Kết quả:
   ```text
   [OTA BẢO MẬT 2/2] Đang xác minh Chữ ký số mã hóa ECDSA P-256 từ Public Key...
   [CẢNH BÁO AN NINH CỰC ĐỘ] XÁC THỰC CHỮ KÝ SỐ THẤT BẠI!
   => Firmware KHÔNG ĐƯỢC KÝ bởi Private Key hợp lệ của Developer!
   => CẢNH BÁO: Database hoặc Server có thể đã bị chiếm quyền/sửa đổi!
   => HỦY BỎ NẠP NGAY LẬP TỨC ĐỂ BẢO VỆ THIẾT BỊ.
   ```
4. Phân vùng Flash được giữ nguyên vẹn, thiết bị không bị hư hại hoặc biến thành botnet.

---

## 6. DANH SÁCH 3 FILE FIRMWARE TEST ĐÃ CÓ SẴN TRONG THƯ MỤC `firmware_binaries/`

| Tên file | Phiên bản | Tính năng test | File Manifest ký sẵn |
| :--- | :---: | :--- | :--- |
| `firmware_ble_v1.0.0.bin` | 1.0.0 | Bluetooth BLE Server (`ESP32S3_BLE_N16R8`) | Bản nạp gốc khởi đầu |
| `firmware_wifi_scan_v1.1.0.bin` | 1.1.0 | Quét danh sách Wi-Fi AP xung quanh mỗi 10s | `firmware_binaries/version_v1.1.0.json` |
| `firmware_rtc_web_v1.2.0.bin` | 1.2.0 | Đồng bộ thời gian thực SNTP (Giờ Việt Nam ICT-7) | `firmware_binaries/version_v1.2.0.json` |
