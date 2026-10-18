"""Pack trusted, offline-built assets into one resource; does not download anything."""
import argparse
import json
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
args = parser.parse_args()
manifest = json.loads((args.directory / "manifest.json").read_text(encoding="utf-8"))
names = ["manifest.json", "skeleton.json", "skeleton.ozz"] + [clip["file"] for clip in manifest["clips"]]
data = bytearray(struct.pack("<II", 0x31464142, len(names)))
for name in names:
    if Path(name).name != name:
        raise ValueError("Only flat asset names are supported")
    encoded = name.encode("utf-8")
    payload = (args.directory / name).read_bytes()
    data += struct.pack("<II", len(encoded), len(payload)) + encoded + payload
(args.directory / "foundation.baf").write_bytes(data)
print(f"Packed {len(names)} files, {len(data)} bytes")
