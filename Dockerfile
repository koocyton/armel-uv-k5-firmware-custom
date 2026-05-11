# syntax=docker/dockerfile:1.6

# Parametric Alpine tag. Override at build time with:
#   docker build --build-arg ALPINE_TAG=3.21 -t uvk5 .
# Examples: 3.22, 3.21, 3.19, edge
ARG ALPINE_TAG=3.21
FROM alpine:${ALPINE_TAG}

# Toolchain and utilities needed to build the firmware
RUN apk add --no-cache \
      bash \
      build-base \
      gcc-arm-none-eabi \
      newlib-arm-none-eabi \
      python3 \
      py3-crcmod \
      py3-pip \
      git

# Project workspace
WORKDIR /app

# Copy sources into the image (the script mounts the repo and runs builds)
COPY . .

# Strip any build products that slipped past .dockerignore (host incremental builds
# must not poison LTO / conditional EEPROM layout).
RUN find /app \( -name '*.o' -o -name '*.d' \) -delete 2>/dev/null || true \
    && find /app -maxdepth 1 -type f \( \
         -name '*.bin' -o -name '*.packed.bin' -o -name 'f4hwn' -o -name 'firmware' \
         -o -name 'f4hwn.*' \
       \) -delete 2>/dev/null || true
