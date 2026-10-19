#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regenerate icons and fonts, or compare real generated output without writing it."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report generated-file drift without overwriting")
    args = parser.parse_args()
    try:
        import PIL
    except ImportError:
        raise SystemExit("Missing Pillow: install tools/artwork/requirements.txt in a Python virtual environment")
    if PIL.__version__ != "12.3.0":
        raise SystemExit("Pillow 12.3.0 required: install tools/artwork/requirements.txt in a Python virtual environment")
    with tempfile.TemporaryDirectory(prefix="serin-artwork-") as temp:
        output = Path(temp)
        subprocess.run([sys.executable, str(ROOT / "tools/gen_icons.py"), "--out", str(output / "icons_serin.c")], check=True)
        subprocess.run([sys.executable, str(ROOT / "tools/gen_fonts.py"), "--out-dir", str(output / "fonts")], check=True)
        files = [(output / "icons_serin.c", ROOT / "main/icons_serin.c")]
        files += [(p, ROOT / "main/fonts" / p.name) for p in sorted((output / "fonts").glob("*.c"))]
        # A stale extra .c also enters the firmware via CMake's glob.
        expected = {target for _, target in files}
        extra = set((ROOT / "main/fonts").glob("*.c")) - expected
        drift = [str(p.relative_to(ROOT)) for p in sorted(extra)]
        for generated, target in files:
            if args.check:
                if not target.is_file() or generated.read_bytes() != target.read_bytes():
                    drift.append(str(target.relative_to(ROOT)))
            else:
                target.write_bytes(generated.read_bytes())
        if extra and not args.check:
            raise SystemExit("Unexpected generated fonts; remove obsolete files explicitly: " + ", ".join(drift))
        if drift:
            raise SystemExit("Artwork drift: " + ", ".join(drift) + "\nRun python tools/artwork.py to regenerate; review the diff.")
    print("Artwork matches the public sources." if args.check else "Artwork regenerated.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"Artwork generator failed (exit {error.returncode}); see the diagnostic above.")
