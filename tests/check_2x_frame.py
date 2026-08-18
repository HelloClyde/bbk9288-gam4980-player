#!/usr/bin/env python3
"""Verify the port's exact 2x LCD geometry in an emulator screenshot."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("screenshot", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    image = Image.open(args.screenshot).convert("RGB")
    if image.size != (320, 240):
        raise SystemExit(f"unexpected screenshot size: {image.size}")
    pixels = image.load()
    horizontal = sum(
        pixels[1 + source_x * 2, y] != pixels[2 + source_x * 2, y]
        for y in range(24, 216)
        for source_x in range(159)
    )
    vertical = sum(
        pixels[x, 24 + source_y * 2] != pixels[x, 25 + source_y * 2]
        for x in range(1, 319)
        for source_y in range(96)
    )
    margin = sum(
        pixels[x, y] != (255, 255, 255)
        for y in range(240)
        for x in range(320)
        if (24 <= y < 216 and x in (0, 319)) or y >= 216
    )
    if horizontal or vertical or margin:
        raise SystemExit(
            "2x geometry check failed: "
            f"horizontal={horizontal}, vertical={vertical}, margin={margin}"
        )
    print(
        "exact 2x geometry verified: 318x192 at (1, 24), "
        "white client-area margins"
    )


if __name__ == "__main__":
    main()
