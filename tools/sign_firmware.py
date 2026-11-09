#!/usr/bin/env python3
"""
tools/sign_firmware.py
=============================================================================
CÔNG CỤ KÝ SỐ FIRMWARE CHO ESP32-S3 OTA BẢO MẬT CAO CẤP (ECDSA NIST P-256)
=============================================================================
Tính năng:
1. Đọc và kiểm tra tính hợp lệ của file binary ESP32-S3 (Magic 0xE9, Chip ID 0x0009).
2. Trích xuất/tính toán mã băm SHA-256 tương thích 100% với esp_partition_get_sha256() trên chip.
3. Ký mã băm bằng Private Key ECDSA P-256 của Developer (chữ ký số định dạng DER hex).
4. Tự động kiểm tra chéo (verify) lại chữ ký bằng Public Key trước khi lưu file.
5. Tạo ra file `version.json` sẵn sàng upload lên Supabase Storage.
"""

import sys
import os
import json
import argparse
import datetime
from pathlib import Path

try:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import Prehashed
    from cryptography.hazmat.primitives.serialization import load_pem_private_key, load_pem_public_key
except ImportError:
    print("[LỖI] Chưa cài đặt thư viện 'cryptography'.")
    print("Vui lòng cài đặt bằng lệnh: pip install cryptography")
    sys.exit(1)

MAGIC_ESP_IMAGE = 0xE9
CHIP_ID_ESP32S3 = 0x0009

def parse_args():
    parser = argparse.ArgumentParser(
        description="Ký số firmware ESP32-S3 bằng ECDSA P-256 chống hack Database/Cloud Storage."
    )
    parser.add_argument("--bin", required=True, help="Đường dẫn file firmware .bin")
    parser.add_argument("--version", required=True, help="Số phiên bản (VD: 1.1.0, 1.2.0)")
    parser.add_argument("--key", default=None, help="Đường dẫn file ota_private_key.pem (mặc định: tools/ota_private_key.pem)")
    parser.add_argument("--pubkey", default=None, help="Đường dẫn file ota_public_key.pem để verify lại (mặc định: tools/ota_public_key.pem)")
    parser.add_argument("--url", default="", help="URL tải firmware trên Supabase/Cloud (VD: https://.../firmware_v1.1.0.bin)")
    parser.add_argument("--out", default="version.json", help="Đường dẫn file manifest JSON đầu ra (mặc định: version.json)")
    parser.add_argument("--notes", default="", help="Ghi chú mô tả bản cập nhật")
    return parser.parse_args()

def get_firmware_sha256(data: bytes) -> tuple[bytes, str]:
    """
    Tính mã SHA-256 của firmware đúng theo chuẩn bootloader_common_get_sha256_of_partition() của ESP-IDF:
    Nếu header có hash_appended = 1 (byte thứ 23), 32 bytes cuối cùng của file chính là mã SHA-256
    của toàn bộ dữ liệu phía trước (data[:-32]).
    """
    if len(data) < 32:
        raise ValueError("Kích thước file binary quá nhỏ, không phải firmware ESP-IDF hợp lệ.")

    magic = data[0]
    if magic != MAGIC_ESP_IMAGE:
        raise ValueError(f"Magic byte không đúng (kỳ vọng 0xE9, nhận được 0x{magic:02X})")

    # Kiểm tra chip_id ở offset 12..13 (ESP32-S3 = 0x0009)
    if len(data) >= 14:
        chip_id = data[12] | (data[13] << 8)
        if chip_id != CHIP_ID_ESP32S3:
            print(f"[CẢNH BÁO] Chip ID trong header là 0x{chip_id:04X} (ESP32-S3 là 0x0009).")

    hash_appended = data[23] if len(data) > 23 else 0
    if hash_appended == 1:
        # 32 bytes cuối là hash của data[:-32]
        payload = data[:-32]
        digest = hashes.Hash(hashes.SHA256())
        digest.update(payload)
        calc_sha = digest.finalize()
        embedded_sha = data[-32:]
        if calc_sha != embedded_sha:
            raise ValueError("Mã băm nhúng sẵn ở cuối file binary không khớp với dữ liệu payload!")
        return embedded_sha, embedded_sha.hex()
    else:
        # Nếu không có hash_appended, hash tính trên toàn bộ file
        digest = hashes.Hash(hashes.SHA256())
        digest.update(data)
        whole_sha = digest.finalize()
        return whole_sha, whole_sha.hex()

