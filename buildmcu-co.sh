#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"

usage() {
    echo "Usage: $0 <options>"
    echo ""
    echo "Options:"
    echo "  -b           Configure and build the firmware"
    echo "  -c           Clean build artifacts"
    echo "  -f <tool>    Flash firmware (tool: st, ocd)"
    echo "  -h           Print this help message"
    exit 0
}

cmd_build() {
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}"
    cmake --build "${BUILD_DIR}"
}

cmd_clean() {
    cmake --build "${BUILD_DIR}" --target clean
    echo "Build artifacts cleaned."
}

cmd_flash() {
    case "$1" in
        st)
            st-flash write "${BUILD_DIR}/mcu-co-firmware.bin" 0x08000000
            ;;
        ocd)
            echo "OpenOCD flashing not configured yet."
            ;;
        *)
            echo "Unknown flash tool: $1. Use stflash or openocd."
            exit 1
            ;;
    esac
}

if [ $# -eq 0 ]; then
    usage
fi

while getopts "bcf:h" opt; do
    case "${opt}" in
        b) cmd_build ;;
        c) cmd_clean ;;
        f) cmd_flash "${OPTARG}" ;;
        h) usage ;;
        *) usage ;;
    esac
done
