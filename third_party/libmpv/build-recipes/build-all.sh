#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
python3 extract-sources.py
bash build-engine.sh
bash build-renderer.sh
bash build-mpv.sh
