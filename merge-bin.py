#!/usr/bin/env python3

import os
from typing import List, Union

Import("env", "projenv")

board_config = env.BoardConfig()

firmware_bin: str = os.path.join("${BUILD_DIR}", "${PROGNAME}.bin")
default_merged_bin: str = os.path.join("${BUILD_DIR}", "${PROGNAME}-merged.bin")
merged_bin: str = os.environ.get("MERGED_BIN_PATH", default_merged_bin)


def merge_bin_action(source: List[File], target: List[File], env: Environment) -> None:
    # Build flash_images as a list of dicts: {"address": ..., "file": ...}
    extra_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
    flash_images = []
    # FLASH_EXTRA_IMAGES is expected to be a list of [address, file] pairs
    for i in range(0, len(extra_images), 2):
        flash_images.append({"address": extra_images[i], "file": extra_images[i + 1]})
    # Add the main firmware image
    flash_images.append(
        {"address": "$ESP32_APP_OFFSET", "file": source[0].get_abspath()}
    )

    print("Flash images to merge:")
    for img in flash_images:
        kiB = int(env.subst(img["address"]), 0) // 1024
        file_name = os.path.basename(img["file"])
        print(f" @ {img['address']} ({kiB:,} KiB): {file_name}")

    merge_cmd: List[str] = [
        "$PYTHONEXE",
        "$OBJCOPY",
        "--chip",
        board_config.get("build.mcu", "esp32"),
        "merge_bin",
        "-o",
        merged_bin,
        "--flash_mode",
        "keep",
        "--flash_freq",
        "${__get_board_f_flash(__env__)}",
        "--flash_size",
        board_config.get("upload.flash_size", "4MB"),
    ]

    # Add flash images (address + file pairs)
    for img in flash_images:
        merge_cmd.append(env.subst(img["address"]))
        merge_cmd.append(env.subst(img["file"]))

    env.Execute(
        env.VerboseAction(
            " ".join(['"' + arg + '"' for arg in merge_cmd]), "Merging firmware..."
        )
    )


env.AddCustomTarget(
    name="mergebin",
    dependencies=firmware_bin,
    actions=merge_bin_action,
    title="Build merged image",
    description="Build combined image",
    always_build=True,
)
