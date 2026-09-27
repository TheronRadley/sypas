#!/bin/bash
# Boot SYPAS in QEMU under OVMF.
#
# Usage: run-qemu.sh [single|lowend|normal|upper] [iso] [extra qemu args...]
# Profiles come from tests/config/matrix.json — the single source of truth.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROFILE="${1:-normal}"
if [ "$#" -ge 2 ]; then
    ISO="$2"
else
    VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
    ISO="$ROOT/release/sypas-$VERSION.iso"
fi
shift $(( $# > 2 ? 2 : $# )) || true

resolve() {
    python3 "$ROOT/tools/resolve_paths.py" "$1" 2>/dev/null || true
}

QEMU="${QEMU:-$(resolve qemu)}"
OVMF_CODE="${OVMF_CODE:-$(resolve ovmf-code)}"
OVMF_VARS="${OVMF_VARS:-$(resolve ovmf-vars)}"

if [ ! -x "$QEMU" ] || [ ! -f "$OVMF_CODE" ] || [ ! -f "$OVMF_VARS" ]; then
    echo "QEMU/OVMF unavailable (QEMU=$QEMU OVMF_CODE=$OVMF_CODE OVMF_VARS=$OVMF_VARS)" >&2
    echo "Run 'make doctor' or set QEMU, OVMF_CODE and OVMF_VARS explicitly." >&2
    exit 1
fi

MATRIX="$ROOT/tests/config/matrix.json"
read -r SMP MEM < <(python3 - "$MATRIX" "$PROFILE" <<'EOF'
import json, sys
matrix = json.load(open(sys.argv[1]))
for cfg in matrix["configs"]:
    if cfg["name"] == sys.argv[2]:
        print(cfg["smp"], cfg["mem_mib"])
        break
else:
    sys.exit(f"unknown profile: {sys.argv[2]} "
             f"(known: {[c['name'] for c in matrix['configs']]})")
EOF
)

VARS_TMP=$(mktemp /tmp/sypas-vars.XXXXXX.fd)
cp "$OVMF_VARS" "$VARS_TMP"
trap 'rm -f "$VARS_TMP"' EXIT

exec "$QEMU" \
    -machine q35 -cpu max -smp "$SMP" -m "$MEM" \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$VARS_TMP" \
    -cdrom "$ISO" \
    -serial stdio \
    -display none \
    -no-reboot \
    "$@"
