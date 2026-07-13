#!/bin/bash
set -euo pipefail

# =============================================================================
# Start Script — Start all services (detached)
# Usage:  chmod +x start.sh && ./start.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
NC='\033[0m'
log() { echo -e "${GREEN}[START]${NC} $1"; }

# Load .env
if [ -f "$SCRIPT_DIR/.env" ]; then
    set -a; source "$SCRIPT_DIR/.env"; set +a
fi

log "Starting ProdxCloud AI Engine stack..."
docker compose -f "$SCRIPT_DIR/docker-compose.yml" up -d

log "Container status:"
docker compose -f "$SCRIPT_DIR/docker-compose.yml" ps

log "Endpoints:"
echo "  REST API:  http://localhost:${HTTP_PORT:-80}"
echo "  gRPC:      localhost:50051"
echo "  Metrics:   http://localhost:${PROMETHEUS_PORT:-9090}"
echo "  Logs:      docker compose logs -f api-server"
