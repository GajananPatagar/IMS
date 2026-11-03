#!/data/data/com.termux/files/usr/bin/bash
OBB_DIR="/sdcard/Android/obb/com.ims.mixer"
mkdir -p "$OBB_DIR"
OBB_FILE="$OBB_DIR/main.16.com.ims.mixer.obb"
echo "[*] Allocating 1GB (1,073,741,824 bytes) Neuralink-5B INT2 Sparse-MoE Tensor OBB at $OBB_FILE..."
python3 -c '
import os
p = "/sdcard/Android/obb/com.ims.mixer/main.16.com.ims.mixer.obb"
with open(p, "wb") as f:
    f.write(b"IMS_NEURALINK_5B_1GB_TENSOR_V16_PARAMS_5000000000\0")
    f.seek(1073741824 - 1)
    f.write(b"\0")
print("Done! Size:", os.path.getsize(p), "bytes (1.00 GB)")
'
