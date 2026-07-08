#!/usr/bin/env bash
# sample.sh — Shell script syntax-highlighting sample for the UltraCanvas demo.
set -euo pipefail

readonly BUILD_DIR="${1:-build}"
readonly JOBS="$(nproc 2>/dev/null || echo 4)"

log() {
    printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"
}

configure() {
    log "Configuring in ${BUILD_DIR} ..."
    cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
}

build() {
    log "Building with ${JOBS} job(s) ..."
    cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
}

main() {
    if [[ ! -f CMakeLists.txt ]]; then
        echo "error: run from the project root" >&2
        exit 1
    fi
    configure
    build
    log "Done."
}

main "$@"
