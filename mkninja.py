import argparse
import importlib
import pathlib
import sys
import shlex
import textwrap
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
        relpath = "/".join(module.__name__.split(".")[1:])
        # set SRC
        src = self.proj.src/relpath
        setattr(module, "SRC", src)
        # set BUILD
        build = self.proj.build/relpath
        setattr(module, "BUILD", build)
        # set root
        if self.proj.root is None:
            # we know the root of any package must be imported first
            self.proj.root = module
        setattr(module, "root", self.proj.root)
        # expose project methods
        setattr(module, "add_target", self.proj.add_target)
        setattr(module, "add_target_object", self.proj.add_target_object)
        try:
            # while executing this module, the default workdir should be src
            self.proj.default_workdir.append(src)
            return super().exec_module(module)
        finally:
            self.proj.default_workdir.pop()


class _Finder:
    """
    When `import project.some.subdir` is encountered, load the file at
    project/some/subdir/mkninja.py instead of the normal import behavior.
    """
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


def _ninjify(s):
    """apply ninja syntax escapes"""
    s = str(s)
    s = s.replace("$", "$$")
    s = s.replace("\n", "$\n")
    s = s.replace(" ", "$ ")
    s = s.replace(":", "$:")
    return s


class Target:
    def __init__(
        self, *, command, outputs, inputs, after, phony, workdir, dyndep, display
    ):
        assert isinstance(outputs, (list, tuple)), type(outputs)
        assert isinstance(inputs, (list, tuple)), type(inputs)
        assert isinstance(after, (list, tuple)), type(after)

        temp = []
        for i in inputs:
            if hasattr(i, "as_input"):
                temp += i.as_input()
            else:
                temp.append(i)
        inputs = temp

        temp = []
        for a in after:
            if hasattr(a, "as_after"):
                temp += a.as_after()
            else:
                temp.append(a)
        after = temp

        if isinstance(command, str):
            command = shlex.split(command)

        if hasattr(dyndep, "as_dyndep"):
            dyndep = dyndep.as_dyndep()

        self.inputs = inputs
        self.after = after
        self.command = command
        self.outputs = outputs
        self.workdir = workdir
        self.phony = phony
        self.dyndep = dyndep
        self.display = display

    def as_after(self):
        return self.outputs

    def as_input(self):
        return self.outputs

    def as_dyndep(self):
        assert len(self.outputs) == 1, (
            "passing a Target as a dyndep requires that the target have "
            "exactly one output"
        )
        return outputs[0]

    def gen(self):
        out = f"build"
        if self.outputs:
            out += ' ' + ' '.join(_ninjify(o) for o in self.outputs)
        out += ": TARGET |"
        if self.inputs:
            out += ' ' + ' '.join(_ninjify(i) for i in self.inputs)
        if self.phony:
            out += " PHONY"
        out += " ||"
        if self.after:
            out += ' ' + ' '.join(_ninjify(a) for a in self.after)
        if self.dyndep:
            out += " " + _ninjify(self.dyndep)
        out += "\n CMD = " + " ".join(_ninjify(c) for c in self.command)
        out += "\n WORKDIR = " + _ninjify(self.workdir)
        if self.dyndep:
            out += "\n dyndep = " + _ninjify(self.dyndep)
        if self.display:
            out += "\n DISPLAY = " + _ninjify(self.display)
        return out


class Project:
    def __init__(self, name, src, build):
        self.name = name
        self.src = pathlib.Path(src)
        self.build = pathlib.Path(build)
        self.finder = _Finder(self)
        # root is the top-level module in the project
        self.root = None
        self.targets = []
        self.mkninja_files = []
        self.default_workdir = []

    def __enter__(self):
        sys.meta_path = [self.finder] + sys.meta_path
        return self

    def __exit__(self, *_):
        sys.meta_path.remove(self.finder)

    def add_target_object(self, target):
        self.targets.append(target)
        return target

    def add_target(
        self,
        *,
        command=(),
        outputs=(),
        inputs=(),
        after=(),
        phony=False,
        workdir=None,
        dyndep=None,
        display=None,
    ):
        if workdir is None:
            workdir = self.default_workdir[-1]

        target = Target(
            command=command,
            outputs=outputs,
            inputs=inputs,
            after=after,
            phony=phony,
            workdir=workdir,
            dyndep=dyndep,
            display=display,
        )
        self.targets.append(target)
        return target

    def gen(self, f, rerun_script=None):
        print(textwrap.dedent("""
            rule TARGET
             command = cd $WORKDIR && $CMD
             description = $DISPLAY
             restat = 1

            # phony target is always out of date
            build PHONY: phony
        """).lstrip(), file=f)

        if rerun_script:
            mkninja_deps = [__file__]
            mkninja_deps += [_ninjify(f) for f in self.mkninja_files]
            mkninja_deps = " ".join(mkninja_deps)
            print(textwrap.dedent(f"""
                # regenerate ninja files based on the original command line
                build build.ninja: TARGET {rerun_script} {mkninja_deps}
                 CMD = sh {rerun_script}
                 WORKDIR = .
                 DISPLAY = regenerating ninja files
                 generator = 1
            """).lstrip(), file=f)

        for target in self.targets:
            print(target.gen(), file=f, end="\n\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser("mkninja")
    parser.add_argument("src")
    args = parser.parse_args(sys.argv[1:])

    src = pathlib.Path(args.src).absolute()
    build = pathlib.Path(".").absolute()

    root_module_name = "arbitrary_module_name"

    # store the exact command we ran with
    rerun_script = build / ".rerun_mkninja.sh"
    with rerun_script.open("w") as f:
        print("#!/bin/sh", file=f)
        quoted = [shlex.quote(sys.executable)]
        quoted += [shlex.quote(arg) for arg in sys.argv]
        print(*quoted, file=f)

    # We don't need the src directory in our sys.path because we have a custom
    # Finder on the sys.metapath.

    with Project(root_module_name, src, build) as p:
        importlib.import_module(root_module_name)

    with open("build.ninja", "w") as f:
        p.gen(f, rerun_script)
