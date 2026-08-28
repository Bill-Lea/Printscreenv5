#!/bin/bash
# PrintScreen SKSE Plugin Build Script (WSL wrapper)
# Usage: ./build.sh [release|debug] [clean]

CONFIG="Release"
CLEAN=""

for arg in "$@"; do
    case "${arg,,}" in
        debug)   CONFIG="Debug" ;;
        release) CONFIG="Release" ;;
        clean)   CLEAN="-Clean" ;;
    esac
done

echo "=== PrintScreen Build Script (WSL) ==="
echo "Configuration: $CONFIG"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIN_PATH=$(wslpath -w "$SCRIPT_DIR")

powershell.exe -ExecutionPolicy Bypass -File "${WIN_PATH}\\build.ps1" -Config "$CONFIG" $CLEAN

exit $?
