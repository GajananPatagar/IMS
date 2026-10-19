#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ // 1.02 GB PRO CLUB WORKSTATION CLOUD BUILDER"
echo "================================================================="

pkg install -y git gh

git add -A
git commit -m "Upgrade to 1.02 GB Pro Club Hardware UI, WAV Recorder, Local Add & Bass-Swap Auto-DJ" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub. Waiting 10 seconds for workflow to start..."
sleep 10

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

termux-setup-storage || true
sleep 2

mkdir -p ~/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D ~/ApexDJ_Build/

mkdir -p ~/storage/downloads/ApexDJ_Build
cp -f ~/ApexDJ_Build/*.apk ~/storage/downloads/ApexDJ_Build/
ls -lh ~/storage/downloads/ApexDJ_Build/
echo "[✓] SUCCESS! 1.02 GB Pro Club APK saved to Downloads/ApexDJ_Build/app-release.apk"
