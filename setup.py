import os
import pathlib
import sys

import setuptools
import distutils.extension
import distutils.command.build_ext

# We subclass the build_ext command to build executables instead of building
# python extensions based on [1].

# This is sort of a hack to trick setuptools into thinking that the package is
# not pure.  Otherwise, if we simply compile the binaries and put them into
# place when we build the wheel, the wheel gets improperly labeled with a
# "Root-is-purelib: true" tag, then auditwheel pukes when it finds compiled
# binaries in a purelib directory (see [2]).
#
# Cons of this strategy:
#  - it is far more opaque than just compiling the files ourselves
#  - it is abusing setuptools/distutils to do something other than intended
#
# Pros of this strategy:
#  - we get to use cibuildwheel to build a ton of wheels in a github action
#  - it seems to pick up the compiler pretty reliably, even on windows
#  - it is the only way I know how to get a PEP-427-compliant wheel
#
# [1] github.com/pypa/packaging-problems/issues/542#issuecomment-912838470
# [2] peps.python.org/pep-0427/#what-s-the-deal-with-purelib-vs-platlib

ext_modules = [
    distutils.extension.Extension("mkninja.findglob", ["findglob/main.c"]),
    distutils.extension.Extension("mkninja.manifest", ["manifest/manifest.c"]),
]

class build_exe(distutils.command.build_ext.build_ext):
    """
    Subclass the build_ext command so we can compile executables instead of
    shared objects.
    """

    ## allow the default .run() to configure and setup the compiler.
    # def run(self):
    #    ...

    def build_extensions(self):
        for ext in self.extensions:
            objs = self.compiler.compile(
                ext.sources,
                output_dir=self.build_temp,
                debug=self.debug,
                extra_postargs=self.compile_postargs(),
            )
            self.compiler.link_executable(
                objs,
                self.get_executable_output(ext),
                debug=self.debug,
                target_lang="c",
            )

    def compile_postargs(self):
        if sys.platform == "win32":
            return [
                # optimize for speed
                "/O2",
                # only Wall is higher than W4
                "/W4",
                # treat warnings as errors
                "/WX",
                # /wd4221, /wd4204: we don't care about ansi compliance
                "/wd4221",
                "/wd4204",
            ]

        return ["-Wall", "-Wextra", "-Werror", "-O3"]

    def get_executable_output(self, ext):
        return os.path.join(self.build_lib, *ext.name.split("."))

    def get_outputs(self):
        return [self.get_executable_output(ext) for ext in self.extensions]


if __name__ == "__main__":
    HERE = pathlib.Path(__file__).parent
    with (HERE / "mkninja" / "__init__.py").open() as f:
        firstline = next(iter(f))
        assert firstline.startswith("__version__ = ")
        version = firstline.split('"')[1]

    readme = (HERE / "README.md").read_text()

    setup_args = dict(
        name="mkninja",
        version=version,
        author="Rex Roni",
        author_email="rexroni@splintermail.com",
        description="A python front-end for ninja",
        long_description=readme,
        long_description_content_type="text/markdown",
        url="https://github.com/rexroni/mkninja",
        classifiers=[
            "Programming Language :: Python :: 3",
            "License :: OSI Approved :: The Unlicense (Unlicense)",
            "Operating System :: OS Independent",
        ],
        packages=["mkninja"],
        python_requires=">=3.6",
        entry_points={"console_scripts": ["mkninja = mkninja.__main__:main"]},
        ext_modules=ext_modules,
        cmdclass={"build_ext": build_exe},
    )

    setuptools.setup(**setup_args)
