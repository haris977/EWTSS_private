#!/usr/bin/env bash
# Render all EWTSS v2 SAD diagrams (.mmd -> .png) with mermaid-cli (mmdc).
# Requires: @mermaid-js/mermaid-cli installed (repo-root node_modules, or global mmdc).
# Usage:  bash docs/architecture/diagrams/render.sh
set -euo pipefail
cd "$(dirname "$0")"

# Resolve mmdc: prefer repo-root local install, else PATH.
if [ -x "../../../node_modules/.bin/mmdc" ]; then
  MMDC="../../../node_modules/.bin/mmdc"
else
  MMDC="mmdc"
fi

CONFIG="mermaid-config.json"
for f in *.mmd; do
  out="${f%.mmd}.png"
  echo "rendering $f -> $out"
  "$MMDC" -i "$f" -o "$out" -c "$CONFIG" -b white -s 3
done
echo "done."
