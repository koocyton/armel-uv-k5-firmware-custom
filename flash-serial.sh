#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$ROOT_DIR/.tools"
K5PROG_DIR="$TOOLS_DIR/k5prog"
LOCAL_K5PROG="$K5PROG_DIR/k5prog"
DEFAULT_IMAGE="$ROOT_DIR/compiled-firmware/firmware.packed.bin"

usage() {
	cat <<EOF
Usage: $(basename "$0") [serial-port] [firmware.bin]

Examples:
  $(basename "$0") /dev/tty.usbserial-0001
  $(basename "$0") /dev/tty.wchusbserial110 firmware.bin

If serial-port is omitted, the script tries to auto-detect a single USB serial port.

Before flashing:
  1. Turn the radio off.
  2. Hold PTT and turn it on.
  3. Keep it in bootloader mode, then press Enter here.

Notes:
  - This script flashes the raw firmware image, usually firmware.bin.
  - Do not pass firmware.packed.bin; that file is for browser/updater flashing.
EOF
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

file_size() {
	if stat -f%z "$1" >/dev/null 2>&1; then
		stat -f%z "$1"
	else
		stat -c%s "$1"
	fi
}

detect_port() {
	local ports=()
	local pattern

	for pattern in \
		/dev/tty.usbserial* \
		/dev/tty.wchusbserial* \
		/dev/tty.SLAB_USBtoUART* \
		/dev/ttyUSB* \
		/dev/ttyACM*
	do
		for port in $pattern; do
			[[ -e "$port" ]] && ports+=("$port")
		done
	done

	if (( ${#ports[@]} == 1 )); then
		echo "${ports[0]}"
	elif (( ${#ports[@]} == 0 )); then
		die "no USB serial port found; pass it explicitly, e.g. $0 /dev/tty.usbserial-0001"
	else
		printf 'Multiple serial ports found:\n' >&2
		printf '  %s\n' "${ports[@]}" >&2
		die "pass the port explicitly"
	fi
}

ensure_k5prog() {
	if command -v k5prog >/dev/null 2>&1; then
		command -v k5prog
		return
	fi

	if [[ -x "$LOCAL_K5PROG" ]]; then
		echo "$LOCAL_K5PROG"
		return
	fi

	command -v git >/dev/null 2>&1 || die "git is required to download k5prog"
	command -v cc >/dev/null 2>&1 || die "a C compiler is required to build k5prog"

	mkdir -p "$TOOLS_DIR"
	if [[ ! -d "$K5PROG_DIR/.git" ]]; then
		echo "Downloading k5prog..." >&2
		git clone --depth 1 https://github.com/sq5bpf/k5prog.git "$K5PROG_DIR"
	fi

	echo "Building k5prog..." >&2
	cc -O2 -Wall -Wextra -o "$LOCAL_K5PROG" "$K5PROG_DIR/k5prog.c"
	echo "$LOCAL_K5PROG"
}

main() {
	if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
		usage
		exit 0
	fi

	local port="${1:-}"
	local image="${2:-$DEFAULT_IMAGE}"

	[[ -n "$port" ]] || port="$(detect_port)"
	[[ "$image" = /* ]] || image="$ROOT_DIR/$image"
	[[ -e "$port" ]] || die "serial port not found: $port"
	[[ -f "$image" ]] || die "firmware image not found: $image; build it first with make"

	case "$(basename "$image")" in
		*.packed.bin)
			die "k5prog expects raw firmware.bin, not firmware.packed.bin"
			;;
	esac

	local size
	size="$(file_size "$image")"
	(( size > 50000 )) || die "firmware image looks too small (${size} bytes)"
	(( size <= 61440 )) || die "firmware image is too large (${size} bytes; max 61440)"

	local k5prog
	k5prog="$(ensure_k5prog)"

	echo
	echo "Port:  $port"
	echo "Image: $image (${size} bytes)"
	echo
	echo "Put the radio in bootloader mode now: hold PTT, power on, flashlight should be on."
	read -r -p "Press Enter to flash, or Ctrl-C to cancel..."

	"$k5prog" -p "$port" -s 38400 -b "$image" -Y -Y -Y -F
	echo
	echo "Flash command finished. Power-cycle the radio."
}

main "$@"
