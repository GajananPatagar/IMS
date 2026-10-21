#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ V10 // CLEAN REPO DEPLOYMENT SCRIPT"
echo "================================================================="

pkg install -y git gh

# Purge ALL old/unnecessary setup scripts and clutter files automatically!
rm -f setup_part*.sh update_chat*.sh fix_*.sh force_*.sh generate_*.sh upgrade_pro_neural.sh

git add -A
git commit -m "ApexDJ V10 Sovereign: Cleaned old scripts, Permanent OBB Vault, 15MB Lightweight APK Build" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub successfully. Monitoring cloud build..."
sleep 5

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

mkdir -p /sdcard/Download/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-15MB-Lightweight-APK" -D /sdcard/Download/ApexDJ_Build/
echo "[✓] SUCCESS! 15MB Lightweight APK downloaded to: /sdcard/Download/ApexDJ_Build/"
ls -lh /sdcard/Download/ApexDJ_Build/
