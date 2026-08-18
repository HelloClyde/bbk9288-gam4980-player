from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess


PROJECT_ROOT = Path(__file__).resolve().parent
SOURCE_ROOT = PROJECT_ROOT / "src"
ICON_ROOT = PROJECT_ROOT / "assets" / "9288"
BUILD_ROOT = PROJECT_ROOT / "build" / "9288"
DEFAULT_SDK = PROJECT_ROOT / "sdk"

MAGIC0 = 0x0032464B
MAGIC1 = 0x19760212
HEADER_SIZE = 0x30
MACHINE_9288 = 1
# KF2 module 8 is the home menu's Entertainment category.
APP_MODULE = 8
APP_NAME = b"GAM4980"


def run(command: list[str], step: str) -> None:
    print("+", " ".join(command))
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError as exc:
        raise SystemExit(f"{step}: tool not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"{step} failed with exit code {exc.returncode}") from exc


def write_sdk_header(path: Path, data: bytes) -> None:
    lines = []
    for line in data.splitlines(keepends=True):
        if line.lstrip().startswith(b"#include"):
            line = line.replace(b"\\", b"/")
        lines.append(line)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"".join(lines))


def prepare_sdk_headers(sdk: Path, destination: Path) -> None:
    source = sdk / "Down_Include"
    if not source.is_dir():
        raise SystemExit(f"9288 SDK headers not found: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    for header in source.rglob("*.h"):
        relative = header.relative_to(source)
        data = header.read_bytes()
        variants = {
            relative,
            Path(*(part.lower() for part in relative.parts)),
            Path(*(part.lower() for part in relative.parts[:-1]), relative.name),
        }
        for variant in variants:
            write_sdk_header(destination / variant, data)


def find_tool(toolchain: Path, name: str) -> str:
    candidates = (
        toolchain / name,
        toolchain / f"{name}.exe",
        toolchain / "bin" / name,
        toolchain / "bin" / f"{name}.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"missing S1C33 tool: {name}; searched under {toolchain}")


def compile_app(
    sdk: Path,
    toolchain: Path,
    switch_dispatch: bool,
    optimization: str,
) -> bytes:
    clang = find_tool(toolchain, "clang")
    objcopy = find_tool(toolchain, "llvm-objcopy")
    readelf = find_tool(toolchain, "llvm-readelf")
    generated_include = BUILD_ROOT / "sdk-include"
    prepare_sdk_headers(sdk, generated_include)

    objects: list[Path] = []
    common_flags = [
        "--target=s1c33-none-elf",
        f"-O{optimization}",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-jump-tables",
        "-fomit-frame-pointer",
        "-fdata-sections",
        "-ffunction-sections",
        "-fno-strict-aliasing",
        "-Wall",
        "-Wextra",
        "-Wno-unused-function",
        "-Wno-unused-parameter",
        "-Wno-pointer-to-int-cast",
        "-Wno-int-to-pointer-cast",
        "-DDL_DOWN",
        "-D_RLS_",
        "-I",
        str(SOURCE_ROOT / "9288_compat"),
        "-I",
        str(generated_include),
        "-I",
        str(SOURCE_ROOT),
    ]
    if switch_dispatch:
        common_flags.append("-DS6502_NO_COMPUTED_GOTO")
    for source in (
        SOURCE_ROOT / "gam4980_9288_start.c",
        SOURCE_ROOT / "gam4980_9288_runtime.c",
        SOURCE_ROOT / "gam4980_9288.c",
    ):
        if not source.is_file():
            raise SystemExit(f"missing source: {source}")
        output = BUILD_ROOT / f"{source.stem}.o"
        run(
            [clang, *common_flags, "-c", str(source), "-o", str(output)],
            f"compile {source.name}",
        )
        objects.append(output)

    elf = BUILD_ROOT / "GAM4980.elf"
    map_path = BUILD_ROOT / "GAM4980.map"
    linker_script = SOURCE_ROOT / "gam4980_9288.ld"
    run(
        [
            clang,
            "--target=s1c33-none-elf",
            "-fuse-ld=lld",
            "-nostdlib",
            "-Wl,--gc-sections",
            f"-Wl,-T,{linker_script}",
            f"-Wl,-Map,{map_path}",
            *(str(item) for item in objects),
            "-o",
            str(elf),
        ],
        "link 9288 ELF",
    )
    raw = BUILD_ROOT / "GAM4980.bin"
    run(
        [objcopy, "-O", "binary", "--gap-fill", "255", str(elf), str(raw)],
        "extract 9288 payload",
    )
    run([readelf, "-h", "-S", str(elf)], "inspect 9288 ELF")
    return raw.read_bytes()


def read_icon(path: Path, width: int, height: int) -> bytes:
    if not path.is_file():
        raise SystemExit(
            f"missing GAM4980 icon: {path}; run tools/convert_9288_icon.py"
        )
    data = path.read_bytes()
    expected_size = 12 + width * height * 2 // 8
    if len(data) != expected_size:
        raise SystemExit(
            f"invalid 9288 icon size: {path} has {len(data)} bytes, "
            f"expected {expected_size}"
        )
    icon_width, icon_height, bpp, reserved0, reserved1, reserved2 = (
        struct.unpack_from("<HHHHHH", data)
    )
    if (icon_width, icon_height, bpp, reserved0, reserved1, reserved2) != (
        width,
        height,
        2,
        0,
        0,
        0,
    ):
        raise SystemExit(f"invalid 9288 icon header: {path}")
    return data


def pack_kf2(payload: bytes) -> bytes:
    icon1 = read_icon(ICON_ROOT / "ico1.bin", 40, 40)
    icon2 = read_icon(ICON_ROOT / "ico2.bin", 16, 16)
    code_offset = HEADER_SIZE + len(icon1) + len(icon2)
    total_size = code_offset + len(payload)
    name = APP_NAME.ljust(16, b"\0")
    header = struct.pack(
        "<IIHH16sIIIII",
        MAGIC0,
        MAGIC1,
        MACHINE_9288,
        APP_MODULE,
        name,
        code_offset,
        HEADER_SIZE,
        len(icon1),
        len(icon2),
        total_size,
    )
    if len(header) != HEADER_SIZE:
        raise AssertionError(f"KF2 header is {len(header)} bytes")
    return header + icon1 + icon2 + payload


def read_existing_payload(path: Path) -> bytes:
    app = path.read_bytes()
    if len(app) < HEADER_SIZE:
        raise SystemExit(f"invalid existing KF2 file: {path}")
    (
        magic0,
        magic1,
        machine,
        _module,
        _name,
        code_offset,
        header_size,
        icon1_size,
        icon2_size,
        total_size,
    ) = struct.unpack_from("<IIHH16sIIIII", app)
    if (magic0, magic1, machine, header_size) != (
        MAGIC0,
        MAGIC1,
        MACHINE_9288,
        HEADER_SIZE,
    ):
        raise SystemExit(f"existing file is not a BBK 9288 KF2 executable: {path}")
    if code_offset != HEADER_SIZE + icon1_size + icon2_size:
        raise SystemExit(f"invalid KF2 code offset in existing file: {path}")
    if total_size != len(app):
        raise SystemExit(f"invalid KF2 total size in existing file: {path}")
    return app[code_offset:]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the standalone BBK 9288 gam4980 port"
    )
    parser.add_argument(
        "--sdk",
        type=Path,
        default=DEFAULT_SDK,
        help="9288 SDK root (WSL path when run in WSL)",
    )
    parser.add_argument(
        "--toolchain",
        type=Path,
        help="directory containing the S1C33 LLVM tools",
    )
    parser.add_argument(
        "--reuse-payload-from",
        type=Path,
        help="reuse an existing 9288 KF2 payload when only repacking resources",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=BUILD_ROOT / "GAM4980.exe",
    )
    parser.add_argument(
        "--switch-dispatch",
        action="store_true",
        help="use the slower portable 6502 switch dispatcher",
    )
    parser.add_argument(
        "--optimization",
        choices=("2", "3", "s", "z"),
        default="s",
        help="compiler optimization level (default: s, fastest in the 9288 emulator)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    sdk = args.sdk.resolve()
    output = args.output.resolve()
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.reuse_payload_from is not None:
        payload = read_existing_payload(args.reuse_payload_from.resolve())
    else:
        if args.toolchain is None:
            raise SystemExit("--toolchain is required unless --reuse-payload-from is used")
        payload = compile_app(
            sdk,
            args.toolchain.resolve(),
            switch_dispatch=args.switch_dispatch,
            optimization=args.optimization,
        )
    app = pack_kf2(payload)
    output.write_bytes(app)
    digest = hashlib.sha256(app).hexdigest()
    print(f"built: {output}")
    print(f"payload: {len(payload)} bytes")
    print(f"KF2: {len(app)} bytes, sha256={digest}")


if __name__ == "__main__":
    main()
