#!/usr/bin/env bash
# Host tests for the protocol, state machine and UI logic. Needs g++ (C++17). Run from anywhere.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../firmware/lora-radio/src"
out="$here/out"
SAN="${SANITIZE:+-fsanitize=address,undefined}"
mkdir -p "$out"
g++ -std=c++17 -Wall -Wextra -g $SAN -I"$src" \
  "$here/test_protocol.cpp" "$src/protocol/protocol.cpp" "$src/protocol/node_fsm.cpp" -o "$out/test_protocol"
"$out/test_protocol"
if [ -f "$here/test_ui.cpp" ]; then
  g++ -std=c++17 -Wall -Wextra -g $SAN -I"$src" "$here/test_ui.cpp" -o "$out/test_ui"
  "$out/test_ui"
fi
