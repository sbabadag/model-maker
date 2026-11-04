#!/usr/bin/env bash
# Git hook kurulumu: WIN32 sozdizimi kapisini pre-push'a baglar.
# Kullanim: bash tools/install_git_hooks.sh
# Islev: tools/hooks/pre-push -> $(git rev-parse --git-path hooks)/pre-push
# Var olan farkli bir pre-push hook'u EZMEZ (uyarir).
set -eu

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
SRC="$ROOT/tools/hooks/pre-push"
DEST_DIR="$(git rev-parse --git-path hooks)"
DEST="$DEST_DIR/pre-push"

[ -f "$SRC" ] || { echo "HATA: $SRC yok" >&2; exit 1; }
mkdir -p "$DEST_DIR"

if [ -f "$DEST" ] && ! cmp -s "$SRC" "$DEST"; then
    echo "UYARI: $DEST zaten var ve farkli; ezilmedi. Elle birlestirin." >&2
    exit 2
fi

cp "$SRC" "$DEST"
chmod 755 "$DEST"
echo "OK: pre-push hook kuruldu -> $DEST"
