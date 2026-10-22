#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ V15 // APP-ONLY FAST UPDATE (5000W PA CLARITY ENGINE)"
echo "================================================================="

# Remove all temporary scripts & clutter files from repository
rm -f setup_part*.sh update_chat*.sh fix_*.sh force_*.sh generate_*.sh upgrade_*.sh v10_chat*.sh v12_*.sh update_app_chat*.sh
rm -rf app/src/main/assets/models

git add -A
git commit -m "ApexDJ V15 App-Only Update: 5000W PA Bit-Pure Engine, 0-Vocal BGM Switch, Zero Tempo Warble" || true
git push -u origin HEAD

echo "[+] Pushed to GitHub. Waiting for cloud compiler..."
sleep 6

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

mkdir -p /sdcard/Download/ApexDJ_Build
rm -f /sdcard/Download/ApexDJ_Build/*.apk
gh run download "$RUN_ID" -n "ApexDJ-50MB-App-APK" -D /sdcard/Download/ApexDJ_Build/

echo "[✓] APP-ONLY UPDATE READY IN /sdcard/Download/ApexDJ_Build/:"
ls -lh /sdcard/Download/ApexDJ_Build/
termux-open /sdcard/Download/ApexDJ_Build/app-release.apk || true
