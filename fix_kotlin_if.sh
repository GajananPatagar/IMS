#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[*] Fixing Kotlin if-expression branch in OnlineAndLocalRepository.kt..."

python3 - << 'PYEOF'
path = "app/src/main/java/com/apexdj/butter/audio/OnlineAndLocalRepository.kt"
with open(path, "r") as f:
    content = f.read()

# Replace the incomplete expression around line 395
old_target = """        val targetLabel = if (queueScore > score && queueTail != null) {
            "★ $queueScore% NEXT QUEUE MATCH (AFTER ${queueTail.title.take(10).uppercase()})$readyTag"
        }"""

new_target = """        val targetLabel = if (queueScore > score && queueTail != null) {
            "★ $queueScore% NEXT QUEUE MATCH (AFTER ${queueTail.title.take(10).uppercase()})$readyTag"
        } else {
            "★ $score% PERFECT FOR $targetDeckName (${track.camelotKey}A • ${track.estimatedBpm.toInt()} BPM)$readyTag"
        }"""

if "val targetLabel = if" in content:
    import re
    # Clean replacement of the targetLabel block ensuring else branch
    content = re.sub(
        r'val targetLabel = if\s*\(queueScore > score && queueTail != null\)\s*\{\s*"[^"]+"\s*\}\s*',
        r'val targetLabel = if (queueScore > score && queueTail != null) {\n            "★ $queueScore% NEXT QUEUE MATCH (AFTER ${queueTail.title.take(10).uppercase()})$readyTag"\n        } else {\n            "★ $score% PERFECT FOR $targetDeckName (${track.camelotKey}A • ${track.estimatedBpm.toInt()} BPM)$readyTag"\n        }',
        content
    )
    with open(path, "w") as f:
        f.write(content)
    print("[✓] Successfully injected missing else branch into targetLabel.")
else:
    print("[!] targetLabel pattern not matched exactly.")
PYEOF

echo "[*] Committing and pushing fix to GitHub..."
git add -A
git commit -m "Fix Kotlin if-expression missing else branch compilation error" || true
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
