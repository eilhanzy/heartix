#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$SCRIPT_DIR"

ports=(
  "zlib/1.2.8"
  "pixman/0.38.0"
  "libpng/1.6.34"
  "freetype/2.5.3"
  "cairo/1.12.18"
  "ncurses/6.4"
  "htop/3.2.2"
)

for p in "${ports[@]}"; do
  echo "==> building $p"
  bash port.sh "$p"
done

if [ -n "${SYSROOT:-}" ]; then
  touch "${SYSROOT}/system/.ports-built"
fi
