#!/usr/bin/env bash
set -euo pipefail

# Repo root: canonical path so Docker Desktop (macOS) matches File Sharing
# entries like /Users/... — a lowercase /users/... PWD often fails to mount.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "${REPO_ROOT}"

# ---------------------------------------------
# Usage:
#   ./compile-with-docker.sh [Preset] [CMake options...]
# Examples:
#   ./compile-with-docker.sh Custom
#   ./compile-with-docker.sh Bandscope -DENABLE_SPECTRUM=ON
#   ./compile-with-docker.sh Broadcast -DENABLE_FEAT_F4HWN_GAME=ON -DENABLE_NOAA=ON
#   ./compile-with-docker.sh All
# Default preset: "Custom"
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
PRESET=${1:-Custom}
shift || true  # remove preset from arguments if present

# Any remaining args will be treated as CMake cache variables
EXTRA_ARGS=("$@")

# ---------------------------------------------
# Validate preset name
# ---------------------------------------------
if [[ ! "$PRESET" =~ ^(Custom|Bandscope|Broadcast|Basic|RescueOps|Game|Si4732|Fusion|All)$ ]]; then
  echo "❌ Unknown preset: '$PRESET'"
  echo "Valid presets are: Custom, Bandscope, Broadcast, Basic, RescueOps, Game, Si4732, Fusion, All"
  exit 1
fi

# ---------------------------------------------
# Build the Docker image (only needed once)
# ---------------------------------------------
if [[ "$(docker images -q $IMAGE)" == "" ]]; then
  echo "Building Docker image..."
  docker build -t "$IMAGE" .
fi

# ---------------------------------------------
# Clean existing CMake cache to ensure toolchain reload
# ---------------------------------------------
rm -rf build
export MSYS_NO_PATHCONV=1
# ---------------------------------------------
# Function to build one preset
# ---------------------------------------------
build_preset() {
  local preset="$1"
  local -a docker_tty=()
  # -t needs a real TTY; Cursor/CI often fail with "the input device is not a TTY"
  if [[ -t 0 && -t 1 ]]; then
    docker_tty=(-it)
  fi
  echo ""
  echo "=== 🚀 Building preset: ${preset} ==="
  echo "---------------------------------------------"
  # Pass -D... via "$@" — values like ...PA14=ON contain ">ON", which breaks bash -c "...".
  docker run --rm \
    -u $(id -u):$(id -g) \
    "${docker_tty[@]}" -v "$REPO_ROOT":/src -w /src "$IMAGE" \
    bash -c '
      set -e
      preset="$1"
      shift
      which arm-none-eabi-gcc
      arm-none-eabi-gcc --version
      cmake --preset "$preset" "$@"
      cmake --build --preset "$preset" -j
    ' _ "${preset}" "${EXTRA_ARGS[@]}"

  local outdir="${REPO_ROOT}/build/${preset}"
  local fw_bin
  fw_bin=$(find "${outdir}" -maxdepth 1 -name 'f4hwn.*.bin' -print -quit 2>/dev/null || true)
  if [[ -z "${fw_bin}" ]]; then
    echo "❌ No firmware .bin in ${outdir} — build step did not complete."
    exit 1
  fi
  echo "📦 Firmware: ${fw_bin}"
  echo "✅ Done: ${preset}"
}

# ---------------------------------------------
# Handle 'All' preset
# ---------------------------------------------
if [[ "$PRESET" == "All" ]]; then
  PRESETS=(Bandscope Broadcast Basic RescueOps Game Fusion)
  for p in "${PRESETS[@]}"; do
    build_preset "$p"
  done
  echo ""
  echo "🎉 All presets built successfully!"
else
  build_preset "$PRESET"
fi
