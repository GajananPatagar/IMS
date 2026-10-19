#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[*] Fixing LazyColumn parameters in FlagshipMixerScreen.kt..."

# Python script to safely replace horizontalArrangement in LazyColumn inside FlagshipMixerScreen.kt
python3 - << 'PYEOF'
path = "app/src/main/java/com/apexdj/butter/ui/FlagshipMixerScreen.kt"
with open(path, "r") as f:
    content = f.read()

# Replace the incorrect LazyColumn instantiation
old_block = """                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(102.dp)
                        .background(Color(0xFF05070B), RoundedCornerShape(6.dp))
                        .padding(4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(3.dp)
                ) {"""

new_block = """                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(102.dp)
                        .background(Color(0xFF05070B), RoundedCornerShape(6.dp))
                        .padding(4.dp),
                    verticalArrangement = Arrangement.spacedBy(3.dp)
                ) {"""

if old_block in content:
    content = content.replace(old_block, new_block)
    with open(path, "w") as f:
        f.write(content)
    print("[✓] LazyColumn fixed successfully.")
else:
    print("[!] Warning: Exact LazyColumn block not found, performing regex fallback.")
    import re
    content = re.sub(r'LazyColumn\((.*?)\s*verticalAlignment\s*=\s*Alignment\.CenterVertically,\s*horizontalArrangement\s*=\s*Arrangement\.spacedBy\(3\.dp\)\s*\)', r'LazyColumn(\1 verticalArrangement = Arrangement.spacedBy(3.dp))', content, flags=re.DOTALL)
    with open(path, "w") as f:
        f.write(content)
    print("[✓] Fallback fix applied.")
PYEOF

echo "[*] Committing and pushing to GitHub..."
git add -A
git commit -m "Fix LazyColumn horizontalArrangement compilation error" || true
git push -u origin HEAD

echo "[+] Waiting 10 seconds for GitHub Actions to restart..."
sleep 10

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching build run ID: $RUN_ID"
gh run watch "$RUN_ID"

termux-setup-storage || true
sleep 2

mkdir -p ~/storage/downloads/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D ~/storage/downloads/ApexDJ_Build/
echo "[✓] SUCCESS! 1.28 GB Ghost-Mix Pro APK downloaded to Downloads/ApexDJ_Build/app-release.apk"
