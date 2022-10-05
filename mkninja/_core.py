import io
import os
import pathlib
import shlex
import string
import sys
import textwrap
from importlib import machinery, util

_aliases = []


def _quote(s):
    return shlex.quote(str(s))


_manifest_bin = os.path.join(os.path.dirname(__file__), "manifest")
_findglob_bin = os.path.join(os.path.dirname(__file__), "findglob")
if sys.platform == "win32":
    _manifest_bin += ".exe"
    _findglob_bin += ".exe"


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
        m = _Module(self.proj, relpath)
        # expose mkninja builtins
        setattr(module, "SRC", m.src)
        setattr(module, "BLD", m.bld)
        setattr(module, "add_target", m.make_add_target())
        setattr(module, "add_manifest", m.make_add_manifest())
        setattr(module, "add_glob", m.make_add_glob())
        setattr(module, "add_series", m.add_series)
        setattr(module, "ALL", self.proj.ALL)
        # this one is undocumented
        setattr(module, "add_target_object", m.add_target_object)
        # setattr(module, "add_subproject", add_subproject)
        return super().exec_module(module)

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


def ninjify(s, allow_space=False):
    """apply ninja syntax escapes"""
    s = str(s)
    s = s.replace("$", "$$")
    if not allow_space:
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
        display,
        series,
        **tags,
    ):
        assert isinstance(outputs, (list, tuple)), type(outputs)
        assert isinstance(inputs, (list, tuple)), type(inputs)
        assert isinstance(after, (list, tuple)), type(after)
        assert all(isinstance(k, str) for k in tags), tags
        assert all(k == k.upper() for k in tags), tags
        tags = {k: str(v) for k, v in tags.items()}

        # expand tags that reference each other, the hacky way
        def expand_tag(s):
            for _ in range(100):
                old = s
                s = string.Template(str(s)).safe_substitute(tags)
                if old == s:
                    return s
            else:
                raise ValueError(
                    f"recursion limit exceeded while expanding tags={tags}"
                )
            return tags_out

        expanded_tags = {k: expand_tag(v) for k, v in tags.items()}

        # expanded tags are assigned as properties of a target
        for k, v in expanded_tags.items():
            setattr(self, k, v)

        # all other strings are expanded against the expanded_tags
        def expand(s):
            return string.Template(str(s)).safe_substitute(expanded_tags)

        temp = []
        for i in inputs:
            if hasattr(i, "as_input"):
                temp += i.as_input()
            else:
                temp.append(i)
        inputs = [expand(t) for t in temp]

        temp = []
        for o in outputs:
            if hasattr(o, "as_output"):
                temp += o.as_output()
            else:
                temp.append(o)
        outputs = [expand(t) for t in temp]

        temp = []
        for a in after:
            if hasattr(a, "as_after"):
                temp += a.as_after()
            else:
                temp.append(a)
        after = [expand(t) for t in temp]

        if isinstance(command, list):
            command = " ".join(_quote(c) for c in command)
        command = expand(command)

        if hasattr(dyndep, "as_dyndep"):
            dyndep = dyndep.as_dyndep()
        if dyndep:
            dyndep = expand(dyndep)

        series = series or []
        if isinstance(series, Series):
            series = [series]
        assert all(isinstance(s, Series) for s in series), series

        self.inputs = inputs
        self.after = after
        self.command = command
        self.outputs = outputs
        self.workdir = workdir
        self.phony = phony
        self.dyndep = dyndep
        self.display = display
        self.series = series
        self.tags = expanded_tags

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
        out += "\n CMD = " + ninjify(self.command, allow_space=True)
        out += "\n WORKDIR = " + ninjify(self.workdir, allow_space=True)
        if self.dyndep:
            out += "\n dyndep = " + ninjify(relbld(self.dyndep), True)
        if self.display:
            out += "\n DISPLAY = " + ninjify(relbld(self.display), True)
        return out

    def __str__(self):
        assert len(self.outputs) == 1, (
            "only targets with exactly one output support implicit string "
            f"conversions; this target has outputs {outputs}"
        )
        return str(self.outputs[0])


class Series:
    """
    A Series is a classification of target, which may exist in all directories
    under some root (most commonly the root of the whole project).

    The default series is "all", so `ninja all` is defined, as does
    `ninja subdir/all` for any subdir with a corresponding `mkninja.py` file.
    """
    def __init__(self, name, proj, relpath):
        self.name = name
        self._proj = proj
        self._relpath = relpath

    def __str__(self):
        return self.name


