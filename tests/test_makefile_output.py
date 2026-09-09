"""Preserve original rules and commands while allowing composable settings."""

import os
import shlex
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_cli import BINARY


SOURCES = {
    "c/main.c": '#include "local.h"\nint main(void) { return 0; }\n',
    "c/local.h": '#include <common.h>\n',
    "cpp/main.cpp": '#include <common.h>\n#ifdef _WIN32\n#include "windows.h"\n#endif\n',
    "cpp/extra.cc": '#include <common.h>\n',
    "cpp/other.cxx": '#include <common.h>\n',
    "cpp/windows.h": '// platform header\n',
    "include/common.h": '#include "nested.h"\n',
    "include/nested.h": '#include "common.h"\n',
    "single.c": 'int single(void) { return 1; }\n',
}

CONFIGS = {
    "minimal": '[paths]\noutput = "app"\nsource = ["c"]\nartifact = "obj"\n[options]\n',
    "shared": '[paths]\noutput = "lib/example"\nsource = ["cpp"]\ninclude = ["include"]\nartifact = "shared-objects"\n[options]\nshared = true\n',
    "static": '[paths]\noutput = "bin/example"\nsource = ["single.c"]\nartifact = "static-objects"\n[options]\nstatic = true\n',
    "full": r'''[paths]
output = "bin/example"
source = ["c", "cpp", "single.c"]
include = ["include"]
library = ["vendor lib", "lib"]
artifact = "objects"
install = "/tmp/install dir"
[options]
c-compiler = "clang"
cpp-compiler = "g++"
compiler = "unused-alias"
c-compilation-flags = "-Wall -O2"
cpp-compilation-flags = "-Wall -std=c++17 -O2"
compilation-flags = "unused-alias"
link-time-flags = "-Wl,--as-needed"
libraries = ["ssl", "crypto"]
static-libraries = ["lib/first.a", "lib/second.a"]
pkg-config-libraries = ["gstreamer-1.0", "glew"]
preludes = ["echo first", "echo second"]
clean-preludes = ["echo cleaning"]
shared = true
static = true
[env.OS.Windows_NT]
paths.library = ["winlib"]
paths.install = 'C:\Program Files\Example'
options.c-compiler = "cl"
options.compiler = "clang-cl"
options.c-compilation-flags = "/W3"
options.compilation-flags = "/W3 /std:c++17 /O2"
options.link-time-flags = "/SUBSYSTEM:WINDOWS"
options.libraries = ["libssl.lib", "ws2_32.lib"]
options.static-libraries = ["winlib/first.lib"]
options.pkg-config-libraries = ["glew"]
options.static = false
[env.MODE.debug]
options.cpp-compiler = "clang++"
options.cpp-compilation-flags = "-O0"
options.libraries = []
options.static-libraries = []
options.pkg-config-libraries = []
[env.OS.Linux]
paths.install = ""
options.static = false
''',
}


def write_project(root, config):
    for name, contents in SOURCES.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents.encode())
    (root / "Polybuild.toml").write_bytes(config.encode())


def rule_tokens(contents):
    return [shlex.split(line.decode()) if line.startswith(b"\t") else line for line in contents.splitlines()]


class MakefileOutputTests(unittest.TestCase):
    def test_original_rules_and_commands(self):
        fixtures = Path(__file__).parent / "fixtures" / "makefiles"
        for name, config in CONFIGS.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix="polybuild-output-") as directory:
                root = Path(directory)
                write_project(root, config)
                result = subprocess.run([str(BINARY)], cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for filename in ("Makefile", ".polybuild.mk"):
                    expected = (fixtures / name / filename).read_bytes()
                    if os.name == "nt":
                        expected = expected.replace(b"\n", b"\r\n")
                    actual = (root / filename).read_bytes()
                    if filename == "Makefile":
                        self.assertEqual(expected, actual, filename)
                    else:
                        self.assertEqual(expected.split(b"c_compiler :=", 1)[0], actual.split(b"c_compiler :=", 1)[0])
                        self.assertEqual(rule_tokens(expected.split(b"\nall:", 1)[1]), rule_tokens(actual.split(b"\nall:", 1)[1]))
                        (root / "Original.mk").write_bytes(expected)

                for library in ("lib/first.a", "lib/second.a", "winlib/first.lib"):
                    path = root / library
                    path.parent.mkdir(exist_ok=True)
                    path.touch()
                environment = dict(os.environ)
                for key in ("MAKEFLAGS", "MFLAGS", "CFLAGS", "CXXFLAGS", "LDFLAGS"):
                    environment.pop(key, None)
                # Each case activates at most one override, where commands should be unchanged.
                for system, mode in (("Unix", "release"), ("Unix", "debug"), ("Windows_NT", "release")):
                    commands = []
                    for makefile in ("Original.mk", ".polybuild.mk"):
                        result = subprocess.run(["make", "-n", "-f", makefile, "all", f"OS={system}", f"MODE={mode}", "CC=cc", "CXX=c++"], cwd=root, env=environment, capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        commands.append([shlex.split(line) for line in result.stdout.splitlines()])
                    self.assertEqual(commands[0], commands[1], (system, mode))


if __name__ == "__main__":
    unittest.main()
