# Polybuild

A simple and declarative Makefile generator designed for humans.

## Usage

Run `polybuild init` to create a starter `Polybuild.toml`, or write one at your project's root:

```toml
[paths]
output = "polybuild" # Where to put the final result (no default)
source = ["."] # Where to find .c/.cpp files (no default)
include = ["include"] # Equivalent to the -I option of a compiler (default: empty)
library = ["lib"] # Equivalent to the -L option of a compiler (default: your system default C++ library paths)
artifact = "obj" # Where to put object files (default: obj)
install = "/usr/local/bin" # Where to put the output binary when `make install` is executed (default: empty)

[options]
c-compiler = "gcc" # The C compiler to use (default: your system default C compiler)
cpp-compiler = "g++" # The C++ compiler to use (default: your system default C++ compiler)
compiler = "g++" # Alias for cpp-compiler
c-compilation-flags = "-Wall -O3" # Options passed to the compiler (default: your system default C compiler flags)
cpp-compilation-flags = "-Wall -std=c++17 -O3" # Options passed to the compiler (default: your system default C++ compiler flags)
compilation-flags = "-Wall -std=c++17 -O3" # Alias for cpp-compilation-flags
link-time-flags = "-lX11" # Options passed to the compiler only at link time, placed after /link on Windows (default: LDFLAGS)
libraries = ["ssl"] # Equivalent to the -l option of a compiler (default: empty)
static-libraries = ["lib/example.a"] # Archive paths to link and track as prerequisites (default: empty)
pkg-config-libraries = ["gstreamer-1.0"] # A list of libraries added to `compilation-flags` and `libraries` with `pkg-config`
preludes = ["echo this is an arbitrary command that always runs", "echo these commands may execute in parallel"] # (default: empty)
clean-preludes = ["echo this is an arbitrary command that runs with the clean target"] # (default: empty)
shared = false # Equivalent to the -shared and -fPIC options of a compiler (default: false)
static = false # Equivalent to the -static option of a compiler (default: false)

# Environment variables can be used to change Makefile behavior at runtime
[env.OS.Windows_NT]
paths.library = ["winlib"]
paths.install = "C:\\Windows\\System32"
options.compiler = "clang-cl" # c-compiler and cpp-compiler can be overrided as well
options.compilation-flags = "/W3 /std:c++17 /O2" # c-compilation-flags and cpp-compilation-flags can be overrided as well
options.link-time-flags = "/SUBSYSTEM:WINDOWS"
options.libraries = ["libssl.lib", "libcrypto.lib", "ws2_32.lib"]
options.pkg-config-libraries = ["gstreamer-1.0", "glew"]
options.static = true
```

Then, run Polybuild in the root directory to generate the `Makefile` and `.polybuild.mk`. The generated rules compile `.c` files with a C compiler and `.cpp`/`.cc`/`.cxx` files with a C++ compiler. Run `make` to build; Polybuild is only needed when regenerating the Makefiles.

## Commands

```sh
polybuild                          # Generate Makefiles (same as generate)
polybuild init                     # Create a starter configuration; never overwrite one
polybuild check                    # Validate configuration and discover sources; write nothing
polybuild generate                 # Generate Makefile and .polybuild.mk
polybuild --help
polybuild --version
```

Use `-C <directory>` to select the project directory and `--config <file>` to select a different TOML file. Configuration paths and project paths are resolved relative to the directory selected by `-C`, or the current directory if omitted. For example:

```sh
polybuild -C my-project check --config Build.toml
make -C my-project MODE=debug -j8
```

Polybuild never invokes Make or a compiler. Generated recipes require GNU Make and a POSIX shell with standard utilities, including on Windows (for example, an MSYS environment with the compiler available).

Generation validates the entire configuration and discovers sources before writing either Makefile. Unknown keys, incorrect value types, inaccessible source directories, and empty source sets are errors. The current Makefile format requires source, discovered header, artifact, and output paths without whitespace or Make/shell metacharacters. Installation prefixes may contain spaces.

Identical generated files are left untouched, including their timestamps. Changed files are each replaced atomically.

Exit status is zero on success, 2 for invalid CLI arguments, and 1 for configuration or file errors. `--version` currently identifies this as a development build.

## Portable Makefiles

The generated files can be committed to Git and used without Polybuild installed. The header scanner deliberately follows literal includes in every preprocessor branch, including inactive `#ifdef` branches. Generation does not select the host's active branches or invoke a compiler to discover dependencies. Environment overrides remain Make conditionals evaluated at build time. Matching blocks apply in configuration order and change only the settings they specify. Later assignments to the same setting win; empty strings and lists clear earlier values.

Rerun `polybuild generate` after changing the configuration, adding/removing sources, or changing the include graph. Macro-expanded includes are not discovered by the scanner, and referenced headers must be available during generation to be recorded.

## Build Types

Polybuild includes a built-in `MODE` variable (defaulting to `release`). You can trigger different behaviors by passing it to `make`:

```bash
make MODE=debug
```

Objects are written directly into `paths.artifact`, and the linker writes directly to `paths.output`. `MODE` affects compiler and linker flags; it does not change artifact or output paths. Clean before switching modes to rebuild with the selected flags.

Objects and linked binaries depend on `.polybuild.mk`. After changing flags in `Polybuild.toml`, regenerate the Makefiles and run `make` to rebuild. Command-line flag overrides alone do not trigger a rebuild; use `make -B` or clean first when changing them.

Use Make for building, cleaning, and installing:

```sh
make MODE=debug
make clean
make install prefix=/tmp/install
```

`make clean` removes the output and the entire artifact directory. `make install` copies the existing output to the configured prefix; it does not build, run preludes, or create the destination directory.

### Automatic Debug Support

When `MODE=debug` is used, Polybuild automatically:
*   **Windows (MSVC)**: Swaps `/MD` to `/MDd` (or `/MT` to `/MTd`) and appends `/Zi` to compiler flags and `/DEBUG` to linker flags.
*   **Linux/Unix**: Appends `-g` to compiler flags. Existing optimization flags are retained; set `CFLAGS`/`CXXFLAGS` or configuration overrides if you want `-O0`.

## Installation One-Liner

```sh
git clone https://github.com/BlueCannonBall/Polybuild.git && cd Polybuild && make && sudo make install
```

## Tests

Build Polybuild, then run the integration suite with Python 3, GNU Make, and a C compiler available:

```sh
python3 -m unittest discover -s tests -v
```

Set `POLYBUILD` to an absolute executable path to test a different build. Tests use temporary projects and cover generation, diagnostics, conservative header discovery, flag changes, original Makefile output, installation, and cleanup.