def get_default_supabase_base_url(script_dir: Path) -> str:
    """Tự động đọc URL Supabase đã cấu hình trong src/ota_config.h"""
    config_path = script_dir.parent / "src" / "ota_config.h"
    if config_path.is_file():
        with open(config_path, "r", encoding="utf-8") as f:
            for line in f:
                if "#define OTA_MANIFEST_URL" in line:
                    parts = line.split('"')
                    if len(parts) >= 2:
                        manifest_url = parts[1].strip()
                        # Bỏ phần version.json ở cuối để lấy thư mục bucket
                        base_url = manifest_url.rsplit("/", 1)[0]
                        return base_url
    return "https://YOUR_SUPABASE_PROJECT.supabase.co/storage/v1/object/public/firmware"

def main():
    args = parse_args()
    script_dir = Path(__file__).resolve().parent

    bin_path = Path(args.bin).resolve()
    if not bin_path.is_file():
        print(f"[LỖI] Không tìm thấy file binary: {bin_path}")
        sys.exit(1)

    # Tìm Private Key
    priv_path = Path(args.key).resolve() if args.key else script_dir / "ota_private_key.pem"
    if not priv_path.is_file():
        print(f"[LỖI] Không tìm thấy Private Key tại: {priv_path}")
        print("Vui lòng tạo khóa hoặc chỉ định đường dẫn bằng --key.")
        sys.exit(1)

    # Tìm Public Key (dùng để self-check)
    pub_path = Path(args.pubkey).resolve() if args.pubkey else script_dir / "ota_public_key.pem"

    with open(bin_path, "rb") as f:
        bin_data = f.read()

    file_size = len(bin_data)
    print(f"[1/4] Đọc file firmware: {bin_path.name} ({file_size:,} bytes)")

    # Tính toán SHA-256
    sha_bytes, sha_hex = get_firmware_sha256(bin_data)
    print(f"[2/4] Mã băm SHA-256 phần cứng (ESP-IDF Image Digest):")
    print(f"      {sha_hex}")

    # Đọc Private Key và ký
    with open(priv_path, "rb") as f:
        priv_key = load_pem_private_key(f.read(), password=None)

    signature_der = priv_key.sign(
        sha_bytes,
        ec.ECDSA(Prehashed(hashes.SHA256()))
    )
    sig_hex = signature_der.hex()
    print(f"[3/4] Đã tạo Chữ ký số ECDSA NIST P-256 (DER format, {len(signature_der)} bytes):")
    print(f"      {sig_hex}")

    # Tự động kiểm tra lại với Public Key
    if pub_path.is_file():
        with open(pub_path, "rb") as f:
            pub_key = load_pem_public_key(f.read())
        try:
            pub_key.verify(signature_der, sha_bytes, ec.ECDSA(Prehashed(hashes.SHA256())))
            print("[XÁC THỰC] Self-check thành công: Chữ ký số hoàn toàn khớp với Public Key!")
        except Exception as e:
            print(f"[LỖI BẢO MẬT] Kiểm tra lại chữ ký thất bại: {e}")
            sys.exit(1)

    # Tạo JSON manifest
    if args.url:
        url = args.url
    else:
        base_url = get_default_supabase_base_url(script_dir)
        url = f"{base_url}/{bin_path.name}"
    manifest = {
        "version": args.version,
        "name": bin_path.name,
        "url": url,
        "size": file_size,
        "sha256": sha_hex,
        "signature": sig_hex,
        "notes": args.notes or f"Firmware v{args.version} ({bin_path.name}) signed with ECDSA P-256",
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat()
    }

    out_path = Path(args.out).resolve()
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print(f"[4/4] Đã xuất file cấu hình: {out_path}")
    print("\n======================= NỘI DUNG MANIFEST VERSION.JSON =======================")
    print(json.dumps(manifest, indent=2, ensure_ascii=False))
    print("==============================================================================\n")
    print("=> HÃY UPLOAD FILE .BIN VÀ FILE VERSION.JSON LÊN SUPABASE STORAGE BUCKET 'firmware'.")

if __name__ == "__main__":
    main()
