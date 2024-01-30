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
```
Then, run Polybuild in the root directory. This generates a Makefile in the same directory. This file should only be regenerated (by running Polybuild again) when you add new files to your project or when you add new includes. The generated Makefile is safe to push to GitHub repositories, as it is the same regardless of the environment in which it was generated. It does not divulge any sensitive information.
