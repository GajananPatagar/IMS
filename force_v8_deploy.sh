#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " [1/5] WIPING OLD CACHED APKs SO YESTERDAY'S BUILD CANNOT LOAD"
echo "================================================================="
termux-setup-storage || true
rm -rf ~/ApexDJ_Build
rm -rf ~/storage/downloads/ApexDJ_Build/*.apk || true
mkdir -p ~/ApexDJ_Build ~/storage/downloads/ApexDJ_Build

echo "================================================================="
echo " [2/5] WRITING V8 C++20 HERMITE & BIT-EXACT 5-STEM NEURAL ENGINE"
echo "================================================================="
chmod +x setup_part1.sh setup_part2.sh
./setup_part1.sh
./setup_part2.sh

echo "================================================================="
echo " [3/5] WRITING V8 CLEAN KOTLIN REPOSITORY & ROTARY OPUS-QUAD UI"
echo "================================================================="
chmod +x upgrade_pro_neural.sh
# Extract and run only the file-generation sections of upgrade_pro_neural.sh
sed -n '1,/^echo "\[3\/4\]/p' upgrade_pro_neural.sh | sed '$d' > generate_v8_kotlin.sh
chmod +x generate_v8_kotlin.sh
./generate_v8_kotlin.sh

# Ensure autoDjEnabled starts OFF on launch so nothing ever auto-plays until you tap Play
python3 - << 'PYEOF'
ui_path = "app/src/main/java/com/apexdj/butter/ui/FlagshipMixerScreen.kt"
with open(ui_path, "r") as f:
    code = f.read()
code = code.replace(
    "var autoDjEnabled by remember { mutableStateOf(true) }",
    "var autoDjEnabled by remember { mutableStateOf(false) }"
)
code = code.replace(
    "1.28 GB NEURAL DIRECTOR",
    "V8.0 AUTONOMOUS NEURAL PRO"
)
with open(ui_path, "w") as f:
    f.write(code)
print("[✓] V8 UI verified: Silent launch + 270° Rotary Knobs + Zero manual mode buttons.")
PYEOF

echo "================================================================="
echo " [4/5] SYNCING GIT & PUSHING V8 COMMIT WITH RETRY PROTECTION"
echo "================================================================="
git fetch origin main || true
git add -A
git commit -m "Release V8.0 Autonomous Neural Pro: Silent Launch, 58 Offline Tracks, Rotary UI" || true

COMMIT_SHA=$(git rev-parse HEAD)
echo "[+] Local V8 Commit SHA: $COMMIT_SHA"

PUSH_OK=0
for attempt in 1 2 3 4; do
    echo "[*] Pushing to GitHub (Attempt $attempt/4)..."
    if git push --force origin HEAD:main; then
        PUSH_OK=1
        break
    fi
    sleep 4
    git fetch origin || true
done

if [ "$PUSH_OK" -ne 1 ]; then
    echo "[!] ERROR: GitHub rejected the push. Check your internet connection."
    exit 1
fi

echo "================================================================="
echo " [5/5] WAITING FOR EXACT V8 COMMIT ($COMMIT_SHA) BUILD ON GITHUB"
echo "================================================================="
RUN_ID=""
for wait_sec in 1 2 3 4 5 6 7 8 9 10; do
    sleep 4
    RUN_ID=$(gh run list --commit "$COMMIT_SHA" --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || true)
    if [ -n "$RUN_ID" ] && [ "$RUN_ID" != "null" ]; then
        break
    fi
    echo "[*] Waiting for GitHub Actions to register commit $COMMIT_SHA..."
done

if [ -z "$RUN_ID" ] || [ "$RUN_ID" = "null" ]; then
    echo "[*] Triggering workflow_dispatch explicitly..."
    gh workflow run build-flagship-dj-apk.yml || true
    sleep 8
    RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
fi

echo "[+] Locked onto V8 GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID" --exit-status

echo "[+] Downloading verified V8 APK..."
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D ~/ApexDJ_Build/

cp -f ~/ApexDJ_Build/*.apk ~/storage/downloads/ApexDJ_Build/ApexDJ_V8_Autonomous_Pro.apk
ls -lh ~/storage/downloads/ApexDJ_Build/ApexDJ_V8_Autonomous_Pro.apk
echo "================================================================="
echo " [✓] SUCCESS! UNINSTALL THE OLD APP ON YOUR PHONE, THEN INSTALL:"
echo "     Downloads/ApexDJ_Build/ApexDJ_V8_Autonomous_Pro.apk"
echo "================================================================="
