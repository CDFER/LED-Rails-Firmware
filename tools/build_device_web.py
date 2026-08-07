"""PlatformIO pre-build hook for the embedded Svelte device console."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

Import("env")

PROJECT_ROOT = Path(env["PROJECT_DIR"])
DEVICE_WEB_DIRECTORY = PROJECT_ROOT / "Device Web"
NPM_COMMAND = "npm.cmd" if os.name == "nt" else "npm"

sys.path.insert(0, str(PROJECT_ROOT / "tools"))
from generate_device_web_assets import assets_are_current, generate  # noqa: E402


def run(command: list[str]) -> None:
    """Run a frontend command from the device console directory."""
    subprocess.run(command, cwd=DEVICE_WEB_DIRECTORY, check=True)


def build_device_web() -> None:
    """Build the Svelte console only when its generated assets are stale."""
    if assets_are_current():
        return

    print("Building embedded Svelte device console...")
    run([NPM_COMMAND, "install", "--no-audit", "--no-fund"])
    run([NPM_COMMAND, "run", "build"])
    generate()
    if not assets_are_current():
        raise RuntimeError("Generated device web assets are stale after build")


build_device_web()
