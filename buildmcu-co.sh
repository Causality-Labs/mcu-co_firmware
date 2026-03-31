#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"

usage() {
    echo "Usage: $0 <options>"
    echo ""
    echo "Options:"
    echo "  -b    Configure and build the firmware"
    echo "  -c    Clean build artifacts"
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

if [ $# -eq 0 ]; then
    usage
fi

while getopts "bc" opt; do
    case "${opt}" in
        b) cmd_build ;;
        c) cmd_clean ;;
        *) usage ;;
    esac
done
