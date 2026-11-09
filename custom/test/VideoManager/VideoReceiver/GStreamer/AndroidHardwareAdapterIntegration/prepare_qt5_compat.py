"""Prepare the sole Qt 5 compatibility substitution in an out-of-tree copy.

Qt 6 builds compile the production H.265 source directly and need no copy.
Never writes production sources or replaces any decoder/negotiation logic.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", type=Path, required=True)
args = parser.parse_args()
source = (Path(__file__).resolve().parents[5] / "src/VideoManager/VideoReceiver/GStreamer"
          / "AndroidH265HardwareDecoderAdapter.cc")
text = source.read_text(encoding="utf-8")
old = "s_registeredElementFactoryNames = {"
new = "s_registeredElementFactoryNames = QStringList{"
if text.count(old) != 1:
    raise SystemExit("Expected exactly one Qt 5 QStringList assignment; review production change first")
args.build_dir.mkdir(parents=True, exist_ok=True)
target = args.build_dir / "AndroidH265HardwareDecoderAdapter-qt5.cc"
target.write_text(text.replace(old, new), encoding="utf-8")
print(f"Qt 5 compatibility copy: {target.resolve()} (one explicit QStringList constructor)")
