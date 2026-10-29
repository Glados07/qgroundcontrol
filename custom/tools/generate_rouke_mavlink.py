#!/usr/bin/env python3
"""Regenerate the Rouke MAVLink 2 dialect with QGC's pinned upstream generator.

Usage:
  python custom/tools/generate_rouke_mavlink.py --mavlink-source /path/to/mavlink \
      --library /path/to/c_library_v2 [--check]

The source checkout must have its pymavlink submodule initialized. The output
contains only our additional dialect. Native headers are reused unchanged by
CustomMavlink.cmake. No source checkout or dependency cache is modified.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

LIBRARY_REVISION = "19f9955598af9a9181064619bd2e3c04bd2d848a"
SOURCE_REVISION = "bedbb581c3d124b57aa4211a76886df86f33cd23"
GENERATOR_REVISION = "4960d4d20dcf9fc2981785355567fc5773416bc9"
BUILD_DATE = "Tue Oct 27 2026"
CUSTOM_ROOT = Path(__file__).resolve().parents[1]
XML = CUSTOM_ROOT / "src/MAVLink/rouke.xml"
OUTPUT = CUSTOM_ROOT / "src/MAVLink/generated/rouke"
HEADERS = ("mavlink.h", "rouke.h", "mavlink_msg_vk_payload_data_relay.h",
           "version.h", "testsuite.h")


def revision(path):
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def crc_entries(text):
    match = re.search(r"^#define MAVLINK_MESSAGE_CRCS (.+)$", text, re.MULTILINE)
    if not match:
        raise RuntimeError("Missing generated MAVLINK_MESSAGE_CRCS")
    entries = {}
    for fields in re.findall(r"\{([^{}]+)\}", match.group(1)):
        entry = tuple(int(value.strip()) for value in fields.split(","))
        if entry[0] in entries:
            raise RuntimeError("Duplicate MAVLink message ID")
        entries[entry[0]] = entry
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mavlink-source", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = args.mavlink_source.resolve()
    library = args.library.resolve()
    for path, expected in ((source, SOURCE_REVISION),
                           (source / "pymavlink", GENERATOR_REVISION),
                           (library, LIBRARY_REVISION)):
        if revision(path) != expected:
            raise RuntimeError("Unexpected revision at {} (expected {})".format(path, expected))

    # The pinned generator hashes dialect names with Python's hash(). Fix the
    # seed in a fresh interpreter so generated identity macros are reproducible.
    with tempfile.TemporaryDirectory(prefix="rouke-mavgen-") as scratch:
        scratch = Path(scratch)
        definitions = scratch / "definitions"
        definitions.mkdir()
        for definition in (source / "message_definitions/v1.0").glob("*.xml"):
            shutil.copyfile(definition, definitions / definition.name)
        shutil.copyfile(XML, definitions / XML.name)
        environment = os.environ.copy()
        environment["PYTHONHASHSEED"] = "0"
        environment["PYTHONPATH"] = str(source)
        generated = scratch / "generated"
        worker = (
            "from pymavlink.generator import mavgen; import sys; "
            "opts = mavgen.Opts(sys.argv[1], wire_protocol='2.0', language='C'); "
            "sys.exit(0 if mavgen.mavgen(opts, [sys.argv[2]]) else 1)"
        )
        subprocess.run([sys.executable, "-c", worker, str(generated),
                        str(definitions / XML.name)], env=environment,
                       cwd=str(scratch), check=True)

        generated_dialect = generated / "rouke"
        upstream_header = library / "all/all.h"
        upstream = crc_entries(upstream_header.read_text(encoding="utf-8"))
        combined = crc_entries((generated_dialect / "rouke.h").read_text(encoding="utf-8"))
        relay = combined.pop(53022)
        if relay != (53022, 37, 132, 132, 0, 0, 0):
            raise RuntimeError("Unexpected VK_PAYLOAD_DATA_RELAY wire definition: {}".format(relay))
        if upstream != combined:
            raise RuntimeError("Generated dialect changes the pinned upstream message table")

        outputs = {}
        for name in HEADERS:
            text = (generated_dialect / name).read_text(encoding="utf-8")
            # The generator's wall-clock date is provenance only, not wire data.
            text = re.sub(r'#define MAVLINK_BUILD_DATE "[^"]+"',
                          '#define MAVLINK_BUILD_DATE "{}"'.format(BUILD_DATE), text)
            outputs[name] = text.encode("utf-8")
        manifest = {
            "library_revision": LIBRARY_REVISION,
            "source_revision": SOURCE_REVISION,
            "generator_revision": GENERATOR_REVISION,
            "xml_sha256": hashlib.sha256(XML.read_text(encoding="utf-8").encode("utf-8")).hexdigest(),
            "upstream_all_sha256": hashlib.sha256(upstream_header.read_text(encoding="utf-8").encode("utf-8")).hexdigest(),
            "upstream_message_count": len(upstream),
            "relay_crc_extra": relay[1],
        }
        outputs["generation.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")

        if args.check:
            different = [name for name, content in outputs.items()
                         if not (OUTPUT / name).exists()
                         or (OUTPUT / name).read_text(encoding="utf-8").encode("utf-8") != content]
            if different:
                raise RuntimeError("Regeneration differs: " + ", ".join(different))
            print("Rouke dialect regeneration matches; {} upstream messages preserved.".format(len(upstream)))
        else:
            OUTPUT.mkdir(parents=True, exist_ok=True)
            for name, content in outputs.items():
                (OUTPUT / name).write_bytes(content)
            print("Wrote {} ({} upstream messages plus 53022).".format(OUTPUT, len(upstream)))


if __name__ == "__main__":
    main()
