#!/bin/sh

IMAGE_NAME="uvk5"
FIRMWARE_DIR="${PWD}/compiled-firmware"
# Default: Alpine 3.21; you can pass BASE=alpine:3.22 / alpine:3.19 / alpine:edge
BASE="${BASE:-alpine:3.22}"

# --- Derive the Alpine tag from BASE ---
case "$BASE" in
  alpine:*)  ALPINE_TAG="${BASE#alpine:}";;
  alpine)    ALPINE_TAG="3.22";;  # fallback if no tag provided
  *)
    echo "❌ BASE must be 'alpine:<tag>' (e.g., alpine:3.21, alpine:edge). Got: '$BASE'"
    exit 1
    ;;
esac

# Create firmware output directory if it doesn't exist
mkdir -p "$FIRMWARE_DIR"

# Clean previously compiled firmware files
rm -f "$FIRMWARE_DIR"/*

# -------------------- IMAGE ---------------------

ensure_image() {
    if [ "${REBUILD:-0}" = "1" ]; then
        docker rmi "$IMAGE_NAME" 2>/dev/null || true
    fi
    if docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        echo "⚙️ Using existing Docker image '$IMAGE_NAME' (REBUILD=1 to rebuild)"
        return 0
    fi
    echo "⚙️ Building Docker image '$IMAGE_NAME' (base=${BASE})..."
    if ! docker build --build-arg "ALPINE_TAG=${ALPINE_TAG}" -t "$IMAGE_NAME" .; then
        echo "❌ Failed to build docker image"
        exit 1
    fi
}

# Mount the host tree over /app so source edits compile without rebuilding the
# image, and so stale image-side *.d files (e.g. /src/printf_config.h) are not used.
compile_in_docker() {
    target_glob="$1"
    shift
    docker run --rm \
        -v "$PWD:/app" \
        -w /app \
        "$IMAGE_NAME" \
        /bin/bash -c "make -s clean $* && make -s $* && cp ${target_glob}* compiled-firmware/"
}

# -------------------- CLEAN ALL ---------------------

clean() {
    echo "🧽 Cleaning all"
    docker rmi "$IMAGE_NAME" 2>/dev/null || true
    docker buildx prune -f || true
    # Optional: if you use buildx history tooling
    if command -v docker >/dev/null 2>&1 && docker buildx help history >/dev/null 2>&1; then
      docker buildx history ls | awk 'NR>1 {print $1}' | xargs docker buildx history rm || true
    fi
    make clean || true
}

# ------------------ BUILD VARIANTS ------------------

custom() {
    echo "🔧 Compiling Custom..."
    compile_in_docker f4hwn.custom \
        EDITION_STRING=Custom \
        TARGET=f4hwn.custom
}

standard() {
    echo "📦 Compiling Standard..."
    compile_in_docker f4hwn.standard \
        ENABLE_SPECTRUM=0 \
        ENABLE_FMRADIO=0 \
        ENABLE_AIRCOPY=0 \
        ENABLE_NOAA=0 \
        EDITION_STRING=Standard \
        TARGET=f4hwn.standard
}

bandscope() {
    echo "📺 Compiling Bandscope..."
    compile_in_docker f4hwn.bandscope \
        ENABLE_SPECTRUM=1 \
        ENABLE_FMRADIO=0 \
        ENABLE_VOX=0 \
        ENABLE_AIRCOPY=1 \
        ENABLE_FEAT_F4HWN_SCREENSHOT=1 \
        ENABLE_FEAT_F4HWN_GAME=0 \
        ENABLE_FEAT_F4HWN_PMR=1 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 \
        ENABLE_NOAA=0 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
        EDITION_STRING=Bandscope \
        TARGET=f4hwn.bandscope
}

si4732() {
    echo "📻 Compiling Si4732..."
    compile_in_docker f4hwn.si4732 \
        ENABLE_SI4732=1 \
        ENABLE_FMRADIO=1 \
        ENABLE_SPECTRUM=0 \
        ENABLE_VOX=0 \
        ENABLE_AIRCOPY=0 \
        ENABLE_AUDIO_BAR=0 \
        ENABLE_RSSI_BAR=0 \
        ENABLE_FLASHLIGHT=0 \
        ENABLE_TX1750=0 \
        ENABLE_KEEP_MEM_NAME=1 \
        ENABLE_BIG_FREQ=1 \
        ENABLE_SMALL_BOLD=0 \
        ENABLE_FEAT_F4HWN_SCREENSHOT=0 \
        ENABLE_FEAT_F4HWN_GAME=0 \
        ENABLE_FEAT_F4HWN_SLEEP=0 \
        ENABLE_FEAT_F4HWN_RESUME_STATE=0 \
        ENABLE_FEAT_F4HWN_NARROWER=0 \
        ENABLE_FEAT_F4HWN_INV=0 \
        ENABLE_FEAT_F4HWN_CTR=0 \
        ENABLE_FEAT_F4HWN_PMR=0 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=0 \
        ENABLE_FEAT_F4HWN_RX_TX_TIMER=0 \
        ENABLE_NOAA=0 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
        EDITION_STRING=Si4732 \
        TARGET=f4hwn.si4732
}

broadcast() {
    echo "📻 Compiling Broadcast..."
    compile_in_docker f4hwn.broadcast \
        ENABLE_SPECTRUM=0 \
        ENABLE_FMRADIO=1 \
        ENABLE_VOX=1 \
        ENABLE_AIRCOPY=1 \
        ENABLE_FEAT_F4HWN_SCREENSHOT=1 \
        ENABLE_FEAT_F4HWN_GAME=0 \
        ENABLE_FEAT_F4HWN_PMR=1 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 \
        ENABLE_NOAA=0 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
        EDITION_STRING=Broadcast \
        TARGET=f4hwn.broadcast
}

basic() {
    echo "☘️ Compiling Basic..."
    compile_in_docker f4hwn.basic \
        ENABLE_SPECTRUM=1 \
        ENABLE_FMRADIO=1 \
        ENABLE_VOX=0 \
        ENABLE_AIRCOPY=0 \
        ENABLE_FEAT_F4HWN_GAME=0 \
        ENABLE_FEAT_F4HWN_SPECTRUM=0 \
        ENABLE_FEAT_F4HWN_PMR=1 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 \
        ENABLE_NOAA=0 \
        ENABLE_AUDIO_BAR=0 \
        ENABLE_FEAT_F4HWN_RESUME_STATE=0 \
        ENABLE_FEAT_F4HWN_CHARGING_C=0 \
        ENABLE_FEAT_F4HWN_INV=1 \
        ENABLE_FEAT_F4HWN_CTR=0 \
        ENABLE_FEAT_F4HWN_NARROWER=1 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
        EDITION_STRING=Basic \
        TARGET=f4hwn.basic
}

rescueops() {
    echo "🚨 Compiling RescueOps..."
    compile_in_docker f4hwn.rescueops \
        ENABLE_SPECTRUM=0 \
        ENABLE_FMRADIO=0 \
        ENABLE_VOX=1 \
        ENABLE_AIRCOPY=1 \
        ENABLE_FEAT_F4HWN_SCREENSHOT=1 \
        ENABLE_FEAT_F4HWN_GAME=0 \
        ENABLE_FEAT_F4HWN_PMR=1 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 \
        ENABLE_NOAA=1 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=1 \
        EDITION_STRING=RescueOps \
        TARGET=f4hwn.rescueops
}

game() {
    echo "🎮 Compiling Game..."
    compile_in_docker f4hwn.game \
        ENABLE_SPECTRUM=0 \
        ENABLE_FMRADIO=1 \
        ENABLE_VOX=0 \
        ENABLE_AIRCOPY=1 \
        ENABLE_FEAT_F4HWN_GAME=1 \
        ENABLE_FEAT_F4HWN_PMR=1 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 \
        ENABLE_NOAA=0 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
        EDITION_STRING=Game \
        TARGET=f4hwn.game
}

# ------------------ MENU ------------------

case "$1" in
    clean) clean ;;
    custom|standard|bandscope|broadcast|basic|rescueops|game|si4732)
        ensure_image
        "$1"
        ;;
    all)
        ensure_image
        bandscope
        broadcast
        basic
        rescueops
        game
        si4732
        ;;
    *)
        echo "Usage: BASE=alpine:<tag> $0 {clean|custom|standard|bandscope|broadcast|basic|rescueops|game|si4732|all}"
        echo "Examples: BASE=alpine:3.22 … | BASE=alpine:3.21 … | BASE=alpine:3.19 … | BASE=alpine:edge …"
        echo "Rebuild image: REBUILD=1 $0 si4732"
        exit 1
        ;;
esac
