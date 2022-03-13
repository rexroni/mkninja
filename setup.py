import shutil
import os
import sys
import subprocess
import tempfile

import setuptools
import setuptools.dist

def find_cc():
    """
    distutils has tooling to detect compilers for a platform, but I find that
    it is not very good at detecting visual studio installs, meaning that I
    have to disable the detection a la [1] to get it to work.

    In windows, you need to be careful when you compile extensions that the
    MSVC C runtime you use matches what was used to compile the python
    interpreter, which would be a good reason to use something like maybe
    setuptools.extension.Extension to find the compiler.  But we're just
    compiling standalone executables, so that doesn't matter.

    Therefore we do the dumbest thing and look for the compiler ourself.

    [1] docs.python.org/3/distutils/apiref.html#module-distutils.msvccompiler
    """
    if "CC" in os.environ:
        return os.environ["CC"]

    if sys.platform == "win32":
        if shutil.which("cl"):
            return "cl"
        raise ValueError(
            "unable to find a compiler.  Is VisualStudio installed, and did "
            "you call the appropriate vcvars .bat file?"
        )

    if shutil.which("gcc"):
        return "gcc"
    if shutil.which("clang"):
        return "clang"
    raise ValueError("unable to find gcc or clang, is one installed?")

def compile_exe(cc, filein, fileout):
    basename = os.path.basename(fileout)
    if sys.platform == "win32":
        # windows
        assert cc == "cl"
        fileout = fileout + ".exe"
        cmd = [
            "cl",
            filein,
            # optimize for speed
            "/O2",
            # only Wall is higher than W4
            "/W4",
            # treat warnings as errors
            "/WX",
            # /wd4221, /wd4204: we don't care about ansi compliance
            "/wd4221",
            "/wd4204",
            # specify the output name
            "/link",
            f"/out:{fileout}.exe"
        ]
        subprocess.run(cmd, check=True)
        objfile = basename + ".obj"
        os.remove(objfile)
        return basename + ".exe"

    # unix
    cmd = [cc, "-o", fileout, filein, "-Wall", "-Wextra", "-Werror", "-O3"]
    subprocess.run(cmd, check=True)
    return basename


if __name__ == "__main__":
    setup_args = dict(
        name="mkninja",
        version="0.1.0",
        author="Rex Roni",
        author_email="rexroni@splintermail.com",
        description="A python front-end for ninja",
        # long_description=long_description,
        # long_description_content_type="text/markdown",
        url="https://github.com/rexroni/mkninja",
        classifiers=[
            "Programming Language :: Python :: 3",
            "License :: OSI Approved :: The Unlicense (Unlicense)",
            "Operating System :: OS Independent",
        ],
        packages=["mkninja"],
        python_requires=">=3.6",
        entry_points={"console_scripts": ["mkninja = mkninja.__main__:main"]}
    )

    # when building the source distribution, we set an environment variable to
    # ensure that we aren't including extraneous built binaries.
    if "MKNINJA_BUILD_SDIST" not in os.environ:
        cc = find_cc()
        manifest = compile_exe(cc, "manifest/manifest.c", "mkninja/manifest")
        findglob = compile_exe(cc, "findglob/main.c", "mkninja/findglob")
        # Include the executables (not part of MANIFEST.in) for non-sdist runs.
        setup_args["package_data"] = {"mkninja": [manifest, findglob]}

    setuptools.setup(**setup_args)
