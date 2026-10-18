#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ // 1.02 GB FAT-APK CLOUD BUILD & AUTO-DOWNLOADER"
echo "================================================================="

pkg install -y git gh

git add -A
git commit -m "1.02 GB Flagship Edition with 16 Neural Tensor Banks & Auto-DJ" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub successfully. Waiting 10 seconds for workflow..."
sleep 10

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

termux-setup-storage || true
sleep 2

mkdir -p ~/storage/downloads/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D ~/storage/downloads/ApexDJ_Build/
echo "[✓] SUCCESS! 1.02 GB Fat-APK downloaded directly to: ~/storage/downloads/ApexDJ_Build/"
