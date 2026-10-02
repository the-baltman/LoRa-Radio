#!/usr/bin/env bash
# Reproducible build of the firmware with pinned versions. Usage:
#   scripts/build.sh            # compile only
#   scripts/build.sh --flash COM5
# Needs arduino-cli >= 1.0 on PATH (or ARDUINO_CLI=...). First run downloads the core (~600 MB).
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cli="${ARDUINO_CLI:-arduino-cli}"
build="$root/build"
mkdir -p "$build/sketchbook/libraries" "$build/dl"

# Pinned versions (see README.md "Versions"). Change them only after re-running tests/host and a board test.
CORE="esp32:esp32@3.3.0"
FQBN="esp32:esp32:esp32"          # ESP32 Dev Module (Steren ESP32-WROOM DevKit, USB-C, CP2102)
LIBS=("RadioLib@7.5.0" "esp32_codec2@1.0.7" "U8g2@2.36.19")

data_dir="${ARDUINO_DATA:-}"
{
  echo "directories:"
  [ -n "$data_dir" ] && echo "  data: $data_dir"
  echo "  user: $build/sketchbook"
  echo "  downloads: $build/dl"
} > "$build/arduino-cli.yaml"
cfg=(--config-file "$build/arduino-cli.yaml")

"$cli" "${cfg[@]}" core update-index
"$cli" "${cfg[@]}" core install "$CORE"
for l in "${LIBS[@]}"; do "$cli" "${cfg[@]}" lib install "$l"; done

"$cli" "${cfg[@]}" compile --fqbn "$FQBN" --build-path "$build/fw" "$root/firmware/lora-radio" 2>&1 | tee "$build/compile.log" | tail -n 4

if [ "${1:-}" = "--flash" ]; then
  "$cli" "${cfg[@]}" upload --fqbn "$FQBN" -p "${2:?serial port}" --build-path "$build/fw" "$root/firmware/lora-radio"
fi
