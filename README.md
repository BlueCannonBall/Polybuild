# Polybuild
A simple and declarative Makefile generator designed for humans.

## Usage
First, make a `Polybuild.toml` at your project's root:
```toml
[paths]
output = "polybuild" # Where to put the final result (no default)
source = ["."] # Where to find .c/.cpp files (no default)
include = ["include"] # Equivalent to the -I option of a compiler (default: empty)
library = ["lib"] # Equivalent to the -L option of a compiler (default: your system default C++ library paths)
artifact = "obj" # Where to put .o files (no default)
install = "/usr/local/bin" # Where to put the output binary when `make install` is executed (default: empty)

[options]
c-compiler = "gcc" # The C compiler to use (default: your system default C compiler)
cpp-compiler = "g++" # The C++ compiler to use (default: your system default C++ compiler)
compiler = "g++" # Alias for cpp-compiler
c-compilation-flags = "-Wall -O3" # Options passed to the compiler (default: your system default C compiler flags)
cpp-compilation-flags = "-Wall -std=c++17 -O3" # Options passed to the compiler (default: your system default C++ compiler flags)
compilation-flags = "-Wall -std=c++17 -O3" # Alias for cpp-compilation-flags
link-time-flags = "-lX11" # Options passed to the compiler only at link time, placed after /link on Windows (defaylt: empty)
libraries = ["ssl"] # Equivalent to the -l option of a compiler (default: empty)
pkg-config-libraries = ["gstreamer-1.0"] # A list of libraries added to `compilation-flags` and `libraries` with `pkg-config`
preludes = ["echo this is an arbitrary command that always runs", "echo these commands may execute in parallel"] # (default: empty)
clean-preludes = ["echo this is an arbitrary command that runs with the clean target"] # (default: empty)
shared = false # Equivalent to the -shared and -fPIC options of a compiler (default: false)
static = false # Equivalent to the -static option of a compiler (default: false)

# Environment variable overrides
[env.OS.Windows_NT]
options.compiler = "cl" # Automatically switches to MSVC-style flags (/I, /Fo, etc.)

# Nested environment overrides (Hierarchical)
[env.OS.Windows_NT.env.PROFILE.debug]
options.compilation-flags = "/Zi /Od" # MSVC-specific debug flags

[env.OS.Linux.env.PROFILE.debug]
options.compilation-flags = "-g -O0" # GCC-specific debug flags
```
Then, run Polybuild in the root directory to generate the `Makefile` and `.polybuild.mk`.

## Build Profiles

Polybuild includes a built-in `PROFILE` variable (defaulting to `release`). You can trigger different behaviors by passing it to `make`:

```bash
make PROFILE=debug
```

### Automatic Debug Support
When `PROFILE=debug` is used, Polybuild automatically:
*   **Windows (MSVC)**: Swaps `/MD` to `/MDd` (or `/MT` to `/MTd`) and appends `/Zi` to compiler flags and `/DEBUG` to linker flags.
*   **Linux/Unix**: Appends `-g` to compiler flags.

You can explicitly control this behavior with `options.debug = true/false` in any environment block.

Polybuild automatically builds `.c` files with a C compiler and `.cpp`/`.cc`/`.cxx` files with a C++ compiler.

## Installation One-Liner
```sh
git clone https://github.com/BlueCannonBall/Polybuild.git && cd Polybuild && make && sudo make install
```
