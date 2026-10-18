"""Encode engine-rendered frames; the source is not a recording of the user's desktop."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("preview", type=Path)
parser.add_argument("--ffmpeg", required=True)
args = parser.parse_args()
subprocess.run([args.ffmpeg, "-y", "-framerate", "30", "-i", str(args.preview / "frames" / "frame-%05d.png"),
                "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                str(args.preview / "G1-G5-preview.mp4")], check=True)
