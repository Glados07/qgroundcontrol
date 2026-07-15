#!/bin/bash
# Extract translatable strings from custom sources
# Usage: Run this script when you add/change qsTr() strings in custom/ sources

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

LUPDATE="${LUPDATE:-lupdate}"

echo "Extracting custom translatable strings..."
if ! command -v "$LUPDATE" >/dev/null 2>&1; then
    echo "lupdate not found: $LUPDATE" >&2
    echo "Set LUPDATE to the Qt 6 lupdate executable before running this script." >&2
    exit 1
fi

"$LUPDATE" ../src -ts custom.ts -no-obsolete

echo "Done. Now update custom_*.ts files with translations."
