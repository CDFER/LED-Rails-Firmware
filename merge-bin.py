#!/usr/bin/env python3

import os

Import("env", "projenv")

board_config = env.BoardConfig()

firmware_bin = os.path.join("${BUILD_DIR}", "${PROGNAME}.bin")
default_merged_bin = os.path.join("${BUILD_DIR}", "${PROGNAME}-merged.bin")
merged_bin = os.environ.get("MERGED_BIN_PATH", default_merged_bin)


def merge_bin_action(source, target, env):
    flash_images = [
        *env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])),
        "$ESP32_APP_OFFSET",
        source[0].get_abspath(),
    ]

    merge_cmd = [
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
    for item in flash_images:
        merge_cmd.append(
            env.subst(item)
        )  # Ensure variables like $ESP32_APP_OFFSET are expanded

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
