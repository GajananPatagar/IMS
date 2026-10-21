#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ V8 // LIGHTWEIGHT APK + STANDALONE .OBB CLOUD BUILDER"
echo "================================================================="

pkg install -y git gh

# Ensure no heavy files are inside APK assets so APK stays ~15MB!
rm -rf app/src/main/assets/models

git add -A
git commit -m "ApexDJ V8: Lightweight APK, Standalone Auto-Copy OBB, Safe Notification Bar, Stutter-Free 64-Bit DSP" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub. Waiting for cloud run to register..."
sleep 5

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

mkdir -p /sdcard/Download/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D /sdcard/Download/ApexDJ_Build/
echo "[✓] SUCCESS! Lightweight APK & main.1.com.apexdj.butter.obb downloaded to: /sdcard/Download/ApexDJ_Build/"
ls -lh /sdcard/Download/ApexDJ_Build/
