#!/usr/bin/env python3
"""US-003 adapter for the ESP32 wireless gateway platform."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = [
    {"id": "esp32-default", "description": "build the tracked ESP-IDF target", "gate": 0, "level": "compile"},
    {"id": "esp32-device", "description": "flash and monitor one identified ESP32", "gate": 1, "level": "device"},
    {"id": "ESP32-WS-001", "description": "two-client WebSocket disconnect isolation", "gate": 1, "level": "device"},
    {"id": "BACKPRESSURE-001", "description": "log backpressure while command freshness continues", "gate": 2, "level": "hil"},
]


def now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def base(command: str, level: str = "static") -> dict[str, Any]:
    stamp = now()
    return {"schema_version": 1, "command": command, "scenario": None, "scenario_version": None, "started_at": stamp, "finished_at": stamp, "outcome": "pass", "verification_level": level, "physical_stage": None, "environment": {}, "repositories": [], "devices": [], "checks": [], "artifacts": [], "approvals": [], "evidence_id": None, "safe_next_action": None}


def item(check_id: str, outcome: str, expected: str, observed: str, duration: int = 0, category: str | None = None, code: int | None = None, command: list[str] | None = None) -> dict[str, Any]:
    return {"id": check_id, "outcome": outcome, "expected_signal": expected, "observed": observed[-4000:], "duration_ms": duration, "failure_category": category, "exit_code": code, "command": command}


def finish(value: dict[str, Any]) -> dict[str, Any]:
    order = {"pass": 0, "skipped": 1, "inconclusive": 2, "blocked": 3, "fail": 4}
    value["outcome"] = max((entry["outcome"] for entry in value["checks"]), key=order.get) if value["checks"] else "inconclusive"
    value["finished_at"] = now()
    return value


def repo() -> dict[str, Any]:
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=False).stdout.strip()
    dirty = bool(subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, text=True, capture_output=True, check=False).stdout.strip())
    return {"name": "esp32", "sha": sha, "dirty": dirty}


def idf_root() -> Path | None:
    configured = os.environ.get("IDF_PATH")
    candidates = [Path(configured)] if configured else []
    candidates.append(Path.home() / "Projects" / "esp-idf")
    return next((path for path in candidates if (path / "export.sh").is_file()), None)


def idf_run(arguments: list[str], timeout: int = 1800) -> tuple[subprocess.CompletedProcess[str], int]:
    root = idf_root()
    if root is None:
        return subprocess.CompletedProcess(arguments, 127, "", "ESP-IDF export.sh not found"), 0
    command = f"source {shlex.quote(str(root / 'export.sh'))} >/dev/null && idf.py " + " ".join(shlex.quote(arg) for arg in arguments)
    env = dict(os.environ)
    env.setdefault("XDG_CACHE_HOME", "/tmp/ai-robot-esp32-cache")
    started = time.monotonic()
    try:
        completed = subprocess.run(["bash", "-lc", command], cwd=ROOT, env=env, text=True, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        completed = subprocess.CompletedProcess(arguments, 124, exc.stdout or "", exc.stderr or "timeout")
    return completed, int((time.monotonic() - started) * 1000)


def describe() -> dict[str, Any]:
    value = base("esp32 adapter describe")
    value["environment"] = {"adapter_protocol": 1, "platform": "esp32", "scenarios": SCENARIOS}
    value["checks"].append(item("esp32.adapter", "pass", "adapter protocol 1", "adapter protocol 1"))
    return finish(value)


def doctor() -> dict[str, Any]:
    value = base("esp32 doctor")
    value["repositories"] = [repo()]
    for name in ("cmake", "ninja"):
        path = shutil.which(name)
        value["checks"].append(item(f"esp32.tool.{name}", "pass" if path else "blocked", f"{name} available", path or "not found", category=None if path else "missing_tool"))
    root = idf_root()
    value["checks"].append(item("esp32.idf", "pass" if root else "blocked", "ESP-IDF installation with export.sh", str(root) if root else "not found", category=None if root else "environment_missing"))
    if root:
        completed, duration = idf_run(["--version"], 60)
        active = completed.returncode == 0
        value["checks"].append(item("esp32.idf.environment", "pass" if active else "blocked", "activatable ESP-IDF Python environment", (completed.stdout + "\n" + completed.stderr).strip(), duration, None if active else "environment_missing", completed.returncode))
        if not active:
            value["safe_next_action"] = "Install or repair the ESP-IDF 5.5 Python environment, then rerun doctor."
    value["environment"] = {"platform": "esp32", "target": "esp32", "idf_installation": bool(root), "software_tools": [{"name": "esp-idf", "version": "5.5", "source": "IDF checkout"}], "read_only": True}
    return finish(value)


def verify(scenario: str, device_json: str | None) -> dict[str, Any]:
    level = next((entry["level"] for entry in SCENARIOS if entry["id"] == scenario), "device")
    value = base(f"esp32 verify {scenario}", level)
    value["scenario"] = scenario
    value["scenario_version"] = 1
    value["repositories"] = [repo()]
    value["environment"] = {"platform": "esp32", "target": "esp32", "software_tools": [{"name": "esp-idf", "version": "5.5", "source": "IDF checkout"}]}
    if scenario in {"ESP32-WS-001", "BACKPRESSURE-001"}:
        value["checks"].append(item(f"esp32.scenario.{scenario.lower()}", "blocked", "instrumented scenario assertions", "runtime endpoint/fixture and identified devices are not yet available", category="human_action_required"))
        value["safe_next_action"] = "Connect and alias the gateway, then add the declared runtime probe fixture."
        return finish(value)
    arguments = ["build"] if scenario == "esp32-default" else ["flash", "monitor"]
    if scenario != "esp32-default":
        if not device_json:
            value["checks"].append(item("esp32.device", "blocked", "resolved serial device", "device metadata was not supplied", category="device_absent"))
            return finish(value)
        device_data = json.loads(Path(device_json).read_text(encoding="utf-8"))
        arguments = ["-p", device_data["observed"]["port"], *arguments]
    completed, duration = idf_run(arguments)
    outcome = "pass" if completed.returncode == 0 else "fail" if completed.returncode not in {127} else "blocked"
    category = None if outcome == "pass" else "environment_missing" if outcome == "blocked" else "flash_failed" if scenario == "esp32-device" else "build_failed"
    value["checks"].append(item(f"esp32.idf.{scenario}", outcome, f"idf.py {' '.join(arguments)} succeeds", (completed.stdout + "\n" + completed.stderr).strip(), duration, category, completed.returncode, ["idf.py", *arguments]))
    if outcome == "blocked":
        value["safe_next_action"] = "Install or repair the ESP-IDF 5.5 Python environment, then rerun this scenario."
    return finish(value)


def main() -> int:
    as_json = "--json" in sys.argv
    argv = [arg for arg in sys.argv[1:] if arg != "--json"]
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("describe")
    sub.add_parser("doctor")
    sub.add_parser("inventory")
    verify_parser = sub.add_parser("verify")
    verify_parser.add_argument("scenario")
    verify_parser.add_argument("--device-json")
    args = parser.parse_args(argv)
    value = describe() if args.command == "describe" else doctor() if args.command in {"doctor", "inventory"} else verify(args.scenario, args.device_json)
    print(json.dumps(value, indent=2, sort_keys=True) if as_json else f"{value['command']}: {value['outcome']}")
    return 0 if value["outcome"] == "pass" else 20 if value["outcome"] == "blocked" else 10


if __name__ == "__main__":
    raise SystemExit(main())
