from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import zipfile


PROJECT_ROOT = Path(__file__).resolve().parent
APP_PATH = PROJECT_ROOT / "build" / "9288" / "GAM4980.exe"
ICON_PATHS = (
    PROJECT_ROOT / "assets" / "9288" / "ico1.bin",
    PROJECT_ROOT / "assets" / "9288" / "ico2.bin",
)
ROM_ROOT = PROJECT_ROOT / "应用" / "数据" / "游戏" / "gam4980"
ROM_PATHS = (ROM_ROOT / "8.BIN", ROM_ROOT / "E.BIN")
ROM_SHA256 = {
    "8.BIN": "1c8f0b75f478cc42b1cc4292ff6c3b022b11384f0b6fc1b9601873a9da656d6f",
    "E.BIN": "9d13aa4593d97b790afc37d73da8be985e7a3aa7f3dcfe6b91c798671067aa5e",
}
ROM_SIZE = 0x200000


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate() -> None:
    if not APP_PATH.is_file():
        raise SystemExit(f"missing app: {APP_PATH}; run build_9288.py first")
    data = APP_PATH.read_bytes()
    if len(data) < 0x30:
        raise SystemExit("9288 app is truncated")
    (
        magic0,
        magic1,
        machine,
        module,
        _name,
        code_offset,
        icon_offset,
        icon1_size,
        icon2_size,
        total_size,
    ) = struct.unpack_from("<IIHH16sIIIII", data)
    if (magic0, magic1, machine) != (0x0032464B, 0x19760212, 1):
        raise SystemExit("9288 app has an invalid KF2 header")
    if module != 8:
        raise SystemExit("9288 app is not assigned to the Entertainment category")
    if total_size != len(data):
        raise SystemExit("9288 app header size does not match the file")
    if (icon_offset, icon1_size, icon2_size, code_offset) != (0x30, 412, 76, 536):
        raise SystemExit("9288 app has an invalid icon layout")
    cursor = icon_offset
    for icon_path, icon_size in zip(ICON_PATHS, (icon1_size, icon2_size)):
        if not icon_path.is_file():
            raise SystemExit(f"missing 9288 icon resource: {icon_path}")
        icon = icon_path.read_bytes()
        if len(icon) != icon_size or data[cursor : cursor + icon_size] != icon:
            raise SystemExit(f"9288 app icon does not match: {icon_path}")
        cursor += icon_size
    for rom in ROM_PATHS:
        if not rom.is_file() or rom.stat().st_size != ROM_SIZE:
            raise SystemExit(f"missing or invalid runtime ROM: {rom}")
        if sha256(rom) != ROM_SHA256[rom.name]:
            raise SystemExit(f"runtime ROM checksum mismatch: {rom}")


def add_file(archive: zipfile.ZipFile, source: Path, target: str) -> None:
    info = zipfile.ZipInfo(target, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, source.read_bytes())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Package the BBK 9288 release")
    parser.add_argument(
        "--output",
        type=Path,
        default=PROJECT_ROOT / "build" / "bbk9288-gam4980-player.zip",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    validate()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        add_file(archive, APP_PATH, "系统/程序/GAM4980.exe")
        for rom in ROM_PATHS:
            add_file(archive, rom, f"gam4980/{rom.name}")
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None:
            raise SystemExit("release ZIP CRC validation failed")
    print(f"built release: {args.output.resolve()}")
    print(f"ZIP: {args.output.stat().st_size} bytes, sha256={sha256(args.output)}")


if __name__ == "__main__":
    main()
