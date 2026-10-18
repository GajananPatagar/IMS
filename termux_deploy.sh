#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ // SELF-NEURAL CLOUD APK BUILDER & AUTO-DOWNLOADER"
echo "================================================================="

pkg install -y git gh

git add -A
git commit -m "Fix SDK build & upgrade to C++20 Self-Neural 5-Stem Engine" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub. Waiting 8 seconds for workflow to register..."
sleep 8

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

mkdir -p /sdcard/Download/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D /sdcard/Download/ApexDJ_Build/
echo "[✓] SUCCESS! Signed Release APK downloaded to: /sdcard/Download/ApexDJ_Build/"
