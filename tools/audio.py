"""Convert the game's OGG sounds to mono IMA-ADPCM WAV for the 3DS.

Effects and voices go to 16 kHz, music to 22.05 kHz, written under
romfs/sfx/ with the original folder layout in lower case
(SFX/Door/DoorOpen1.ogg -> romfs/sfx/door/dooropen1.wav).

Needs ffmpeg on PATH. Usage: py -3 tools/audio.py [upstream_dir] [romfs_dir]
"""

import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def convert(src, dst, rate):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-ar", str(rate),
           "-c:a", "adpcm_ima_wav", "-block_size", "1024", dst]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return src, r.returncode == 0, r.stderr.strip()


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    upstream = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "upstream")
    romfs = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "romfs")
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg not found on PATH")

    sfx = os.path.join(upstream, "SFX")
    jobs = []
    for dirpath, _dirs, files in os.walk(sfx):
        for name in files:
            if not name.lower().endswith((".ogg", ".wav")):
                continue
            src = os.path.join(dirpath, name)
            rel = os.path.relpath(src, sfx).replace("\\", "/").lower()
            dst = os.path.join(romfs, "sfx", os.path.splitext(rel)[0] + ".wav")
            if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
                continue
            rate = 22050 if rel.startswith("music/") else 16000
            jobs.append((src, dst, rate))

    failed = 0
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for src, ok, err in pool.map(lambda j: convert(*j), jobs):
            if not ok:
                failed += 1
                print(f"failed {src}: {err}")
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(os.path.join(romfs, "sfx")) for f in fs)
    print(f"{len(jobs)} converted, {failed} failed, sfx total {total // (1024 * 1024)} MiB")


if __name__ == "__main__":
    main()
