# mkninja: A python frontend for ninja files

`mkninja` is like [CMake](https://cmake.org/) or
[Meson](https://mesonbuild.com/) in that it is a metabuild system that generates
a build system.  `mkninja` is also like [Tup](https://gittup.org/tup/) or
[ninja](https://ninja-build.org/) itself in that it is 100% language agnostic.

## Why would I use mkninja?

You might want to use mkninja for the following reasons:

- You are in a project that needs a wrapper build system around several
  unrelated sub-build systems (often in a cross-language tech stack)
- You want to execute a large number of arbitrary actions in a build graph
  (perhaps treating a server configuration as a build system problem)
- You don't like alternative build tools and you want something with an easy
  learning curve.

## Why would I not use mkninja?

- You are building a single-language project where some existing build system
  has tons of great tooling for you to leverage (such as C++ and CMake).
- You want something that is polished while `mkninja` is still experimental.

## How does mkninja work?

`mkninja` is close in spirit to CMake.  Instead of using `CMakeLists.txt` and
writing in CMake script, you use `mkninja.py` files and you write normal
python.

You generate the ninja files once with:

```
cd /path/to/build && /path/to/mkninja.py /path/to/src
```

After initial configuration, `mkninja` will automatically regenerate its
`build.ninja` files any time you run `ninja` and any of the `mkninja.py` files
have been updated, much like CMake or Meson.

When mkninja runs, it imports the top-level `mkninja.py` of your project as
the special module name `root`.  If you have another `mkninja.py in a
subdirectory like `tools/mkninja.py` you may import it as a python module as
`root.tools` and use it as you would normally use a python module.

Note that only the `mkninja.py` in the root directory is automatically
imported.  Any subdirectories containing `mkninja.py` files must be need to be
explicitly imported to take effect.

Every `mkninja.py` file has access to the following special values (without
importing anything):
  - `SRC`: a `pathlib.Path` object pointing to source directory in which that
    `mkninja.py` file exists.  Think `${CMAKE_CURRENT_SOURCE_DIR}`.
  - `BLD`: a `pathlib.Path` object pointing to the directory corresponding to
    `SRC` but in the build tree.  Think `${CMAKE_CURRENT_BULID_DIR}`.
  - `add_target()`: a function for adding a new build edge (in ninja
    terminology) to the generated `build.ninja` file.  `add_target()` has the
    following keyword-only args:
      - `command`: a list of tokens as to how the command should run.  You
        are allowed to provide a single string, which will be first tokenized
        via `shlex.split()`.
      - `outputs`: a list of output files that the command should generate.
      - `inputs`: a list of input files that the command will read as inputs.
      - `after`: a list of order-only dependencies of the target.
      - `phony`: a boolean indicating that the target should always be
        considered out-of-date.  Note that `make` has the obnoxious behavior
        that any output that depends on a PHONY rule is effectively PHONY
        itself.  The same is not true under `mkninja.py`.
      - `workdir`: a directory to `cd` into before launching the `command`.
      - `display`: a string of text to display while this step is running.
      - `dyndep`: a file or target (which must also be in `inputs`) that ninja
        should treat as a dynamic dependency file.  See ninja docs for details.

    `add_target()` returns a `mkninja.Target` object which has attrubutes
    `inputs`, `outputs`, and `after` which may be useful.  Additionally, the
    `Target` object may be passed into `inputs` or `after` directly.

Additionally, to assist in writing tooling that can be reused throughout a
build system, the `mkninja` module also includes a couple extra methods that
you can access:
  - `mkninja.get_cur_src()`: like the `SRC` value that is exposed to each
    `mkninja.py`, except it returns the `SRC` of the current file being
    imported.
  - `mkninja.get_cur_bld()`: the same thing but for the current build
    directory.

An example where you would use these functions if you wanted to have a helper
function in one module that called `add_target()` with automatic output to the
subdirectory where the helper function is called.
