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
link-time-flags = "-lX11" # Options passed to the compiler only at link time, placed after /link on Windows (default: empty)
libraries = ["ssl"] # Equivalent to the -l option of a compiler (default: empty)
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

Then, run Polybuild in the root directory to generate the `Makefile` and `.polybuild.mk`. Polybuild automatically builds `.c` files with a C compiler and `.cpp`/`.cc`/`.cxx` files with a C++ compiler.

## Build Types

Polybuild includes a built-in `MODE` variable (defaulting to `release`). You can trigger different behaviors by passing it to `make`:

```bash
make MODE=debug
```

### Automatic Debug Support

When `MODE=debug` is used, Polybuild automatically:
*   **Windows (MSVC)**: Swaps `/MD` to `/MDd` (or `/MT` to `/MTd`) and appends `/Zi` to compiler flags and `/DEBUG` to linker flags.
*   **Linux/Unix**: Appends `-g` to compiler flags.

## Installation One-Liner

```sh
git clone https://github.com/BlueCannonBall/Polybuild.git && cd Polybuild && make && sudo make install
```
