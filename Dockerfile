# ==============================================================================
# Dockerfile for QCAP2 Build and Test Environment
# Supports Ubuntu 24.04 currently. Designed for future ARM64 & custom toolchains.
# ==============================================================================

# Allow base image to be overridden via build-args (e.g. for ARM64 or customized bases)
ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE}

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install core build dependencies, FFmpeg development libraries, and Boost
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    make \
    g++ \
    git \
    pkg-config \
    libboost-all-dev \
    ffmpeg \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libavfilter-dev \
    libfreetype-dev \
    libharfbuzz-dev \
    libdrm-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Configure git to trust the workspace directory system-wide
RUN git config --system --add safe.directory '*'

# Set up working directory
WORKDIR /workspace

# Copy codebase into the container
COPY . /workspace

# Configure, build, and run the test suite via CTest
CMD ["sh", "-c", "mkdir -p build && cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure"]
