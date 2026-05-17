#!/bin/sh

IMAGE_NAME="uvk5"
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
FIRMWARE_DIR="${REPO_ROOT}/compiled-firmware"

# Build in container, copy out with docker cp (no -v; avoids macOS File Sharing /users path errors)
docker_build_and_copy() {
    build_script="$1"
    cid=$(docker run -d "$IMAGE_NAME" /bin/bash -c "$build_script") || exit 1
    exit_code=$(docker wait "$cid")
    if [ "$exit_code" != "0" ]; then
        echo "❌ Build failed (container exit $exit_code)"
        docker logs "$cid" 2>&1 | tail -80
        docker rm -f "$cid" >/dev/null 2>&1
        exit 1
    fi
    mkdir -p "$FIRMWARE_DIR"
    docker cp "$cid:/app/compiled-firmware/." "$FIRMWARE_DIR/"
    docker rm -f "$cid" >/dev/null 2>&1
    if [ -z "$(ls -A "$FIRMWARE_DIR" 2>/dev/null)" ]; then
        echo "❌ Build finished but $FIRMWARE_DIR is empty"
        exit 1
    fi
    echo "✅ Output: $FIRMWARE_DIR"
    ls -la "$FIRMWARE_DIR"
}
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

# Clean up old Docker artifacts
echo "🧽 Cleaning up old Docker artifacts..."
docker system prune -f --volumes >/dev/null 2>&1 || true

# Always rebuild the Docker image to ensure latest code changes
echo "⚙️ Rebuilding Docker image '$IMAGE_NAME' (base=${BASE})..."
docker rmi "$IMAGE_NAME" 2>/dev/null || true
if ! docker build --pull --build-arg "ALPINE_TAG=${ALPINE_TAG}" -t "$IMAGE_NAME" .; then
    echo "❌ Failed to build docker image"
    exit 1
fi

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
    docker_build_and_copy "\
        rm -f ./compiled-firmware/* && cd /app && make -s \
        EDITION_STRING=Custom \
        TARGET=f4hwn.custom \
        && cp f4hwn.custom* compiled-firmware/"
}

standard() {
    echo "📦 Compiling Standard..."
    docker_build_and_copy "\
        rm -f ./compiled-firmware/* && cd /app && make -s \
        ENABLE_SPECTRUM=0 \
        ENABLE_FMRADIO=0 \
        ENABLE_AIRCOPY=0 \
        ENABLE_NOAA=0 \
        EDITION_STRING=Standard \
        TARGET=f4hwn.standard \
        && cp f4hwn.standard* compiled-firmware/"
}

bandscope() {
    echo "📺 Compiling Bandscope..."
    docker_build_and_copy "\
        rm -f ./compiled-firmware/* && cd /app && make -s \
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
        TARGET=f4hwn.bandscope \
        && cp f4hwn.bandscope* compiled-firmware/"
}

broadcast() {
    echo "📻 Compiling Broadcast..."
    docker_build_and_copy "\
        cd /app && make -s \
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
        TARGET=f4hwn.broadcast \
        && cp f4hwn.broadcast* compiled-firmware/"
}

si4732() {
    echo "📻 Compiling Si4732 (FM/AM/SSB)..."
    docker_build_and_copy "\
        rm -f ./compiled-firmware/* && cd /app && make -s \
        ENABLE_SPECTRUM=0 \
        ENABLE_FEAT_F4HWN_SPECTRUM=0 \
        ENABLE_FMRADIO=1 \
        ENABLE_VOX=0 \
        ENABLE_SI4732=1 \
        ENABLE_AIRCOPY=0 \
        ENABLE_UART=1 \
        ENABLE_FEAT_F4HWN_SCREENSHOT=0 \
        ENABLE_FEAT_F4HWN_GAME=0 \
        ENABLE_FEAT_F4HWN_PMR=1 \
        ENABLE_FEAT_F4HWN_GMRS_FRS_MURS=1 \
        ENABLE_NOAA=0 \
        ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
        EDITION_STRING=Si4732 \
        TARGET=f4hwn.si4732 \
        && cp f4hwn.si4732* compiled-firmware/"
}

basic() {
    echo "☘️ Compiling Basic..."
    docker_build_and_copy "\
        cd /app && make -s \
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
        TARGET=f4hwn.basic \
        && cp f4hwn.basic* compiled-firmware/"
}

rescueops() {
    echo "🚨 Compiling RescueOps..."
    docker_build_and_copy "\
        cd /app && make -s \
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
        TARGET=f4hwn.rescueops \
        && cp f4hwn.rescueops* compiled-firmware/"
}

game() {
    echo "🎮 Compiling Game..."
    docker_build_and_copy "\
        cd /app && make -s \
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
        TARGET=f4hwn.game \
        && cp f4hwn.game* compiled-firmware/"
}

# ------------------ MENU ------------------

case "$1" in
    clean) clean ;;
    custom) custom ;;
    standard) standard ;;
    bandscope) bandscope ;;
    broadcast) broadcast ;;
    si4732) si4732 ;;
    basic) basic ;;
    rescueops) rescueops ;;
    game) game ;;
    all)
        bandscope
        broadcast
        basic
        rescueops
        game
        ;;
    *)
        echo "Usage: BASE=alpine:<tag> $0 {clean|custom|standard|bandscope|broadcast|si4732|basic|rescueops|game|all}"
        echo "Examples: BASE=alpine:3.22 … | BASE=alpine:3.21 … | BASE=alpine:3.19 … | BASE=alpine:edge …"
        exit 1
        ;;
esac
