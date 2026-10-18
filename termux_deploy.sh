#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ // TERMUX TO GITHUB ACTIONS CLOUD FAT-APK BUILDER"
echo "================================================================="

pkg install -y git gh

git add -A
git commit -m "Build 1-Crore Flagship DJ App (Jogwheels, 8 Hot Cues, 14 Beat FX, Stems, Butter Morph)" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub successfully. Monitoring cloud build..."
sleep 5

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

mkdir -p /sdcard/Download/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D /sdcard/Download/ApexDJ_Build/
echo "[✓] SUCCESS! Installable Fat-APK downloaded to: /sdcard/Download/ApexDJ_Build/"
