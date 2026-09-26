#!/bin/bash
# Boot SYPAS in QEMU under OVMF.
#
# Usage: run-qemu.sh [single|lowend|normal|upper] [iso] [extra qemu args...]
#
# Profiles come from tests/config/matrix.json — the single source of
# truth for the machine matrix (do not hardcode CPU/RAM values here).

set -eu

PROFILE="${1:-normal}"
ISO="${2:-release/sypas-0.1.0.iso}"
shift $(( $# > 2 ? 2 : $# )) || true

QEMU="${QEMU:-$HOME/sysroot/bin/qemu-system-x86_64}"
OVMF_CODE="${OVMF_CODE:-$HOME/firmware/OVMF_CODE.fd}"
OVMF_VARS="${OVMF_VARS:-$HOME/firmware/OVMF_VARS.fd}"

MATRIX="$(dirname "$0")/../tests/config/matrix.json"
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
