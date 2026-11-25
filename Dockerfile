# qalcBot - Docker image
#
# Build (native arch):
#   docker build -t qalcbot .
#
# Run (the Discord bot token is read from the environment at runtime):
#   docker run --rm -e DISCORD_BOT_TOKEN=<token> qalcbot
#
# The image is built in two stages: a Debian trixie builder stage compiles the
# C++20 CMake project against libqalculate and D++ (dpp), and a slim runtime
# stage carries only the shared library runtime and the resulting binary.
# The build is architecture-neutral: the CI workflow builds it for
# linux/amd64 and linux/arm64 from the same Dockerfile.

FROM debian:trixie AS builder
WORKDIR /build
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        libqalculate-dev \
        git \
        ca-certificates \
        libssl-dev \
        zlib1g-dev \
        libzstd-dev \
    && rm -rf /var/lib/apt/lists/*
# libqalculate-dev hard-depends on libcurl4-gnutls-dev, so the curl headers used
# for Qalculate!'s exchange-rate fetching are installed automatically.
# libssl-dev and zlib1g-dev are dpp's own build requirements. libopus is
# deliberately NOT installed: it disables dpp's voice/MLS support, which this
# bot does not use, and avoids the vendored mlspp static-library link step.
COPY CMakeLists.txt ./
COPY src ./src
# dpp v10.1.2 is fetched by CMake (FetchContent) when no system dpp is present.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && cp build/qalcbot /qalcbot

FROM debian:trixie
WORKDIR /app
# The binary links dpp (and, via dpp's static build, OpenSSL and zlib)
# statically; the runtime image needs the dynamically linked libqalculate
# and, because Debian trixie's static libcrypto.a is built with zstd
# support, libzstd1 as well. libqalculate23 provides libqalculate.so and
# pulls in libqalculate-data, which installs the unit/function database at
# /usr/share/qalculate; ca-certificates is needed for exchange-rate TLS.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libqalculate23 \
        libzstd1 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /qalcbot /app/qalcbot
RUN chmod +x /app/qalcbot
ENTRYPOINT ["/app/qalcbot"]
