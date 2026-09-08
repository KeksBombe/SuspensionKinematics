#!/usr/bin/env bash
# Rebuild SuspensionKinematics and run it.
#
#   ./start.sh                 rebuild and launch empty
#   ./start.sh part.stl        rebuild and open a file
#   SUSPKIN_PRESET=linux-release ./start.sh
#
# Any extra arguments are passed straight through to the binary.
set -Eeuo pipefail
cd "$(dirname "$0")"

readonly PRESET="${SUSPKIN_PRESET:-linux-debug}"
readonly BUILD_DIR="build/${PRESET}"

# Only configure the first time; cmake --build re-runs configuration by itself
# whenever CMakeLists.txt changes.
if [[ ! -d "${BUILD_DIR}" ]]; then
    cmake --preset "${PRESET}"
fi

cmake --build --preset "${PRESET}"

exec "${BUILD_DIR}/suspkin" "$@"
