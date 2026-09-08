"""Integration tests. Run with POLYBUILD=/absolute/path/to/polybuild python3 -m unittest discover -s tests -v."""

import json
import os
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
