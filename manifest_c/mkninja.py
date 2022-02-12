# This is an example of how manifest.c could be included in a larger project.

import shlex
import sys

if sys.platform == 'win32':
    # windows build
    build_cmd = ["cl.exe", SRC/"manifest.c"]
    manifest_bin = BLD/"manifest.exe"
else:
    # unix build
    manifest_bin = BLD/"manifest"
    build_cmd = [
        "gcc",
        SRC/"manifest.c",
        "-o",
        manifest_bin,
        "-Wall",
        "-Wextra",
        "-Werror",
    ]

add_target(
    inputs=[SRC/"manifest.c"],
    outputs=[manifest_bin],
    command=build_cmd,
    workdir=BLD,
    display="building manifest binary",
)

def add_manifest(where, cmd, out):
    if isinstance(cmd, str):
        cmd = shlex.split(cmd)
    return add_target(
        inputs=[manifest_bin],
        command=["(", *cmd, ")", "|", manifest_bin, out],
        outputs=[out],
        workdir=where,
        display=f"listing files: {' '.join(cmd)}",
        phony=True,
    )
