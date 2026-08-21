#!/usr/bin/env python3
"""Exercise the selector settings and performance-log path through QMP."""

from __future__ import annotations

import argparse
from pathlib import Path
import time

from emulator_qmp_smoke import QmpClient


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--boot-wait", type=float, default=12.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    qmp = QmpClient(args.host, args.port)
    try:
        time.sleep(args.boot_wait)
        qmp.key("ret")
        time.sleep(1.0)
        qmp.key("f12")
        time.sleep(1.0)
        qmp.key("f5")
        time.sleep(2.0)
        qmp.key("9", hold=1.0)
        time.sleep(2.0)
        for _ in range(2):
            qmp.key("down")
            time.sleep(0.2)
        qmp.key("ret")
        time.sleep(3.0)
        qmp.capture(args.output / "01-selector-default.ppm")

        qmp.key("up")
        time.sleep(0.5)
        qmp.capture(args.output / "02-settings-item.ppm")
        qmp.key("ret")
        time.sleep(1.0)
        qmp.capture(args.output / "03-settings-default.ppm")

        qmp.key("right")  # AOT off
        time.sleep(0.8)
        qmp.capture(args.output / "04-aot-off.ppm")
        qmp.key("right")  # leave AOT enabled for the gameplay test
        time.sleep(0.8)
        qmp.capture(args.output / "05-aot-on.ppm")
        qmp.key("down")
        time.sleep(0.8)
        qmp.key("ret")    # performance debug on
        time.sleep(0.8)
        qmp.capture(args.output / "06-debug-on.ppm")

        qmp.key("down")
        time.sleep(0.8)
        qmp.key("ret")    # return to selector and persist settings
        time.sleep(1.0)
        qmp.capture(args.output / "07-selector-return.ppm")
        qmp.key("ret", hold=1.0)
        time.sleep(55.0)
        qmp.capture(args.output / "08-game-menu.ppm")

        qmp.key("esc", hold=1.4)
        time.sleep(4.0)
        qmp.capture(args.output / "09-after-exit.ppm")
    finally:
        qmp.close()


if __name__ == "__main__":
    main()
