#!/bin/bash
# Xcode's environment contains the expanded SDK/configuration bundle directory.
set -euo pipefail
stage_dir="$(cd "$(dirname "$0")" && pwd)"
: "${TARGET_BUILD_DIR:?Xcode build environment required}"
: "${FULL_PRODUCT_NAME:?Xcode product name required}"
python3 "$stage_dir/stage.py" --source-install "$1" --output "$TARGET_BUILD_DIR/$FULL_PRODUCT_NAME/GameData"
