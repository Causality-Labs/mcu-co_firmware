#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build-tests"

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
    echo "  -t [filter]  Build and run the host-native unit test suite (tests/unit-tests)."
    echo "                 With filter (GroupName or GroupName:TestName), run just that"
    echo "                 group/test instead of the full suite."
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

cmd_test() {
    cmake -S "${SCRIPT_DIR}/tests/unit-tests" -B "${TEST_BUILD_DIR}"
    cmake --build "${TEST_BUILD_DIR}"
    ctest --test-dir "${TEST_BUILD_DIR}" --verbose
}

# Distinct from 1 so a filter that matches nothing in one binary is not confused
# with a real assertion failure in it.
FILTER_MATCHED_NOTHING=2

# CppUTest exits nonzero when a filter selects 0 tests, which is expected for
# whichever binary doesn't hold the requested group. Its "ran nothing" banner is
# the only thing separating that from a genuine failure.
run_filtered() {
    local binary="$1"
    shift

    local output=""
    local result=0
    output=$("${binary}" "$@" -v 2>&1) || result=$?

    # Swallow the "test run failed" banner in this case - it reads as an error
    # but only means the group lives in the other binary.
    if [ "${result}" -ne 0 ] && [[ "${output}" == *"ran nothing"* ]]; then
        echo "$(basename "${binary}"): no match, skipped."
        return "${FILTER_MATCHED_NOTHING}"
    fi

    printf '%s\n' "${output}"

    # Not "${result}": CppUTest exits with the number of failures, which could
    # itself be FILTER_MATCHED_NOTHING.
    if [ "${result}" -ne 0 ]; then
        return 1
    fi

    return 0
}

cmd_test_single() {
    local filter="$1"
    local group="${filter%%:*}"
    local name=""
    if [[ "${filter}" == *:* ]]; then
        name="${filter#*:}"
    fi

    cmake -S "${SCRIPT_DIR}/tests/unit-tests" -B "${TEST_BUILD_DIR}"
    cmake --build "${TEST_BUILD_DIR}"

    local cpputest_args=(-g "${group}")
    if [ -n "${name}" ]; then
        cpputest_args+=(-n "${name}")
    fi

    local failed=0
    local matched=0

    for binary in unit_tests test_command_dispatcher; do
        local result=0
        run_filtered "${TEST_BUILD_DIR}/${binary}" "${cpputest_args[@]}" || result=$?
        case "${result}" in
            0) matched=1 ;;
            "${FILTER_MATCHED_NOTHING}") ;;
            *) failed=1 ;;
        esac
    done

    if [ "${failed}" -ne 0 ]; then
        echo "Test failures in '${filter}'." >&2
        return 1
    fi

    if [ "${matched}" -eq 0 ]; then
        echo "No tests matched '${filter}' in any test binary." >&2
        return 1
    fi
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

while getopts "bcf:Fl:s:th" opt; do
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
        t)
            if [[ -n "${!OPTIND-}" && "${!OPTIND}" != -* ]]; then
                cmd_test_single "${!OPTIND}"
                OPTIND=$((OPTIND + 1))
            else
                cmd_test
            fi
            ;;
        h)
            usage
            ;;
        *)
            usage
            ;;
    esac
done