class _Module:
    def __init__(self, proj, relpath):
        self.proj = proj
        self.relpath = relpath
        self.src = proj.src/relpath
        self.bld = proj.bld/relpath
        self.targets = []

        proj.modules[relpath] = self

    def add_target_object(self, target):
        """
        If you subclass the Target object, you could use add_target_object
        to include it into the ninja file.
        """
        self.targets.append(target)
        self.proj.targets.append(target)
        return target

    def _add_target(
        self,
        *,
        command,
        workdir,
        series,
        outputs=(),
        inputs=(),
        after=(),
        phony=False,
        dyndep=None,
        display=None,
        **tags,
    ):
        target = Target(
            command=command,
            outputs=outputs,
            inputs=inputs,
            after=after,
            phony=phony,
            workdir=workdir,
            dyndep=dyndep,
            display=display,
            series=series,
            **tags,
        )

        return self.add_target_object(target)

    def make_add_target(self):
        def add_target(
            *,
            command,
            outputs=(),
            inputs=(),
            after=(),
            phony=False,
            workdir=self.src,
            dyndep=None,
            display=None,
            series=self.proj.ALL,
            **tags,
        ):
            return self._add_target(
                command=command,
                outputs=outputs,
                inputs=inputs,
                after=after,
                phony=phony,
                workdir=workdir,
                dyndep=dyndep,
                display=display,
                series=series,
                **tags,
            )

        return add_target

    def make_add_manifest(self):
        def add_manifest(
            *, command, out, after=(), workdir=self.src, **tags,
        ):
            if isinstance(command, list):
                command = " ".join(_quote(c) for c in command)
            return self._add_target(
                inputs=[],
                command=(
                    f"( {command} ) | {_quote(_manifest_bin)} {_quote(out)}"
                ),
                outputs=[out],
                workdir=workdir,
                display=f"updating manifest: {command}",
                phony=True,
                series=None,
                **tags,
            )

        return add_manifest

    def make_add_glob(self):
        def add_glob(
            *patterns, out, workdir=None, after=(), **tags
        ):
            if not patterns:
                raise ValueError("at least one pattern must be provided")
            patterns = [_quote(str(p)) for p in patterns]
            return self._add_target(
                inputs=[],
                command=(
                    f"{_quote(_findglob_bin)} "
                    f"{' '.join(patterns)} "
                    f"| {_quote(_manifest_bin)} {_quote(out)}"
                ),
                outputs=[out],
                workdir=workdir or self.src,
                after=after,
                display=f"findglob {' '.join(patterns)}",
                phony=True,
                series=None,
                **tags,
            )

        return add_glob

    def add_series(self, name):
        if name in ["clean", "all"]:
            raise ValueError(f"series name '{name}' is reserved")
        existing = self.proj.series.get(name)
        if existing:
            relmod = existing._relpath.replace("/", ".")
            mod = f"root.{relmod}" if relmod else "root"
            raise ValueError(
                f"series name '{name}' is already in use (defined in {mod})"
            )
        series = Series(name, self.proj, self.relpath)
        self.proj.series[name] = series
        return series

    def _get_series_outputs(self, series):
        series_targets = [t for t in self.targets if series in t.series]
        return [o for t in series_targets for o in t.outputs]


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
        self.modules = {}
        # start with default series
        self.ALL = Series("all", self, "")
        self.series = {"all": self.ALL}

    def __enter__(self):
        sys.meta_path = [self.finder] + sys.meta_path
        # remove the current set of aliases
        if _aliases:
            for alias in _aliases[-1]:
                sys.modules.pop(alias)
        _aliases.append({})
        return self

    def __exit__(self, *_):
        sys.meta_path.remove(self.finder)
        # remove any aliases we created
        for alias in _aliases[-1]:
            sys.modules.pop(alias)
        _aliases.pop()
        if _aliases:
            # restore our parent's aliases
            for alias, module in _aliases[-1].items():
                sys.modules[alias] = module

    def _gen_series(self, bld, f, series):

        def relbld(s):
            s = str(s)
            if str(bld) in s:
                s = os.path.relpath(s, str(bld))
            return s

        print(f'# "{series}"-series targets', file=f)
        for relpath, module in sorted(self.modules.items()):
            if not relpath.startswith(series._relpath):
                continue
            name = ninjify(pathlib.Path(relpath)/f"_{series}")
            targets = [t for t in module.targets if series in t.series]
            outputs = [o for t in targets for o in t.outputs]
            deps = ' '.join(ninjify(relbld(o)) for o in outputs)
            print(f"build {name}: phony {deps}", file=f)
        print(file=f)

        for relpath, module in sorted(self.modules.items()):
            if not relpath.startswith(series._relpath):
                continue
            if series.name == "all":
                # the "all" series has the special property that each
                # directory's target is just the name of that directory
                name = ninjify(relpath or "all")
            else:
                name = ninjify(pathlib.Path(relpath)/series.name)
            children = [r for r in self.modules if r.startswith(relpath)]
            alltgts = [pathlib.Path(c)/f"_{series}" for c in children]
            alldeps = ' '.join(ninjify(relbld(a)) for a in alltgts)
            print(f"build {name}: phony {alldeps}", file=f)
        print(file=f)

    def _gen_clean(self, bld, f):
        """Sort of like _gen_series() but obnoxiously different."""

        def relbld(s):
            s = str(s)
            if str(bld) in s:
                s = os.path.relpath(s, str(bld))
            return s

        print(f'# "clean"-series targets', file=f)
        for relpath, module in sorted(self.modules.items()):
            name = ninjify(pathlib.Path(relpath)/"_clean")
            outputs = [o for t in module.targets for o in t.outputs]
            args = ' '.join(ninjify(relbld(o)) for o in outputs)
            print(f"build {name}: TARGET | PHONY ||", file=f)
            print(f" CMD = rm -rf {args}", file=f)
            print(" WORKDIR = .", file=f)
            print(f" DISPLAY = cleaning {relpath or 'root'}", file=f)
        print(file=f)

        for relpath, module in sorted(self.modules.items()):
            name = ninjify(pathlib.Path(relpath)/"clean")
            children = [r for r in self.modules if r.startswith(relpath)]
            alltgts = [pathlib.Path(c)/f"_clean" for c in children]
            alldeps = ' '.join(ninjify(relbld(a)) for a in alltgts)
            print(f"build {name}: phony {alldeps}", file=f)
        print(file=f)

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

        for name, series in sorted(self.series.items()):
            self._gen_series(bld, f, series)

        self._gen_clean(bld, f)

        print(f"default all", file=f)

        # post-process the text to use $SRC and $BLD
        text = f.getvalue().rstrip()
        bld = str(self.bld)
        src = str(self.src)
        if bld in src:
            text = text.replace(src, "$SRC")
            text = text.replace(bld, "$BLD")
        else:
            text = text.replace(bld, "$BLD")
            text = text.replace(src, "$SRC")

        return f"# global path variables\nBLD={bld}\nSRC={src}\n\n{text}"
