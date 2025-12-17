#!/bin/bash
# Run BlitzOS in QEMU

cd /mnt/c/Users/over9/Desktop/Coding/OS

echo "=========================================="
echo "🚀 BlitzOS - Lightning-Fast OS"
echo "=========================================="
echo ""
echo "[INFO] Starting BlitzOS in QEMU..."
echo "[INFO] ISO: BlitzOS.iso (5.0 MB)"
echo "[INFO] Memory: 128 MB"
echo "[INFO] Boot time: <100ms expected"
echo ""
echo "Press Ctrl+C to stop the emulator"
echo ""

# Run QEMU with VGA output
qemu-system-x86_64 \
    -cdrom BlitzOS.iso \
    -m 128M \
    -serial stdio \
    -name "BlitzOS" \
    -enable-kvm 2>/dev/null || \
qemu-system-x86_64 \
    -cdrom BlitzOS.iso \
    -m 128M \
    -serial stdio \
    -name "BlitzOS"
