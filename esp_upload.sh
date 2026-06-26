#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-}"
BOARD="${2:-esp32:esp32:esp32}"
BAUD="${3:-115200}"
SKETCH_DIR="${4:-.}"
BUILD_DIR="${TMPDIR:-/tmp}/smarthome-esp32-build"

usage() {
  cat <<'EOF'
Usage:
  ./esp_upload.sh [port] [fqbn] [baud] [sketch_dir]

Defaults:
  port       auto-detected from /dev/cu.usbserial* or /dev/cu.usbmodem*
  fqbn       esp32:esp32:esp32
  baud       115200
  sketch_dir .

Examples:
  ./esp_upload.sh
  ./esp_upload.sh /dev/cu.usbserial-110
  ./esp_upload.sh /dev/cu.usbserial-110 esp32:esp32:esp32 921600 .
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

find_port() {
  local candidate

  for candidate in /dev/cu.usbserial* /dev/cu.usbmodem* /dev/tty.usbserial* /dev/tty.usbmodem*; do
    if [[ -e "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

if [[ -z "$PORT" ]]; then
  if ! PORT="$(find_port)"; then
    echo "No ESP32 serial port found."
    echo "Run: arduino-cli board list"
    echo "Then retry: ./esp_upload.sh /dev/cu.usbserial-XXXX"
    exit 1
  fi
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli was not found. Install Arduino CLI or upload from Arduino IDE."
  exit 1
fi

echo "Compiling $SKETCH_DIR for $BOARD..."
rm -rf "$BUILD_DIR"
arduino-cli compile --fqbn "$BOARD" --build-path "$BUILD_DIR" "$SKETCH_DIR"

echo "Uploading to $PORT at $BAUD baud..."
arduino-cli upload \
  -p "$PORT" \
  --fqbn "$BOARD" \
  --input-dir "$BUILD_DIR" \
  --upload-property "upload.speed=$BAUD" \
  "$SKETCH_DIR"

echo "Upload complete."
