#!/bin/bash
set -euo pipefail

# =============================================================================
# Build Script — Build the ProdxCloud AI Engine Docker image
# Usage:  chmod +x build.sh && ./build.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'
log()  { echo -e "${GREEN}[BUILD]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Load .env
if [ -f "$SCRIPT_DIR/.env" ]; then
    set -a; source "$SCRIPT_DIR/.env"; set +a
    log "Loaded .env"
fi

log "Building ProdxCloud AI Engine from: $SCRIPT_DIR"
log "Orchestration-only build (no CUDA, no local model execution)..."

docker compose -f "$SCRIPT_DIR/docker-compose.yml" build --no-cache

log "Build complete!"
docker images | grep "${API_IMAGE%%:*}" || true
