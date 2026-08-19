#!/usr/bin/env python3
"""Drive the BBK 9288 emulator through QMP and capture smoke-test frames."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import time


class QmpClient:
    def __init__(self, host: str, port: int) -> None:
        self._socket = socket.create_connection((host, port), timeout=10)
        self._reader = self._socket.makefile("r", encoding="utf-8")
        greeting = json.loads(self._reader.readline())
        if "QMP" not in greeting:
            raise RuntimeError(f"unexpected QMP greeting: {greeting}")
        self._next_id = 1
        self.command("qmp_capabilities")

    def close(self) -> None:
        self._reader.close()
        self._socket.close()

    def command(self, execute: str, arguments: dict | None = None) -> object:
        command_id = self._next_id
        self._next_id += 1
        request: dict[str, object] = {"execute": execute, "id": command_id}
        if arguments is not None:
            request["arguments"] = arguments
        self._socket.sendall((json.dumps(request) + "\n").encode("utf-8"))
        while True:
            line = self._reader.readline()
            if not line:
                raise RuntimeError("QMP connection closed")
            response = json.loads(line)
            if response.get("id") != command_id:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP command {execute} failed: {response['error']}")
            return response.get("return")

    def key(self, qcode: str, hold: float = 0.45) -> None:
        for down in (True, False):
            self.command(
                "input-send-event",
                {
                    "events": [
                        {
                            "type": "key",
                            "data": {
                                "down": down,
                                "key": {"type": "qcode", "data": qcode},
                            },
                        }
                    ]
                },
            )
            if down:
                time.sleep(hold)

    def capture(self, path: Path) -> None:
        qemu_path = path.resolve().as_posix()
        self.command(
            "human-monitor-command",
            {"command-line": f"screendump {qemu_path}"},
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--boot-wait", type=float, default=12.0)
    parser.add_argument(
        "--from-home",
        action="store_true",
        help="start with the emulator already showing the home screen",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    qmp = QmpClient(args.host, args.port)
    try:
        if not args.from_home:
            time.sleep(args.boot_wait)
            qmp.key("ret")   # dismiss the clock prompt (calendar opens below it)
            time.sleep(1.0)
            qmp.key("f12")   # leave Calendar
            time.sleep(1.0)
            qmp.key("f5")    # Start / home
            time.sleep(2.0)
        qmp.key("9", hold=1.0)  # Entertainment category needs a full key scan
        time.sleep(2.0)
        for _ in range(2):   # row 3, column 1: GAM4980
            qmp.key("down")
            time.sleep(0.2)
        qmp.capture(args.output / "01-gam4980-selected.ppm")

        qmp.key("ret")
        time.sleep(3.0)
        qmp.capture(args.output / "02-file-selector.ppm")

        qmp.key("ret", hold=1.0)  # select the highlighted .gam with a fresh press/release
        time.sleep(55.0)      # allow the full 9288-speed logo/title sequence
        qmp.capture(args.output / "03-game-menu.ppm")

        qmp.key("ret")       # choose the default "new journey" item
        time.sleep(3.0)
        qmp.key("ret")       # tolerate one missed matrix scan and advance dialog
        time.sleep(15.0)
        qmp.capture(args.output / "04-game-world.ppm")

        qmp.key("ret")       # verify that the opening story accepts input
        time.sleep(8.0)
        qmp.capture(args.output / "05-story-dialog.ppm")

        qmp.key("esc")       # short press sends game Exit without closing the app
        time.sleep(3.0)
        qmp.capture(args.output / "06-after-short-exit.ppm")

        qmp.key("esc", hold=1.4)  # a deliberate hold closes the 9288 app
        time.sleep(3.0)
        qmp.capture(args.output / "07-after-long-exit.ppm")
    finally:
        qmp.close()


if __name__ == "__main__":
    main()
