# Polybuild
A simple Makefile generator designed for humans.

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
compiler = "g++" # The compiler to use (default: your system default C++ compiler)
compilation-flags = "-Wall -std=c++17 -O3" # Options passed to the compiler (default: your system default C++ compiler flags)
libraries = ["ssl"] # Equivalent to the -l option of a compiler (default: empty)
shared = false # Equivalent to the -shared and -fPIC options of a compiler (default: false)
static = false # Equivalent to the -static option of a compiler (default: false)

# Environment variables can be used to change Makefile behavior at runtime
[env.OS.Windows_NT]
paths.library = ["winlib"]
options.compiler = "clang++"
options.compilation-flags = "-Wall -std=c++17 -O2"
options.libraries = ["ssl", "ws2_32"]
options.static = true
```
Then, run Polybuild in the root directory. This generates a Makefile in the same directory. This file should only be regenerated (by running Polybuild again) when you add new files to your project or when you add new includes. The generated Makefile is safe to push to GitHub repositories, as it is the same regardless of the environment in which it was generated. It does not divulge any sensitive information.

## Installation One-Liner
```sh
git clone https://github.com/BlueCannonBall/Polybuild.git && cd Polybuild && make && sudo make install && cd ..
```
