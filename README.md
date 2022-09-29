# mkninja: A python frontend for ninja files

`mkninja` is like [CMake](https://cmake.org/) or
[Meson](https://mesonbuild.com/) in that it is a metabuild system that
generates a build system.  `mkninja` is also like
[Tup](https://gittup.org/tup/) or [ninja](https://ninja-build.org/) itself in
that it is 100% language agnostic.

## Why would I use mkninja?

You might want to use `mkninja` for the following reasons:

- You are in a project that needs a wrapper build system around several
  unrelated sub-build systems (often in a cross-language tech stack).
- You want to execute a large number of arbitrary actions in a build graph
  (perhaps treating a server configuration as a build system problem).
- You don't like alternative build tools and you want something with an easy
  learning curve.

## Why would I not use mkninja?

You might decide not to use `mkninja` for the following reasons:

- You are building a single-language project where some existing build system
  has tons of great tooling for you to leverage (such as C++ and CMake).
- You want something that is polished while `mkninja` is still experimental.

## How do I install mkninja?

You can install `mkninja` via `pip`:

```
pip install mkninja
```

## How does mkninja work?

`mkninja` is close in spirit to CMake.  Instead of using `CMakeLists.txt` and
writing in CMake script, you use `mkninja.py` files and you write normal
python.

You generate the ninja files once with:

```
cd /path/to/build && mkninja /path/to/src
```

After initial configuration, `mkninja` will automatically regenerate its
`build.ninja` files any time you run `ninja` and any of the `mkninja.py` files
have been updated, much like CMake or Meson.

When `mkninja` runs, it imports the top-level `mkninja.py` of your project as
the special module name `root`.  If you have another `mkninja.py` in a
subdirectory like `tools/mkninja.py` you may import it with `import root.tools`
and use it as you would normally use a python module.

Note that only the `mkninja.py` in the root directory is automatically
imported.  Any subdirectories containing `mkninja.py` files must be need to be
explicitly imported to take effect.

## API Reference

Every `mkninja.py` file has access to the following special values (without
importing anything):

  - `SRC`
  - `BLD`
  - `add_target()`
  - `add_manifest()`
  - `add_glob()`

### `SRC`

`SRC` is a `pathlib.Path` object pointing to source directory in which that
`mkninja.py` file exists.  Think `${CMAKE_CURRENT_SOURCE_DIR}`.

### `BLD`

`BLD` is a `pathlib.Path` object pointing to the directory corresponding to
`SRC` but in the build tree.  Think `${CMAKE_CURRENT_BULID_DIR}`.

### `add_target()`

`add_target()` adds a new target (or "build edge" in ninja terminology) to
the generated `build.ninja` file.  `add_target()` has the following
keyword-only arguments:
  - `command`: a list of tokens as to how the command should run.  You
    are allowed to provide a single string, which will be first tokenized
    via `shlex.split()`.
  - `outputs`: a list of output files that the command should generate.
  - `inputs`: a list of input files or `mkninja.Target` objects that the
    command depends on.
  - `after`: a list of order-only dependencies of the target.  Items may be
    output files from another `mkninja.Target` or another `mkninja.Target`
    object itself.
  - `phony`: a boolean indicating that the target should always be
    considered out-of-date.  Note that `make` has the obnoxious behavior
    that any target that depends on a PHONY target is also always
    considered out-of-date.  The same is not true under `mkninja.py`.
  - `workdir`: a directory to `cd` into before launching the `command`,
    defaults to `SRC`.
  - `display`: a string of text to display while this step is running.
  - `dyndep`: a file or target (which must also be in `inputs`) that ninja
    should treat as a dynamic dependency file.  See ninja docs for details.

`add_target()` returns a `mkninja.Target` object which has attrubutes
`inputs`, `outputs`, and `after` which may be useful to read (but which
probably shouldn't be modified).  Additionally, the `mkninja.Target` object
may be passed into `inputs` or `after` directly.

### `add_manifest()`

`add_manifest()` adds a target to track a dynamic list of files.  The
`command` argument should write a list of files, one per line, to stdout.
Each time this target runs, the `command` is run and piped to the
`manifest` binary (packaged with `mkninja`).  `manifest` will ensure that
the `out` file contains an up-to-date list of files and that the `out` file
is always newer than any of the files in the list.  In this way, another
target which depends on the `out` file can be thought of as depending on
the entire list of files; it will be rebuilt any time that the list of
files changes, or any time that a file in the list is modified.

`add_manifest()` has the following keyword-only arguments:

  - `command`: the command to generate the stdin to the `manifest` binary.
    As with `add_target()`, `command` may be either a list of tokens or a
    single string.
  - `out`: the output file to be generated by the `manifest` file.
  - `after`: a list of order-only dependencies before building the manifest
  - `workdir`: a directory to `cd` into before launching the `command`,
    defaults to `SRC`.

`add_manifest()` returns a `mkninja.Target`.

### `add_glob()`

`add_glob()` adds a new target which uses the `findglob` binary (packaged
with `mkninja`) to search for files matching the patterns provided as
arguments.  The result is piped into the `manifest` binary (described
above), so that another target which depends on the output of `add_glob()`
will effectively depend on all the files matching the patterns provided.
`add_glob()` has the following arguments:

  - `*patterns`: a list of patterns to pass as command-line arguments to
    the `findglob` binary.  You might use `"*.c"` to list all of the `.c`
    files in the current directory, or `"**/*.c"` to list all of the `.c`
    files in the directory tree rooted at `.`.  See the full
    output of `findglob --help` below for more details about patterns.
  - `out`: where to write the manifest file to
  - `after`: a list of order-only dependencies before searching for files
  - `workdir`: a diretory to `cd` into before launching `findglob`, defaults to
    `SRC`.

`add_glob()` returns a `mkninja.Target`.

## Appendix A: `findglob --help` output

`findglob` is what runs in the ninja build edge created by `add_glob()` and so
knowing how it works will help you choose patterns for `add_glob()`.

```
findglob will find matching files and directories and write them to stdout.

usage: findglob PATTERN... [ANTIPATERN...]

examples:

    # find all .c files below a directory
    findglob '**/*.c'

    # find all .c AND .h files below a directory
    findglob '**/*.c' '**/*.h'

    # find all .c AND .h files below a directory, while avoid searching
    # through the .git directory
    findglob '**/*.c' '**/*.h' '!.git'

    # find all .py files below a directory, while avoid searching through
    # the git directory or any __pycache__ directories
    findglob '**/*.py' '!.git' '!**/__pycache__'

    # find all .c files below a directory but ignore any .in.c files
    findglob '**/*.c' '!**/*.in.c'

Some details of how patterns work:

  - a PATTERN starting with ** will begin searching in $PWD

  - a PATTERN starting with prefix/** will begin searching at prefix/

  - PATTERNs of a/** and b/** will search a/ and b/ in sequence

  - PATTERNs of **/a and **/b will search $PWD once for files named a or b,
    because they have the same start point ($PWD)

  - PATTERNs of a/** and a/b/** will search a/ once, since the start point
    of the first pattern is a parent of the start point of the second

  - PATTERNs ending with a file separator ('/') will only match directories

  - ANTIPATTERNs start with a '!', and cause matching files to not be
    printed and matching directories to not be searched

  - ANTIPATTERNs follow the same startpoint rules, so !**/.git will prevent
    matching anything beneath $PWD named .git, while !/**/.git, which has a
    start point of / will prevent matching anything named .git across the
    entire filesystem.  Unlike PATTERNs, an ANTIPATTERN with a start point
    of '/' is not enough to cause findglob to search through all of '/'.

  - PATTERNs and ANTIPATTERNs may have types.  Presently only dir-types and
    file-types (really, non-dir-types) exist.  Dir-type patterns will match
    directories but not files, file-types will match files but not dirs,
    and untyped patterns will match either.  Dir-type patterns may be
    specified with a trailing file separator (/).  File-type patterns must
    be specified with the extended syntax.

  - on Windows, using '\' as a separator is not allowed; use '/' instead

Extended syntax:

  - Extended-syntax patterns begin with a ':', followed by zero or more
    flags, followed by another ':', followed by the pattern.  The following
    flags are currently supported:

      - ! -> an ANTIPATTERN
      - f -> match against files
      - d -> match against directories
      - if no type flag is supplied, it matches all types

   Example:
       # find files (not dirs) named 'build' except those in build dirs:
       findglob ':f:**/build' ':!d:**/build'
```
