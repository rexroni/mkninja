import argparse
import importlib
import pathlib
import sys
from importlib import machinery, util


class _Loader(machinery.SourceFileLoader):
    """
    Our Loader extends the built-in SourceFileLoader.  The SourceFileLoader
    is able to load and execute a python script from a file, but we want to
    expose specific values just before executing the module, specifically:
      - SRC: the current source file
      - BUILD: the current build file
      - add_target(): adds a build target into the generated ninja file
      - root: the top-level module for the project (so as to avoid needing to
        know the top-level module name to import it)
    """

    def __init__(self, fullname, path, proj):
        self.proj = proj
        super().__init__(fullname, path)

    def exec_module(self, module):
        # set SRC
        src = pathlib.Path(module.__path__[0])
        setattr(module, "SRC", src)
        # set BUILD
        build = "/".join(module.__name__.split(".")[1:])
        setattr(module, "BUILD", self.proj.build/build)
        # set add_target
        setattr(module, "add_target", self.proj.make_add_target(src))
        # set root
        if self.proj.root is None:
            # we know the root of any package must be imported first
            self.proj.root = module
        setattr(module, "root", self.proj.root)
        return super().exec_module(module)


class _Finder:
    def __init__(self, proj):
        self.proj = proj

    def find_spec(self, name, path=None, target=None):
        if name.split(".")[0] != self.proj.name:
            # Return None to let the next Finder in sys.meta_path take over.
            return None
        # target is None in the normal import case
        assert target is None, f"can't handle target={target}"

        # We want normal source-loading mechanics, with these exceptions:
        #  - we want to load the wrong source (fullname != path)
        #  - we want to pretend it's a package and not a module
        #    (submodule_search_locations is a list)

        # The code we use for the module root.sub is at root/sub/mkninja.py.
        submodule_path = "/".join(name.split(".")[1:])
        src = str(self.proj.src / submodule_path / "mkninja.py")

        self.proj.mkninja_files.append(src)

        loader = _Loader(name, src, self.proj)

        return util.spec_from_file_location(
            name, src, loader=loader, submodule_search_locations=[]
        )


class Target:
    def __init__(
        self, inputs, command, outputs, phony, workdir
    ):
        self.inputs = inputs
        self.command = command
        self.outputs = outputs
        self.workdir = workdir
        self.phony = phony

    def __repr__(self):
        out = f"build"
        if self.outputs:
            out += ' ' + ' '.join(str(o) for o in self.outputs)
        out += ": TARGET |"
        if self.inputs:
            out += ' ' + ' '.join(str(i) for i in self.inputs)
        if self.phony:
            out += " PHONY"
        out += "\n CMD =" + " ".join(str(c) for c in self.command)
        return out


class Project:
    def __init__(self, name, src, build):
        self.name = name
        self.src = pathlib.Path(src)
        self.build = pathlib.Path(build)
        self.finder = _Finder(self)
        self.targets = []
        self.mkninja_files = []
        # root is the top-level module in the project
        self.root = None

    def __enter__(self):
        sys.meta_path = [self.finder] + sys.meta_path

    def __exit__(self, *_):
        sys.meta_path.remove(self.finder)

    def make_add_target(self, default_workdir):

        def add_target(
            inputs=(),
            command=(),
            outputs=(),
            phony=False,
            workdir=default_workdir,
        ):
            target = Target(
                inputs, command, outputs, phony, workdir
            )
            self.targets.append(target)
            return target

        return add_target


if __name__ == "__main__":
    parser = argparse.ArgumentParser("mkninja")
    parser.add_argument("src")
    args = parser.parse_args()

    src = pathlib.Path(args.src).absolute()
    build = pathlib.Path(".").absolute()

    root_module_name = "arbitrary_module_name"

    # We don't need the src directory in our sys.path because we have a custom
    # Finder on the sys.metapath.

    with Project(root_module_name, src, build):
        importlib.import_module(root_module_name)
