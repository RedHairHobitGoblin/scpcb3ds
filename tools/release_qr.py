"""Adds a QR code for FBI remote install to a GitHub release.

The QR encodes the release's direct SCPCB3DS.cia download link. It is uploaded
as a release asset and shown at the end of the release notes.

Usage: py -3 tools/release_qr.py <tag>   (needs gh logged in and `pip install qrcode`)
"""

import os
import subprocess
import sys

import qrcode

REPO = "cheapcow787-gif/scpcb3ds"
GH = os.environ.get("GH", r"C:\Users\Computer User\tools\gh\bin\gh.exe")


def main():
    tag = sys.argv[1]
    url = f"https://github.com/{REPO}/releases/download/{tag}/SCPCB3DS.cia"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    png = os.path.join(root, "release", f"qr-{tag}.png")
    qrcode.make(url, box_size=8, border=4).save(png)

    subprocess.run([GH, "release", "upload", tag, png, "--clobber", "--repo", REPO], check=True)
    notes = subprocess.run([GH, "release", "view", tag, "--repo", REPO, "--json", "body", "--jq", ".body"],
                           check=True, capture_output=True, text=True).stdout.rstrip()
    marker = "## Install with FBI (QR)"
    if marker not in notes:
        img = f"https://github.com/{REPO}/releases/download/{tag}/qr-{tag}.png"
        notes += (f"\n\n{marker}\n\nOn your 3DS open FBI > Remote Install > Scan QR Code and scan this:\n\n"
                  f"![QR code for SCPCB3DS.cia]({img})\n\n`{url}`\n")
        subprocess.run([GH, "release", "edit", tag, "--repo", REPO, "--notes", notes], check=True)
    print("QR for", url)


if __name__ == "__main__":
    main()
