#!/bin/bash
# Extract translatable strings from custom sources
# Usage: Run this script when you add/change qsTr() strings in custom/ sources

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

LUPDATE="${LUPDATE:-/home/glados/Qt/6.8.3/gcc_64/bin/lupdate}"

echo "Extracting custom translatable strings..."
"$LUPDATE" ../src ../res -ts custom.ts -no-obsolete

echo "Done. Now update custom_*.ts files with translations."
