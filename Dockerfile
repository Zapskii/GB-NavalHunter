FROM debian:bookworm-slim

ARG GBDK_RELEASE=4.5.0
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl make \
  && rm -rf /var/lib/apt/lists/*

# Prebuilt GBDK-2020 toolchain (lcc, sdcc, linker, romusage)
RUN set -e; \
    case "$(uname -m)" in \
      x86_64) GBDK_ARCH=linux64 ;; \
      aarch64) GBDK_ARCH=linux-arm64 ;; \
    esac; \
    curl -L "https://github.com/gbdk-2020/gbdk-2020/releases/download/${GBDK_RELEASE}/gbdk-${GBDK_ARCH}.tar.gz" \
    | tar -xz -C /opt
ENV GBDK_HOME=/opt/gbdk

# python3 only for mkgfx.py; plain gcc for the host-side placement tests
RUN apt-get update && apt-get install -y --no-install-recommends python3 gcc libc6-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
ENV TMPDIR=/tmp