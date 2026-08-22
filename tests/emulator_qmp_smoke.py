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
    parser.add_argument(
        "--system-exit",
        action="store_true",
        help="advance to gameplay and verify the game's system-menu exit path",
    )
    parser.add_argument(
        "--enable-hle",
        action="store_true",
        help="enable firmware HLE in the file selector before starting",
    )
    parser.add_argument(
        "--visit-settings",
        action="store_true",
        help="open and leave settings without changing an option",
    )
    parser.add_argument(
        "--title-exit",
        action="store_true",
        help="close the 9288 app from the title screen instead of starting a game",
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
            qmp.key("esc")   # leave Calendar on the 9288 key matrix
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

        if args.enable_hle or args.visit_settings:
            qmp.key("up")       # settings row immediately precedes the game
            time.sleep(0.5)
            qmp.key("ret")      # enter settings
            time.sleep(0.8)
            if args.enable_hle:
                qmp.key("down")     # firmware HLE row
                time.sleep(0.5)
                qmp.key("ret")      # enable HLE; leave performance debug off
                time.sleep(0.5)
                return_steps = 3
            else:
                return_steps = 4
            for _ in range(return_steps):  # return to game list
                qmp.key("down")
                time.sleep(0.3)
            qmp.key("ret")
            time.sleep(1.0)
            qmp.capture(args.output / "02a-hle-enabled.ppm")

        qmp.key("ret", hold=1.0)  # select the highlighted .gam with a fresh press/release
        time.sleep(55.0)      # allow the full 9288-speed logo/title sequence
        qmp.capture(args.output / "03-game-menu.ppm")

        if args.title_exit:
            qmp.key("esc", hold=1.4)
            time.sleep(10.0)
            qmp.capture(args.output / "04-after-title-exit.ppm")
            time.sleep(20.0)
            qmp.capture(args.output / "05-title-exit-stable.ppm")
            return

        qmp.key("ret")       # choose the default "new journey" item
        time.sleep(3.0)
        qmp.key("ret")       # tolerate one missed matrix scan and advance dialog
        time.sleep(15.0)
        qmp.capture(args.output / "04-game-world.ppm")

        qmp.key("ret")       # verify that the opening story accepts input
        time.sleep(8.0)
        qmp.capture(args.output / "05-story-dialog.ppm")

        if args.system_exit:
            # Finish the opening dialogue, open the in-game menu with Exit,
            # choose 系统 -> 结束游戏, then leave the returned title menu.
            # F6 is the physical 4980 目录 key and is not this game menu.
            # The release copy of Fumozhuan has a longer opening conversation
            # than the earlier fixture.  Extra confirmations are harmless once
            # the world is idle and make sure Escape reaches the game menu.
            for _ in range(240):
                qmp.key("ret", hold=0.12)
                time.sleep(0.38)
            qmp.capture(args.output / "06-gameplay.ppm")
            qmp.key("esc", hold=0.12)
            time.sleep(4.0)
            qmp.capture(args.output / "07-game-menu.ppm")
            for _ in range(3):
                qmp.key("down", hold=0.12)
                time.sleep(0.5)
            qmp.key("ret", hold=0.12)
            time.sleep(3.0)
            qmp.capture(args.output / "08-system-submenu.ppm")
            for _ in range(3):
                qmp.key("down", hold=0.12)
                time.sleep(0.5)
            qmp.key("ret", hold=0.12)
            time.sleep(5.0)
            qmp.capture(args.output / "09-title-after-end-game.ppm")
            qmp.key("esc")
            time.sleep(10.0)
            qmp.capture(args.output / "10-after-system-exit.ppm")
            time.sleep(20.0)
            qmp.capture(args.output / "11-system-exit-stable.ppm")
            return

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
