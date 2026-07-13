#!/bin/bash
set -euo pipefail

# =============================================================================
# Setup Script — Verify prerequisites for deployment
# Run once:  chmod +x setup.sh && ./setup.sh
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'
log()  { echo -e "${GREEN}[SETUP]${NC} $1"; }
err()  { echo -e "${RED}[ERROR]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

log "Checking prerequisites..."

# --- Docker ---
if ! command -v docker &> /dev/null; then
    err "Docker is not installed. Please install Docker first."
    exit 1
fi
if ! docker compose version &> /dev/null; then
    err "Docker Compose plugin not found."
    exit 1
fi
if ! docker info &> /dev/null 2>&1; then
    err "Docker daemon is not running."
    exit 1
fi
log "Docker:          $(docker --version)"
log "Docker Compose:  $(docker compose version)"

# --- NVIDIA GPU (not required — model execution is in SLM-Models service) ---
if command -v nvidia-smi &> /dev/null; then
    log "GPU:             $(nvidia-smi --query-gpu=name --format=csv,noheader | head -1) (not used by this service)"
else
    log "GPU:             not detected (not required — SLM-Models handles inference)"
fi

# --- .env ---
if [ ! -f "$SCRIPT_DIR/.env" ]; then
    warn ".env not found — creating from defaults. Edit it before deploying."
    cp "$SCRIPT_DIR/.env.example" "$SCRIPT_DIR/.env" 2>/dev/null || \
    cat > "$SCRIPT_DIR/.env" << 'EOF'
API_IMAGE=valtunox/agentcontrol:latest
HTTP_PORT=80
HTTPS_PORT=443
PROMETHEUS_PORT=9090
NGINX_CONF=default.conf
SSL_CERTS_PATH=./certs
SLM_SERVICE_URL=http://slm-models:8000
LOG_LEVEL=info
EOF
else
    log ".env:            found"
fi

# --- certs/ ---
CERTS_DIR="$SCRIPT_DIR/certs"
if [ ! -d "$CERTS_DIR" ]; then
    mkdir -p "$CERTS_DIR"
    warn "Created ./certs/ — add SSL cert files for Option B (HTTPS)."
    echo "    Cloudflare Origin Cert: origin-cert.pem + origin-key.pem"
    echo "    Let's Encrypt:          fullchain.pem  + privkey.pem"
else
    log "certs/:          found ($(ls "$CERTS_DIR" | wc -l | tr -d ' ') file(s))"
fi

# --- logs/ ---
mkdir -p "$SCRIPT_DIR/logs"
log "logs/:           ready"

# --- nginx configs ---
if [ ! -f "$SCRIPT_DIR/nginx/default.conf" ] || [ ! -f "$SCRIPT_DIR/nginx/ssl.conf" ]; then
    err "Missing nginx configs in ./nginx/ — expected default.conf and ssl.conf."
    exit 1
fi
log "nginx/:          default.conf + ssl.conf present"

# --- config/ ---
for f in server.yaml models.yaml prometheus.yml; do
    if [ ! -f "$SCRIPT_DIR/config/$f" ]; then
        err "Missing config/$f"
        exit 1
    fi
done
log "config/:         server.yaml + models.yaml + prometheus.yml present"

log "All prerequisites met. Ready to deploy!"
echo ""
echo "  Next: ./deploy.sh"
