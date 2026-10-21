#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ V12 // CLEAN REPO + 50MB APK + 1.50GB OBB CLOUD BUILDER"
echo "================================================================="

# Remove all temporary chat/setup scripts so IMS repository stays 100% clean
rm -f setup_part*.sh update_chat*.sh fix_*.sh force_*.sh generate_*.sh upgrade_*.sh v10_chat*.sh v12_chat*.sh v12_final_deploy.sh
rm -rf app/src/main/assets/models

git add -A
git commit -m "ApexDJ V12: 50MB APK, Mandatory 1.50GB Neural Brain OBB, 3-Deck BGM Booth, 0ms Load, In-App Local, 4 Themes, REC" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub. Waiting for cloud run to register..."
sleep 6

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

mkdir -p /sdcard/Download/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-50MB-App-APK" -D /sdcard/Download/ApexDJ_Build/

# Download the 1.50 GB OBB once if not already present locally
if [ ! -f "/sdcard/Download/ApexDJ_Build/main.1.com.apexdj.butter.obb" ]; then
    echo "[+] Downloading 1.50 GB Master OBB Vault from GitHub Release final-obb-vault-v12..."
    gh release download final-obb-vault-v12 -p "main.1.com.apexdj.butter.obb" -D /sdcard/Download/ApexDJ_Build/ --clobber || true
fi

echo "[✓] SUCCESS! Files ready in /sdcard/Download/ApexDJ_Build/:"
ls -lh /sdcard/Download/ApexDJ_Build/
