import io
import os
import pathlib
import shlex
import sys
import textwrap
from importlib import machinery, util

_aliases = []
_proj = []
_src = []
_bld = []

# on reflection, I think these are a bad idea.
# They only work during import, so if you have a
# function that calls add_target() then it will
# misbehave when called from another file.
# def get_cur_src():
#     return _src[-1]
#
# def get_cur_bld():
#     return _bld[-1]


def add_target_object(target):
    """
    If you subclass the Target object, you could use mkninja.add_target_object
    to include it into the ninja file.
    """
    proj = _proj[-1]
    proj.targets.append(target)
    return target


def _add_target(
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
        workdir = get_cur_src()

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

    add_target_object(target)
    return target


def _make_add_target(default_workdir):

    def add_target(
        *,
        command,
        outputs=(),
        inputs=(),
        after=(),
        phony=False,
        workdir=default_workdir,
        dyndep=None,
        display=None,
    ):
        return _add_target(
            command=command,
            outputs=outputs,
            inputs=inputs,
            after=after,
            phony=phony,
            workdir=workdir,
            dyndep=dyndep,
            display=display,
        )

    return add_target


_manifest_bin = os.path.join(os.path.dirname(__file__), "manifest")
_findglob_bin = os.path.join(os.path.dirname(__file__), "findglob")
if sys.platform == "win32":
    _manifest_bin += ".exe"
    _findglob_bin += ".exe"


def _add_manifest(*, command, out, workdir, after=()):
    if isinstance(command, str):
        command = shlex.split(command)
    command = [shlex.quote(c) for c in command]
    return _add_target(
        inputs=[manifest_bin],
        command=["(" *command, ")", "|", _manifest_bin, out],
        outputs=[out],
        workdir=workdir,
        display=f"updating manifest: {' '.join(command)}",
        phony=True,
    )


def _make_add_manifest(default_workdir):

    def add_manifest(*, command, out, after=(), workdir=default_workdir):
        return _add_manifest(
            command=cmd, out=out, after=after, workdir=workdir
        )

    return add_manifest


def _add_glob(*patterns, out, workdir, after=()):
    if not patterns:
        raise ValueError("at least one pattern must be provided")
    patterns = [shlex.quote(p) for p in patterns]
    return _add_target(
        inputs=[_findglob_bin],
        command=[_findglob_bin, *patterns, "|", _manifest_bin, out],
        outputs=[out],
        workdir=workdir,
        display=f"findglob {' '.join(patterns)}",
        phony=True,
    )


def _make_add_glob(default_workdir):

    def add_glob(*patterns, out, after=(), workdir=default_workdir):
        return _add_glob(*patterns, out=out, after=after, workdir=workdir)

    return add_glob


## add_subproject needs more support from ninja itself before it is a good
## idea; currently the subninja command does not provide sufficient insulation
## to run a subproject in isolation and expect it to be unaffected.
# def add_subproject(name, alias="root"):
#     proj = _proj[-1]
#     src = _src[-1]/name
#     bld = _bld[-1]/name
#     root_bld = _bld[0]
#     with _Project(src, bld, name, alias) as p:
#         module = importlib.import_module(name)
#
#     proj.mkninja_files += p.mkninja_files
#
#     ninjafile = bld/"build.ninja"
#     with ninjafile.open("w") as f:
#         print(p.gen(root_bld, isroot=False), file=f)
#
#     proj.subprojects.append(ninjafile)
#
#     return module


class _Loader(machinery.SourceFileLoader):
    """
    Our Loader extends the built-in SourceFileLoader.  The SourceFileLoader
    is able to load and execute a python script from a file, but we want to
    expose specific values just before executing the module, specifically:
      - SRC: the current source file
      - BLD: the current build file
      - add_target(): adds a build target into the generated ninja file
      - add_manifest(): adds a target to build a manifest file
      - add_glob(): adds a target that writes a manifest by calling findglob
    """

    def __init__(self, fullname, path, proj, alias):
        self.proj = proj
        self.alias = alias
        super().__init__(fullname, path)

    def exec_module(self, module):
        global _src, _bld
        if self.alias is not None:
            sys.modules[self.alias] = module
            _aliases[-1][self.alias] = module
        relpath = "/".join(module.__name__.split(".")[1:])
        # set SRC
        src = self.proj.src/relpath
        setattr(module, "SRC", src)
        # set BLD
        bld = self.proj.bld/relpath
        setattr(module, "BLD", bld)
        # expose mkninja builtins
        setattr(module, "add_target", _make_add_target(src))
        setattr(module, "add_manifest", _make_add_manifest(src))
        setattr(module, "add_glob", _make_add_glob(src))
        # setattr(module, "add_subproject", add_subproject)
        try:
            # while executing this module, the default workdir should be src
            _bld.append(bld)
            _src.append(src)
            return super().exec_module(module)
        finally:
            _bld.pop()
            _src.pop()

    def set_data(self, *args, **kwarg):
        # We don't want to generate annoying __pycache__ directories in the
        # source tree, so we will disable caching of our mkninja.py files.
        raise NotImplementedError()


class _Finder:
    """
    When `import project.some.subdir` is encountered, load the file at
    project/some/subdir/mkninja.py instead of the normal import behavior.
    """
    def __init__(self, proj):
        self.proj = proj
        self.loaders = {}

    def find_spec(self, name, path=None, target=None):
        nameparts = name.split(".")
        base = nameparts[0]
        subs = nameparts[1:]
        if base not in (self.proj.truename, self.proj.alias):
            # Return None to let the next Finder in sys.meta_path take over.
            return None

        # target is None in the normal import case
        assert target is None, f"can't handle target={target}"

        truename = ".".join([self.proj.truename] + subs)
        if self.proj.alias is not None:
            alias = ".".join([self.proj.alias] + subs)
        else:
            alias = None


        # We want normal source-loading mechanics, with these exceptions:
        #  - we want to load the wrong source (fullname != path)
        #  - we want to pretend it's a package and not a module
        #    (submodule_search_locations is a list)

        # The code we use for the module root.sub is at root/sub/mkninja.py.
        submodule_path = "/".join(subs)
        src = str(self.proj.src / submodule_path / "mkninja.py")

        self.proj.mkninja_files.append(src)

        loader = _Loader(truename, src, self.proj, alias)

        return util.spec_from_file_location(
            truename, src, loader=loader, submodule_search_locations=[]
        )


def ninjify(s):
    """apply ninja syntax escapes"""
    s = str(s)
    s = s.replace("$", "$$")
    s = s.replace("\n", "$\n")
    s = s.replace(" ", "$ ")
    s = s.replace(":", "$:")
    return s


class Target:
    def __init__(
        self,
        *,
        command,
        outputs,
        inputs,
        after,
        phony,
        workdir,
        dyndep,
        display
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
        return self.outputs[0]

    def gen(self, bld):

        def relbld(s):
            s = str(s)
            if str(bld) in s:
                s = os.path.relpath(s, str(bld))
            return s

        out = f"build"
        if self.outputs:
            out += ' ' + ' '.join(ninjify(relbld(o)) for o in self.outputs)
        out += ": TARGET |"
        if self.inputs:
            out += ' ' + ' '.join(ninjify(relbld(i)) for i in self.inputs)
        if self.phony:
            out += " PHONY"
        out += " ||"
        if self.after:
            out += ' ' + ' '.join(ninjify(relbld(a)) for a in self.after)
        # if self.dyndep:
        #     out += " " + ninjify(self.dyndep)
        out += "\n CMD = " + " ".join(ninjify(c) for c in self.command)
        out += "\n WORKDIR = " + ninjify(self.workdir)
        if self.dyndep:
            out += "\n dyndep = " + ninjify(relbld(self.dyndep))
        if self.display:
            out += "\n DISPLAY = " + ninjify(relbld(self.display))
        return out


class _Project:
    def __init__(self, src, bld, truename, alias=None):
        self.src = pathlib.Path(src).absolute()
        self.bld = pathlib.Path(bld).absolute()
        self.truename = truename
        self.alias = alias
        self.finder = _Finder(self)
        self.targets = []
        self.mkninja_files = []
        self.subprojects = []

    def __enter__(self):
        sys.meta_path = [self.finder] + sys.meta_path
        _proj.append(self)
        # remove the current set of aliases
        if _aliases:
            for alias in _aliases[-1]:
                sys.modules.pop(alias)
        _aliases.append({})
        return self

    def __exit__(self, *_):
        sys.meta_path.remove(self.finder)
        _proj.pop()
        # remove any aliases we created
        for alias in _aliases[-1]:
            sys.modules.pop(alias)
        _aliases.pop()
        if _aliases:
            # restore our parent's aliases
            for alias, module in _aliases[-1].items():
                sys.modules[alias] = module

    def gen(self, bld, rerun_script=None, isroot=True):
        f = io.StringIO()

        for subproject, ninjafile in self.subprojects:
            print(
                "subproject", ninjify(subproject), ninjify(ninjafile), file=f
            )

        print(textwrap.dedent("""
            rule TARGET
             command = cd $WORKDIR && $CMD
             description = $DISPLAY
             restat = 1
        """).lstrip(), file=f)

        if isroot:
            print(textwrap.dedent("""
                # phony target is always out of date
                build PHONY: phony
            """).lstrip(), file=f)

        if rerun_script:
            mkninja_deps = [__file__]
            mkninja_deps += [ninjify(f) for f in self.mkninja_files]
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
            print(target.gen(bld), file=f, end="\n\n")

        # post-process the text to use $SRC and $BLD
        text = f.getvalue()
        bld = str(self.bld)
        src = str(self.src)
        if bld in src:
            text = text.replace(src, "$SRC")
            text = text.replace(bld, "$BLD")
        else:
            text = text.replace(bld, "$BLD")
            text = text.replace(src, "$SRC")

        return f"# global path variables\nBLD={bld}\nSRC={src}\n\n{text}"
