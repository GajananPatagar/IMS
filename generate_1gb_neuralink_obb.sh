#!/data/data/com.termux/files/usr/bin/bash
OBB_DIR="/sdcard/Android/obb/com.ims.mixer"
mkdir -p "$OBB_DIR"
OBB_FILE="$OBB_DIR/main.16.com.ims.mixer.obb"
echo "[*] Allocating 1GB (1,073,741,824 bytes) Neuralink-5B INT2 Sparse-MoE Tensor File at $OBB_FILE..."
python3 -c '
import os, struct
p = "/sdcard/Android/obb/com.ims.mixer/main.16.com.ims.mixer.obb"
with open(p, "wb") as f:
    f.write(b"IMS_NEURALINK_5B_SPARSE_MOE_V16\0")
    f.write(struct.pack("<QII", 5000000000, 64, 2))
    f.seek(1073741824 - 1)
    f.write(b"\0")
print("Allocated 1GB Neuralink-5B OBB:", os.path.getsize(p), "bytes")
'
