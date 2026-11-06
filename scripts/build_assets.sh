#!/usr/bin/env bash
# Generates romdisk/*.bin from YOUR copy of the Katana SDK 0.20 Pre.7 and the texture pack.
#   KATANA_SDK=/path/to/sdk  TEXTURE_PACK=/path/to/pack  scripts/build_assets.sh
set -euo pipefail
cd "$(dirname "$0")/.."
T=tools
echo "[1/9] extract scud scene";      python3 $T/extract_scud.py
echo "[2/9] convert course";          python3 $T/convert_world.py
echo "[3/9] course textures (VQ)";    python3 $T/convert_tex.py
echo "[4/9] camera/driving line";     python3 $T/path.py
echo "[5/9] line height";             python3 $T/track_height.py
echo "[6/9] road widths";             python3 $T/path_width.py
echo "[7/9] extract drive2 cars";     python3 $T/extract_drive2.py
echo "[8/9] convert cars";            python3 $T/convert_cars.py
echo "[9/9] HUD font";                python3 $T/font.py
ls -la romdisk
