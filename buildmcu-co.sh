#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"

usage() {
    echo "Usage: $0 <options>"
    echo ""
    echo "Options:"
    echo "  -b           Configure and build the firmware"
    echo "  -l <level>   Set log verbosity (level: error, warn, info, debug, off; default: debug)"
    echo "                 Must be passed before -b to take effect."
    echo "  -c           Clean build artifacts"
    echo "  -f <tool>    Flash firmware (tool: st, ocd)"
    echo "  -F           Format source files with clang-format"
    echo "  -s <tool>    Run static analysis (tool: clang, cpp, both)"
    echo "  -h           Print this help message"
    exit 0
}

log_args_from_level() {
    case "$1" in
        error)
            echo "-DLOG_ENABLED=1 -DLOG_LEVEL=LOG_LEVEL_ERROR"
            ;;
        warn)
            echo "-DLOG_ENABLED=1 -DLOG_LEVEL=LOG_LEVEL_WARN"
            ;;
        info)
            echo "-DLOG_ENABLED=1 -DLOG_LEVEL=LOG_LEVEL_INFO"
            ;;
        debug)
            echo "-DLOG_ENABLED=1 -DLOG_LEVEL=LOG_LEVEL_DEBUG"
            ;;
        off)
            echo "-DLOG_ENABLED=0 -DLOG_LEVEL=LOG_LEVEL_NONE"
            ;;
        *)
            echo "Unknown log level: $1. Use error, warn, info, debug, or off." >&2
            exit 1
            ;;
    esac
}

cmd_build() {
    local log_args
    log_args=$(log_args_from_level "${LOG_LEVEL_ARG}")
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
        -DENABLE_CERT_CHECK=OFF -DENABLE_CPPCHECK=OFF \
        ${log_args}
    cmake --build "${BUILD_DIR}"
}

cmd_clean() {
    cmake --build "${BUILD_DIR}" --target clean
    echo "Build artifacts cleaned."
}

cmd_static_analysis() {
    case "$1" in
        clang)
            cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DENABLE_CERT_CHECK=ON -DENABLE_CPPCHECK=OFF
            cmake --build "${BUILD_DIR}"
            ;;
        cpp)
            cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DENABLE_CERT_CHECK=OFF -DENABLE_CPPCHECK=ON
            cmake --build "${BUILD_DIR}"
            ;;
        both)
            cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DENABLE_CERT_CHECK=ON -DENABLE_CPPCHECK=ON
            cmake --build "${BUILD_DIR}"
            ;;
        *)
            echo "Unknown static analysis tool: $1. Use clang, cpp, or both."
            exit 1
            ;;
    esac
}

cmd_format() {
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DENABLE_CERT_CHECK=OFF -DENABLE_CPPCHECK=OFF
    cmake --build "${BUILD_DIR}" --target format
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
            echo "Unknown flash tool: $1. Use st or ocd."
            exit 1
            ;;
    esac
}

if [ $# -eq 0 ]; then
    usage
fi

LOG_LEVEL_ARG="debug"

while getopts "bcf:Fl:s:h" opt; do
    case "${opt}" in
        b)
            cmd_build
            ;;
        c)
            cmd_clean
            ;;
        f)
            cmd_flash "${OPTARG}"
            ;;
        F)
            cmd_format
            ;;
        l)
            LOG_LEVEL_ARG="${OPTARG}"
            ;;
        s)
            cmd_static_analysis "${OPTARG}"
            ;;
        h)
            usage
            ;;
        *)
            usage
            ;;
    esac
done
