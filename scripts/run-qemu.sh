#!/bin/bash
# Boot SYPAS in QEMU under OVMF.
#
# Usage: run-qemu.sh [lowend|normal|upper] [iso] [extra qemu args...]
#
# Profiles mirror docs/hardware-target.md:
#   lowend : 2 CPUs, 2 GiB
#   normal : 4 CPUs, 4 GiB
#   upper  : 8 CPUs, 8 GiB

set -eu

PROFILE="${1:-normal}"
ISO="${2:-release/sypas-0.1.0.iso}"
shift $(( $# > 2 ? 2 : $# )) || true

QEMU="${QEMU:-$HOME/sysroot/bin/qemu-system-x86_64}"
OVMF_CODE="${OVMF_CODE:-$HOME/firmware/OVMF_CODE.fd}"
OVMF_VARS="${OVMF_VARS:-$HOME/firmware/OVMF_VARS.fd}"

case "$PROFILE" in
    lowend) SMP=2; MEM=2048 ;;
    normal) SMP=4; MEM=4096 ;;
    upper)  SMP=8; MEM=8192 ;;
    *) echo "unknown profile: $PROFILE" >&2; exit 1 ;;
esac

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
