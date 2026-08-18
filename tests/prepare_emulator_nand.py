#!/usr/bin/env python3
"""Install a partial tree into a copy of a BBK 9288 NAND without formatting it."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import shutil


def load_image_tool(path: Path):
    spec = importlib.util.spec_from_file_location("bbk9288_nand_image", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load NAND image tool: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image-tool", type=Path, required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--flat", type=Path, required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.source.is_dir():
        raise SystemExit(f"source tree not found: {args.source}")

    image_tool = load_image_tool(args.image_tool.resolve())
    image_tool.extract_image(args.base.resolve(), args.flat.resolve())

    from pyfatfs.PyFatFS import PyFatFS

    fat = PyFatFS(
        str(args.flat.resolve()),
        encoding="gbk",
        offset=image_tool.PARTITION_LBA * image_tool.SECTOR_SIZE,
        preserve_case=True,
        read_only=False,
    )
    installed: list[tuple[str, int]] = []
    try:
        paths = sorted(
            args.source.rglob("*"),
            key=lambda path: (not path.is_dir(), path.as_posix()),
        )
        for source in paths:
            relative = source.relative_to(args.source).as_posix()
            target = "/" + relative
            if source.is_dir():
                fat.makedirs(target, recreate=True)
            elif source.is_file():
                parent = target.rsplit("/", 1)[0] or "/"
                fat.makedirs(parent, recreate=True)
                # pyfatfs may keep the previous cluster chain when an existing
                # file is opened with "w".  Removing it first makes a resource-
                # only update deterministic and avoids writing through a stale
                # FREE_CLUSTER marker.
                if fat.exists(target):
                    fat.remove(target)
                with source.open("rb") as src, fat.openbin(target, "w") as dst:
                    shutil.copyfileobj(src, dst, length=1024 * 1024)
                installed.append((target, source.stat().st_size))
    finally:
        fat.close()

    image_tool.patch_gbk_short_names(args.flat.resolve())

    verify = PyFatFS(
        str(args.flat.resolve()),
        encoding="gbk",
        offset=image_tool.PARTITION_LBA * image_tool.SECTOR_SIZE,
        read_only=True,
    )
    try:
        kernel_size = verify.getsize("/kernel.bin")
        for target, expected_size in installed:
            actual_size = verify.getsize(target)
            if actual_size != expected_size:
                raise RuntimeError(
                    f"size mismatch for {target}: {actual_size} != {expected_size}"
                )
    finally:
        verify.close()

    image_tool.pack_image(args.flat.resolve(), args.output.resolve())
    print(
        f"preserved kernel.bin ({kernel_size} bytes); "
        f"installed {len(installed)} files"
    )


if __name__ == "__main__":
    main()
