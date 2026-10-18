#!/usr/bin/env python3
"""Applies the smartboard additions to a copy of the ESP-Hosted slave project.

Usage: apply_additions.py <copied slave project dir> <fw revision> [--test-never-confirm]

--test-never-confirm builds a TEST image that never confirms itself, to
verify the self-check's fallback on hardware. Never ship it.

Every edit is anchored on exact upstream text and fails loudly if the anchor
is missing, so an ESP-Hosted version bump that moves things around is
noticed instead of silently producing a firmware without the self-check.
"""
import pathlib
import shutil
import sys


def edit(path: pathlib.Path, anchor: str, replacement: str) -> None:
    text = path.read_text()
    if text.count(anchor) != 1:
        sys.exit(f"apply_additions: anchor not found exactly once in {path}:\n{anchor}")
    path.write_text(text.replace(anchor, replacement))


def main() -> None:
    project = pathlib.Path(sys.argv[1])
    revision = int(sys.argv[2])
    never_confirm = "--test-never-confirm" in sys.argv[3:]
    here = pathlib.Path(__file__).resolve().parent
    main_dir = project / "main"

    for name in ("c6_self_check.c", "c6_self_check.h"):
        shutil.copy(here / "additions" / name, main_dir / name)

    # 1. Compile the self-check.
    edit(main_dir / "CMakeLists.txt",
         '\t"slave_control.c"\n)',
         '\t"slave_control.c"\n\t"c6_self_check.c"\n)')

    # 2. Version: "<esp-hosted version>-sb.<revision>" in the image header,
    #    revision also reported to the host in the fw-version RPC's "build".
    edit(project / "CMakeLists.txt",
         'set(PROJECT_VER "${PROJECT_VERSION_MAJOR_1}.${PROJECT_VERSION_MINOR_1}.${PROJECT_VERSION_PATCH_1}")',
         'set(PROJECT_VER "${PROJECT_VERSION_MAJOR_1}.${PROJECT_VERSION_MINOR_1}.${PROJECT_VERSION_PATCH_1}-sb.'
         f'{revision}")')
    edit(project / "CMakeLists.txt",
         'idf_build_set_property(COMPILE_OPTIONS "-fdiagnostics-color=always" APPEND)',
         'idf_build_set_property(COMPILE_OPTIONS "-fdiagnostics-color=always" APPEND)\n'
         f'idf_build_set_property(COMPILE_DEFINITIONS "SMARTBOARD_C6_FW_REVISION={revision}" APPEND)')
    edit(main_dir / "slave_control.c",
         "\tresp_payload->build      = -1;",
         "\tresp_payload->build      = SMARTBOARD_C6_FW_REVISION;")

    # 3. Self-check on boot.
    edit(main_dir / "esp_hosted_coprocessor.c",
         "\tESP_ERROR_CHECK( ret );\n\n\tesp_hosted_coprocessor_init();",
         "\tESP_ERROR_CHECK( ret );\n\n\tc6_self_check_on_boot();\n\n\tesp_hosted_coprocessor_init();")
    edit(main_dir / "esp_hosted_coprocessor.c",
         '#include "esp_hosted_coprocessor.h"',
         '#include "esp_hosted_coprocessor.h"\n#include "c6_self_check.h"')

    # 4. Confirm once the host completed an RPC.
    edit(main_dir / "slave_control.c",
         "\tret = req_table[req_index].command_handler(req, resp, priv_data);\n"
         "\tif (ret) {\n"
         "\t\tESP_LOGE(TAG, \"RPC Req [0x%x] execution failed\", req->msg_id);\n"
         "\t\tgoto err_cmd_error;\n"
         "\t}\n\n"
         "\treturn ESP_OK;",
         "\tret = req_table[req_index].command_handler(req, resp, priv_data);\n"
         "\tif (ret) {\n"
         "\t\tESP_LOGE(TAG, \"RPC Req [0x%x] execution failed\", req->msg_id);\n"
         "\t\tgoto err_cmd_error;\n"
         "\t}\n\n"
         "\t/* smartboard: the host can talk to this image -> it is good */\n"
         + ("\t/* TEST IMAGE: confirmation disabled (--test-never-confirm) */\n"
            if never_confirm else "\tc6_self_check_confirm();\n")
         + "\treturn ESP_OK;")
    edit(main_dir / "slave_control.c",
         '#include "slave_control.h"',
         '#include "slave_control.h"\n#include "c6_self_check.h"')

    print(f"apply_additions: done (revision {revision})"
          + (" — TEST IMAGE, never confirms" if never_confirm else ""))


if __name__ == "__main__":
    main()
