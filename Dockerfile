# =============================================================================
# VA AgentControl — Multi-stage Dockerfile
# Orchestration-only: no local model execution, no CUDA required.
# Model training/inference is handled by the SLM-Models Python service.
# Build:   docker compose build
# Runtime: C++ binary (Crow HTTP :8788 | gRPC :50051)
# =============================================================================
# syntax=docker/dockerfile:1

# -----------------------------------------------------------------------------
# Stage 1: vcpkg bootstrap
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS vcpkg-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc-13 g++-13 cmake ninja-build \
    git curl zip unzip tar \
    pkg-config autoconf automake libtool \
    libssl-dev libcurl4-openssl-dev \
    ca-certificates \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && rm -rf /var/lib/apt/lists/*

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

RUN git clone --depth=1 https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

# -----------------------------------------------------------------------------
# Stage 2: dependency resolution (cached until vcpkg.json changes)
# -----------------------------------------------------------------------------
FROM vcpkg-base AS deps

WORKDIR /app
COPY vcpkg.json ./

RUN ${VCPKG_ROOT}/vcpkg install \
    --triplet=x64-linux \
    --no-print-usage

# -----------------------------------------------------------------------------
# Stage 3: build
# -----------------------------------------------------------------------------
FROM deps AS builder

WORKDIR /app

COPY CMakeLists.txt ./
COPY src/     src/
COPY apps/    apps/
COPY proto/   proto/
COPY config/  config/

RUN cmake -B build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_TESTS=OFF \
    && cmake --build build --parallel "$(nproc)"

# -----------------------------------------------------------------------------
# Stage 4: runtime (minimal image)
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libcurl4 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r prodxcloud && useradd -r -g prodxcloud -s /sbin/nologin prodxcloud

WORKDIR /app

RUN mkdir -p /app/data/vectors /app/logs \
    && chown -R prodxcloud:prodxcloud /app

COPY --from=builder --chown=prodxcloud:prodxcloud \
    /app/build/apps/api_server/prodxcloud_api_server \
    /usr/local/bin/prodxcloud_api_server

COPY --chown=prodxcloud:prodxcloud config/ /etc/prodxcloud/

USER prodxcloud

EXPOSE 8788 50051 9090

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD curl -sf http://127.0.0.1:8788/health || exit 1

ENTRYPOINT ["prodxcloud_api_server"]
CMD ["/etc/prodxcloud/server.yaml"]
