import argparse
import importlib
import pathlib
import shlex
import subprocess
import sys

import mkninja


def main():
    # shortcuts for calling the binaries directly
    HERE = pathlib.Path(__file__).parent
    if len(sys.argv) > 1 and sys.argv[1] == "--findglob":
        findglob = HERE / "findglob"
        return subprocess.Popen([str(findglob), *sys.argv[2:]]).wait()
    if len(sys.argv) > 1 and sys.argv[1] == "--manifest":
        manifest = HERE / "manifest"
        return subprocess.Popen([str(manifest), *sys.argv[2:]]).wait()

    parser = argparse.ArgumentParser("mkninja")
    parser.add_argument("src")
    parser.add_argument(
        "--version", action="version", version=mkninja.__version__
    )
    args = parser.parse_args(sys.argv[1:])

    src = pathlib.Path(args.src).absolute()
    bld = pathlib.Path(".").absolute()

    truename = "arbitrary_module_name"
    alias = "root"

    # store the exact command we ran with
    rerun_script = bld / ".rerun_mkninja.sh"
    with rerun_script.open("w") as f:
        print("#!/bin/sh", file=f)
        quoted = [shlex.quote(sys.executable)]
        quoted += [shlex.quote(arg) for arg in sys.argv]
        print(*quoted, file=f)

    # We don't need the src directory in our sys.path because we have a custom
    # Finder on the sys.metapath.

    with mkninja._Project(src, bld, truename, alias) as p:
        importlib.import_module(truename)

    with open("build.ninja", "w") as f:
        print(p.gen(bld, rerun_script), file=f)

        return 0


if __name__ == "__main__":
    exit(main())
