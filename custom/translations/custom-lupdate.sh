#!/usr/bin/env bash
# Extract translatable strings from custom sources and update locale catalogs.
# Usage: Run this script when qsTr() strings or QML file locations change.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ -z "${LUPDATE:-}" ]]; then
    LUPDATE="$(command -v lupdate || true)"
fi

if [[ -z "$LUPDATE" || ! -x "$LUPDATE" ]]; then
    echo "Error: lupdate was not found. Add it to PATH or set LUPDATE to its executable path." >&2
    exit 1
fi

SOURCE_PATHS=(
    "$SCRIPT_DIR/../src"
    "$SCRIPT_DIR/../res"
)

shopt -s nullglob
LOCALE_TS_FILES=("$SCRIPT_DIR"/custom_*.ts)
shopt -u nullglob

echo "Updating custom translation template..."
"$LUPDATE" "${SOURCE_PATHS[@]}" -ts "$SCRIPT_DIR/custom.ts" -no-obsolete

if ((${#LOCALE_TS_FILES[@]})); then
    echo "Updating custom locale catalogs..."
    "$LUPDATE" "${SOURCE_PATHS[@]}" -ts "${LOCALE_TS_FILES[@]}" -no-obsolete
fi

echo "Done. Review translated catalogs for entries marked type=\"unfinished\"."
