#!/bin/bash
set -euo pipefail

# =============================================================================
# Stop Script — Stop and remove all containers
# Usage:  chmod +x stop.sh && ./stop.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
NC='\033[0m'
log() { echo -e "${GREEN}[STOP]${NC} $1"; }

log "Stopping ProdxCloud AI Engine stack..."
docker compose -f "$SCRIPT_DIR/docker-compose.yml" down

log "Stopped."
