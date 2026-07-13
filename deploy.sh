#!/bin/bash
set -euo pipefail

# =============================================================================
# Deploy Script — Build + Start in one command
# Usage:  chmod +x deploy.sh && ./deploy.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'
log()  { echo -e "${GREEN}[DEPLOY]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Load .env
if [ -f "$SCRIPT_DIR/.env" ]; then
    set -a; source "$SCRIPT_DIR/.env"; set +a
fi

log "Step 1/3: Building image..."
"$SCRIPT_DIR/build.sh"

log "Step 2/3: Starting services..."
"$SCRIPT_DIR/start.sh"

log "Step 3/3: Verifying health..."
sleep 10
PORT="${HTTP_PORT:-80}"
if curl -sf "http://localhost:${PORT}/health" > /dev/null 2>&1; then
    log "Health check PASSED — engine is live on port ${PORT}"
else
    warn "Health check pending — container may still be initialising."
    warn "Check: docker compose logs -f api-server"
fi

log "Deploy complete!"
echo ""
echo "  REST API:  http://localhost:${HTTP_PORT:-80}"
echo "  gRPC:      localhost:50051"
echo "  Metrics:   http://localhost:${PROMETHEUS_PORT:-9090}"
echo "  Logs:      docker compose logs -f api-server"
