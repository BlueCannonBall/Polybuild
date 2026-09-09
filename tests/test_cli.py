"""Integration tests. Run with POLYBUILD=/absolute/path/to/polybuild python3 -m unittest discover -s tests -v."""

import json
import os
import shlex
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest


BINARY = Path(os.environ.get("POLYBUILD", Path(__file__).resolve().parents[1] / "polybuild")).resolve()


class PolybuildTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="polybuild-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.environment = dict(os.environ, CCACHE_DISABLE="1")
        for key in ("MAKEFLAGS", "MFLAGS", "MODE", "OS", "CFLAGS", "CXXFLAGS", "LDFLAGS", "CC", "CXX"):
            self.environment.pop(key, None)
        self.config = '[paths]\noutput = "app"\nsource = ["."]\n'
        self.write("Polybuild.toml", self.config)
        self.write("main.c", '#include <stdio.h>\n#ifndef VALUE\n#define VALUE 1\n#endif\nint main(void) { printf("%d\\n", VALUE); }\n')

    def write(self, name, contents):
        (self.root / name).write_text(contents)

    def run_command(self, *args, success=True, environment=None):
        result = subprocess.run(args, cwd=self.root, env=environment or self.environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def cli(self, *args, **kwargs):
        return self.run_command(str(BINARY), *args, **kwargs)

    def test_help_version_and_argument_errors(self):
        (self.root / "Polybuild.toml").unlink()
        self.assertIn("Usage:", self.cli("--help").stdout)
        self.assertIn("Polybuild", self.cli("--version").stdout)
        for args in [("unknown",), ("--config",), ("build",), ("clean",), ("install",), ("--mode", "debug"), ("-j2",), ("--all-modes",), ("--", "CC=clang")]:
            self.assertEqual(self.cli(*args, success=False).returncode, 2)

    def test_generator_needs_no_build_tools(self):
        environment = dict(self.environment, PATH=str(self.root / "no-tools"))
        self.write("Polybuild.toml", self.config + '[options]\npreludes = ["touch prelude-ran"]\n')
        self.cli("check", environment=environment)
        self.cli("generate", environment=environment)
        self.cli("init", "--config", "Other.toml", environment=environment)
        self.assertTrue((self.root / "Makefile").exists())
        self.assertFalse((self.root / "app").exists())
        self.assertFalse((self.root / "prelude-ran").exists())

    def test_check_and_generation_are_deterministic(self):
        self.cli("check")
        self.assertFalse((self.root / "Makefile").exists())
        self.cli()
        paths = [self.root / "Makefile", self.root / ".polybuild.mk"]
        before = [(path.read_bytes(), path.stat().st_mtime_ns) for path in paths]
        environment = dict(self.environment, OS="Windows_NT", MODE="debug")
        self.cli("generate", environment=environment)
        self.assertEqual(before, [(path.read_bytes(), path.stat().st_mtime_ns) for path in paths])

    def test_validation_preserves_existing_files(self):
        self.cli()
        before = (self.root / ".polybuild.mk").read_bytes()
        for extra in ['[options]\nshared = "yes"\n', '[options]\ncompilation-flag = "-g"\n']:
            self.write("Polybuild.toml", self.config + extra)
            self.cli("generate", success=False)
            self.assertEqual(before, (self.root / ".polybuild.mk").read_bytes())
        self.write("Polybuild.toml", self.config.replace('["."]', '["missing"]'))
        self.cli("check", success=False)
        self.assertEqual(before, (self.root / ".polybuild.mk").read_bytes())

    def test_temporary_path_collisions_preserve_existing_files(self):
        for name in (".polybuild.mk", "Makefile", "Other.toml"):
            for directory in (False, True):
                with self.subTest(name=name, directory=directory):
                    target = self.root / name
                    temporary = self.root / (name + ".tmp")
                    if name == "Other.toml":
                        command = ("init", "--config", name)
                    else:
                        target.write_text("original output\n")
                        command = ("generate",)
                    if directory:
                        temporary.mkdir()
                        contents = temporary / name
                    else:
                        contents = temporary
                    contents.write_text("existing temporary contents\n")
                    try:
                        self.cli(*command, success=False)
                        self.assertEqual(contents.read_text(), "existing temporary contents\n")
                        if name == "Other.toml":
                            self.assertFalse(target.exists())
                        else:
                            self.assertEqual(target.read_text(), "original output\n")
                    finally:
                        contents.unlink()
                        if directory:
                            temporary.rmdir()
                    self.cli(*command)
                    self.assertFalse(temporary.exists())
                    if name == "Other.toml":
                        target.unlink()

    def test_temporary_symlinks_are_preserved(self):
        directory = self.root / "existing-directory"
        directory.mkdir()
        marker = directory / ".polybuild.mk"
        marker.write_text("keep this file\n")
        missing = self.root / "missing"
        temporary = self.root / ".polybuild.mk.tmp"
        for target in (directory, missing):
            try:
                temporary.symlink_to(target, target_is_directory=target == directory)
            except OSError:
                self.skipTest("Creating symlinks is unavailable on this system")
            try:
                self.cli("generate", success=False)
                self.assertTrue(temporary.is_symlink())
                self.assertEqual(marker.read_text(), "keep this file\n")
                self.assertFalse(missing.exists())
            finally:
                temporary.unlink()

    def test_init_and_directory_config_options(self):
        self.cli("-C", str(self.root), "init", "--config", "Other.toml")
        before = (self.root / "Other.toml").read_bytes()
        self.cli("init", "--config", "Other.toml", success=False)
        self.assertEqual(before, (self.root / "Other.toml").read_bytes())
        self.cli("check", "-C", str(self.root), "--config", "Other.toml")

    def test_paths_only_environment_override(self):
        destination = self.root / "installed-app"
        self.write("Polybuild.toml", self.config + f'[env.DEST.test.paths]\ninstall = "{destination}"\n')
        self.cli("check")
        self.cli("generate")
        self.run_command("make")
        self.run_command("make", "install", "DEST=test")
        self.assertEqual((self.root / "app").read_bytes(), destination.read_bytes())

    def test_environment_overrides_compose_in_builds(self):
        self.write("Polybuild.toml", self.config + '''[options]
c-compiler = "missing-compiler"
link-time-flags = "invalid-link-flags"
[env.FEATURE.on.options]
c-compiler = "cc"
c-compilation-flags = "-DVALUE=2"
[env.MODE.debug.options]
link-time-flags = ""
[env.EXTRA.on.options]
c-compilation-flags = "-DVALUE=3"
''')
        self.cli("generate")
        for extra, value in (("off", "2"), ("on", "3")):
            self.run_command("make", "-B", "FEATURE=on", "MODE=debug", f"EXTRA={extra}")
            self.assertEqual(self.run_command("./app").stdout.strip(), value)

    def test_environment_overrides_compose_and_clear_flags(self):
        self.write("extra.cpp", "int extra() { return 0; }\n")
        self.write("custom.a", "")
        self.write("Polybuild.toml", self.config + '''include = ["include"]
[options]
shared = true
[env.PLATFORM.windows]
paths.library = ["custom libs"]
paths.install = "custom prefix"
options.c-compiler = "cl"
options.compiler = "unused-alias"
options.cpp-compiler = "clang-cl"
options.c-compilation-flags = "/DCUSTOM_C"
options.libraries = ["custom.lib"]
options.static-libraries = ["custom.a"]
options.pkg-config-libraries = ["custom-pkg"]
options.static = true
[env.MODE.debug.options]
compilation-flags = "unused-alias"
cpp-compilation-flags = "/DCUSTOM_CPP"
link-time-flags = "/CUSTOM_LINK"
[env.CLEAR.yes]
paths.library = []
paths.install = ""
options.c-compilation-flags = ""
options.cpp-compilation-flags = ""
options.link-time-flags = ""
options.libraries = []
options.static-libraries = []
options.pkg-config-libraries = []
options.static = false
''')
        self.cli("generate")
        for clear in ("no", "yes"):
            with self.subTest(clear=clear):
                args = ("OS=Windows_NT", "PLATFORM=windows", "MODE=debug", f"CLEAR={clear}")
                result = self.run_command("make", "-n", "-f", ".polybuild.mk", "all", *args)
                commands = [shlex.split(line) for line in result.stdout.splitlines()]
                commands = [command for command in commands if command[0] in ("cl", "clang-cl")]
                self.assertEqual(len(commands), 3)
                c_command = next(command for command in commands if command[0] == "cl")
                cpp_command = next(command for command in commands if command[0] == "clang-cl" and "/c" in command)
                link_command = next(command for command in commands if "/c" not in command)
                for command in commands:
                    for flag in ("/Zi", "/Iinclude", "/LD", "/MDd" if clear == "yes" else "/MTd"):
                        self.assertIn(flag, command)
                    self.assertEqual("--cflags" in command, clear == "no")
                    self.assertEqual("custom-pkg`" in command, clear == "no")
                    self.assertNotIn("unused-alias", command)
                self.assertEqual("/DCUSTOM_C" in c_command, clear == "no")
                self.assertEqual("/DCUSTOM_CPP" in cpp_command, clear == "no")
                self.assertEqual("/DCUSTOM_CPP" in link_command, clear == "no")
                for flag in ("/CUSTOM_LINK", "/LIBPATH:custom libs", "custom.lib", "custom.a", "--libs"):
                    self.assertEqual(flag in link_command, clear == "no", flag)
                self.assertIn("/DEBUG", link_command)
                result = self.run_command("make", "-n", "-f", ".polybuild.mk", "install", *args)
                self.assertIn('cp "app.dll" ' + ('""' if clear == "yes" else '"custom prefix"'), result.stdout)

    def test_environment_variable_names(self):
        for name in ("_", "a", "Z_90", "_VAR", "", "0ABC", "space name", "a-b", "é"):
            with self.subTest(name=name):
                self.write("Polybuild.toml", self.config + f'[env.{json.dumps(name)}.enabled]\noptions.static = true\n')
                valid = name in ("_", "a", "Z_90", "_VAR")
                result = self.cli("check", success=valid)
                if not valid:
                    self.assertIn("Invalid environment variable:", result.stdout)

    def test_crlf_and_lf_generate_identical_dependencies(self):
        self.write("Polybuild.toml", self.config + 'include = ["."]\n')
        self.write("quoted.h", '#include <angled.h>\n')
        self.write("angled.h", "\n")
        self.write("main.c", '\n#include "quoted.h"\nint main(void) { return 0; }\n')
        self.cli("generate")
        before = (self.root / ".polybuild.mk").read_bytes()
        self.assertIn(b" quoted.h", before)
        self.assertIn(b" angled.h", before)
        for name in ("main.c", "quoted.h", "angled.h"):
            path = self.root / name
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.cli("generate")
        self.assertEqual(before, (self.root / ".polybuild.mk").read_bytes())

    def test_build_is_unchanged_without_regeneration(self):
        self.cli("generate")
        self.run_command("make", "-j2")
        obj = self.root / "obj/main_0.o"
        before = obj.stat().st_mtime_ns
        self.run_command("make")
        self.assertEqual(before, obj.stat().st_mtime_ns)

    def test_spaces_in_paths(self):
        original_root = self.root
        for extension, compiler in (("c", "cc"), ("cpp", "c++")):
            with self.subTest(extension=extension):
                self.root = original_root / ("project " + extension)
                self.root.mkdir()
                for directory in ("source files", "include files", "library files", "compiler tools", " installed files"):
                    (self.root / directory).mkdir()
                compiler_path = self.root / "compiler tools" / "my compiler"
                compiler_path.write_text("#!/bin/sh\nexec " + shlex.quote(shutil.which(compiler)) + ' "$@"\n')
                compiler_path.chmod(0o755)
                self.write("include files/nested header.h", "#define VALUE 1\n")
                self.write("include files/main header.h", '#include "nested header.h"\n')
                self.write("source files/main file." + extension, '#include <stdio.h>\n#include <main header.h>\n'
                           '#ifdef __cplusplus\nextern "C" {\n#endif\n'
                           'int archived(void); int named(void);\n#ifdef __cplusplus\n}\n#endif\n'
                           'int extra(void);\nint main(void) { printf("%d\\n", VALUE + extra() + archived() + named()); }\n')
                self.write(" extra  file." + extension, "int extra(void) { return 1; }\n")
                for filename, function, value in (("static library", "archived", 10), ("override library", "archived", 20), ("libnamed library", "named", 100)):
                    self.write("library files/library.c", f"int {function}(void) {{ return {value}; }}\n")
                    self.run_command("cc", "-c", "library files/library.c", "-o", "library files/library.o")
                    self.run_command("ar", "rcs", f"library files/{filename}.a", "library files/library.o")
                self.write("Build config.toml", '[paths]\noutput = "output files/my app"\n'
                           'artifact = "object files"\nsource = ["source files", " extra  file.' + extension + '"]\n'
                           'include = ["include files"]\nlibrary = ["library files"]\ninstall = " installed files"\n'
                           '[options]\nc-compiler = ' + json.dumps(str(compiler_path)) + '\n'
                           'cpp-compiler = ' + json.dumps(str(compiler_path)) + '\n'
                           'libraries = ["named library"]\nstatic-libraries = ["library files/static library.a"]\n'
                           '[env.USE_OVERRIDE.yes]\noptions.static-libraries = ["library files/override library.a"]\n')
                self.cli("--config", "Build config.toml")
                commands = self.run_command("make", "-n", "OS=Windows_NT").stdout
                arguments = [shlex.split(line) for line in commands.splitlines()]
                compilations = [args for args in arguments if "/c" in args]
                self.assertEqual(len(compilations), 2)
                self.assertIn("source files/main file." + extension, compilations[0])
                self.assertIn("/Fo:object files/main file_0.obj", compilations[0])
                link = next(args for args in arguments if "/Fe:output files/my app.exe" in args)
                self.assertIn("object files/ extra  file_0.obj", link)
                self.assertIn("library files/static library.a", link)
                self.run_command("make", "-j2")
                self.assertEqual(self.run_command("./output files/my app").stdout.strip(), "112")
                objects = list((self.root / "object files").iterdir())
                before = [path.stat().st_mtime_ns for path in objects]
                self.run_command("make", "-j2")
                self.assertEqual(before, [path.stat().st_mtime_ns for path in objects])
                self.write("include files/nested header.h", "#define VALUE 2\n")
                self.run_command("make", "-j2")
                self.assertEqual(self.run_command("./output files/my app").stdout.strip(), "113")
                (self.root / "output files/my app").unlink()
                self.run_command("make", "USE_OVERRIDE=yes")
                self.assertEqual(self.run_command("./output files/my app").stdout.strip(), "123")
                self.write("library files/library.c", "int archived(void) { return 30; }\n")
                self.run_command("cc", "-c", "library files/library.c", "-o", "library files/library.o")
                self.run_command("ar", "rcs", "library files/override library.a", "library files/library.o")
                self.run_command("make", "USE_OVERRIDE=yes")
                self.assertEqual(self.run_command("./output files/my app").stdout.strip(), "133")
                self.run_command("make", "install")
                self.assertEqual((self.root / "output files/my app").read_bytes(), (self.root / " installed files/my app").read_bytes())
                self.run_command("make", "install", "prefix=./ installed files/another app")
                self.assertTrue((self.root / " installed files/another app").is_file())
                self.run_command("make", "clean")
                self.assertFalse((self.root / "object files").exists())
                self.assertFalse((self.root / "output files/my app").exists())
        self.root = original_root

    def test_shared_library_with_spaces(self):
        self.write("main.c", "int value(void) { return 1; }\n")
        output = self.root / "shared libraries" / "my library"
        artifact = self.root / "shared objects"
        self.write("Polybuild.toml", '[paths]\noutput = ' + json.dumps(str(output)) + '\n'
                   'artifact = ' + json.dumps(str(artifact)) + '\nsource = ["main.c"]\n'
                   '[options]\nshared = true\n')
        self.cli()
        self.run_command("make", "-j2")
        self.assertTrue(output.with_suffix(".so").is_file())
        self.assertTrue((artifact / "main_0.o").is_file())
        self.run_command("make", "clean")
        self.assertFalse(output.with_suffix(".so").exists())
        self.assertFalse(artifact.exists())

    def test_unreadable_sources_preserve_makefiles(self):
        self.write("main.c", '#include "first.h"\nint main(void) { return 0; }\n')
        self.write("first.h", '#include "second.h"\n')
        self.write("second.h", "\n")
        self.cli("generate")
        paths = [self.root / "Makefile", self.root / ".polybuild.mk"]
        before = [path.read_bytes() for path in paths]
        for name in ("main.c", "first.h", "second.h"):
            with self.subTest(name=name):
                source = self.root / name
                mode = source.stat().st_mode
                source.chmod(0)
                try:
                    if os.access(source, os.R_OK):
                        self.skipTest("File permissions do not prevent reading on this system")
                    for command in ("check", "generate"):
                        result = self.cli(command, success=False)
                        self.assertIn("Failed to open source file:", result.stdout)
                        self.assertIn(name, result.stdout)
                    self.assertEqual(before, [path.read_bytes() for path in paths])
                finally:
                    source.chmod(mode)

    def test_overlapping_source_paths(self):
        self.cli("generate")
        before = (self.root / ".polybuild.mk").read_bytes()
        sources = [".", "./.", "main.c", "./main.c", str(self.root / "main.c")]
        self.write("Polybuild.toml", self.config.replace('["."]', json.dumps(sources)))
        self.cli("generate")
        self.assertEqual(before, (self.root / ".polybuild.mk").read_bytes())
        self.run_command("make")
        self.assertEqual(self.run_command("./app").stdout.strip(), "1")

    def test_configured_flag_changes_rebuild(self):
        for value in (2, 3):
            self.write("Polybuild.toml", self.config + f'[options]\nc-compilation-flags = "-DVALUE={value}"\n')
            self.cli("generate")
            self.run_command("make")
            self.assertEqual(self.run_command("./app").stdout.strip(), str(value))
        self.assertNotIn(".flags", (self.root / ".polybuild.mk").read_text())
        self.assertFalse((self.root / "obj/.flags").exists())

    def test_literal_artifact_path_and_clean(self):
        self.write("Polybuild.toml", self.config + 'artifact = "build/objects"\n')
        self.cli("generate")
        self.run_command("make", "MODE=debug")
        self.assertTrue((self.root / "build/objects/main_0.o").exists())
        self.run_command("make", "clean")
        self.assertFalse((self.root / "build/objects").exists())
        self.assertFalse((self.root / "app").exists())

    def test_source_discovery_and_conservative_headers(self):
        self.write("windows.h", "/* platform-specific header */\n")
        self.write("main.c", '#ifdef _WIN32\n#include "windows.h"\n#endif\nint main(void) { return 0; }\n')
        self.cli("generate")
        self.run_command("make")
        self.assertIn("windows.h", (self.root / ".polybuild.mk").read_text())
        self.write("extra.c", "int extra(void) { return 1; }\n")
        self.cli("generate")
        self.run_command("make")
        self.assertTrue((self.root / "obj/extra_0.o").exists())

    def test_install_only_copies_existing_output(self):
        destination = self.root / "installed-app"
        self.write("Polybuild.toml", self.config + f'install = "{destination}"\n[options]\npreludes = ["touch prelude-ran"]\n')
        self.cli("generate")
        self.run_command("make", "install", success=False)
        self.assertFalse((self.root / "app").exists())
        self.assertFalse((self.root / "prelude-ran").exists())
        self.run_command("make")
        before = (self.root / "app").read_bytes()
        (self.root / "prelude-ran").unlink()
        self.write("main.c", "invalid C source\n")
        self.run_command("make", "install")
        self.assertEqual(before, destination.read_bytes())
        self.assertFalse((self.root / "prelude-ran").exists())


if __name__ == "__main__":
    unittest.main()
