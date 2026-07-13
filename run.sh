#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# ProdxCloud AI Engine — native build & run (no Docker, no vcpkg)
# Usage:  bash run.sh [--rebuild]
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CONFIG="$SCRIPT_DIR/config/server.yaml"
BINARY="$BUILD_DIR/apps/api_server/prodxcloud_api_server"

# ── helpers ──────────────────────────────────────────────────────
log()  { echo "[run.sh] $*"; }
die()  { echo "[run.sh] ERROR: $*" >&2; exit 1; }

# ── parse flags ──────────────────────────────────────────────────
REBUILD=false
for arg in "$@"; do
    [[ "$arg" == "--rebuild" ]] && REBUILD=true
done

# ── 1. install system dependencies (skip if already present) ─────
log "Checking system dependencies..."
MISSING=()
for pkg in cmake ninja-build libspdlog-dev nlohmann-json3-dev libyaml-cpp-dev libssl-dev libsqlite3-dev; do
    dpkg -s "$pkg" &>/dev/null || MISSING+=("$pkg")
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    log "Installing missing packages: ${MISSING[*]}"
    sudo apt-get update -qq
    sudo apt-get install -y build-essential "${MISSING[@]}"
else
    log "All system dependencies already installed."
fi

# ── 2. clean stale cache if --rebuild or path mismatch ───────────
if [[ "$REBUILD" == true ]]; then
    log "--rebuild flag set — removing build directory..."
    rm -rf "$BUILD_DIR"
elif [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    CACHED_SRC=$(grep -m1 "^CMAKE_HOME_DIRECTORY" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || true)
    if [[ -n "$CACHED_SRC" && "$CACHED_SRC" != "$SCRIPT_DIR" ]]; then
        log "Stale CMakeCache.txt from a different path ($CACHED_SRC) — cleaning..."
        rm -rf "$BUILD_DIR"
    fi
fi

# ── 3. configure ─────────────────────────────────────────────────
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    log "Configuring (CMake + Ninja, no CUDA, no tests)..."
    cmake -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_CUDA=OFF \
        -DENABLE_TESTS=OFF \
        -S "$SCRIPT_DIR"
else
    log "CMake already configured — skipping (use --rebuild to force)."
fi

# ── 4. build ─────────────────────────────────────────────────────
log "Building..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# ── 5. create local data directories ─────────────────────────────
log "Ensuring local data directories exist..."
mkdir -p "$SCRIPT_DIR/data/vectors" \
         "$SCRIPT_DIR/logs"

# ── 6. run ───────────────────────────────────────────────────────
[[ -f "$BINARY" ]] || die "Binary not found at $BINARY"
[[ -f "$CONFIG" ]] || die "Config not found at $CONFIG"

log "Starting ProdxCloud AI Engine on port 8788..."
log "Config: $CONFIG"
cd "$SCRIPT_DIR"
exec "$BINARY" "$CONFIG"
