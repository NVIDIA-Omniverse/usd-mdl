# SPDX-License-Identifier: Apache-2.0
"""Exercise test-wrapper command quoting through subprocess and CMake dispatch."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

_ARGS = None


def _command(arguments: list[str]) -> str:
    if os.name != "nt":
        return shlex.join(arguments)
    # This is the wrapper's CMake command-string syntax. Single quotes let a
    # literal double quote reach argv without making it a quote delimiter.
    return " ".join("'" + value + "'" if '"' in value else '"' + value + '"'
                    for value in arguments)


class TestWrapperQuoting(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("mdl_test_wrapper", _ARGS.wrapper)
        cls.wrapper_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.wrapper_module)

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="usdMdl wrapper paths ")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.fixture = self.directory / "test environment"
        self.fixture.mkdir()
        self.script = self.fixture / "argument probe.py"
        self.script.write_text('''import os
from pathlib import Path
import sys
assert sys.argv[1:] == ["input file.txt", "", r"C:\\special path\\file", 'literal "quotes" text'], sys.argv
assert Path("input file.txt").read_text() == "copied fixture"
assert Path(os.environ["PXR_TEST_DIR"]) == Path.cwd()
assert os.environ["WRAPPER_MARKER"] == "spaced environment value"
print("WRAPPER_PROBE_OK")
sys.exit(int(os.environ.get("WRAPPER_EXIT", "0")))
''')
        (self.fixture / "input file.txt").write_text("copied fixture")
        self.environment = os.environ.copy()
        scratch = self.directory / "wrapper scratch"
        scratch.mkdir()
        for name in ("TMP", "TEMP", "TMPDIR"):
            self.environment[name] = str(scratch)
        # A real executable path with spaces catches quoting in both the
        # command string and CMake's wrapper-interpreter invocation.
        executable_directory = self.directory / "python runtime"
        executable_directory.mkdir()
        self.executable = executable_directory / Path(sys.executable).name
        if os.name == "nt":
            shutil.copy2(sys.executable, self.executable)
            for library in Path(sys.executable).parent.glob("python*.dll"):
                shutil.copy2(library, executable_directory / library.name)
        else:
            self.executable.symlink_to(Path(sys.executable).resolve())
        self.environment["PYTHONHOME"] = sys.base_prefix
        self.arguments = [str(self.script), "input file.txt", "",
                          r"C:\special path\file", 'literal "quotes" text']

    def _run_wrapper(self, actual_exit: int, expected_exit: int):
        return subprocess.run(
            [str(self.executable), _ARGS.wrapper, "--verbose",
             "--testenv-dir=" + str(self.fixture),
             "--env-var=WRAPPER_MARKER=spaced environment value",
             "--env-var=WRAPPER_EXIT=" + str(actual_exit),
             "--expected-return-code=" + str(expected_exit),
             _command([str(self.executable), *self.arguments])],
            env=self.environment, capture_output=True, text=True, timeout=30)

    def test_windows_quote_delimiters_backslashes_and_empty_arguments(self) -> None:
        command = '"C:\\Python Space\\python.exe" "C:\\test script.py" "" \'literal "quotes" text\''
        self.assertEqual(self.wrapper_module._split_command(command, windows=True),
                         [r"C:\Python Space\python.exe", r"C:\test script.py", "", 'literal "quotes" text'])

    def test_posix_quoted_command_retains_the_same_arguments(self) -> None:
        arguments = ["/python with spaces", "/a script.py", "", r"C:\a b\c", 'literal "quotes" text']
        self.assertEqual(self.wrapper_module._split_command(shlex.join(arguments), windows=False), arguments)

    def test_spaced_executable_script_fixture_and_arguments(self) -> None:
        result = self._run_wrapper(0, 0)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("WRAPPER_PROBE_OK", result.stdout)

    def test_expected_nonzero_child_return_code_is_accepted(self) -> None:
        result = self._run_wrapper(7, 7)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("WRAPPER_PROBE_OK", result.stdout)

    def test_unexpected_child_return_code_fails(self) -> None:
        result = self._run_wrapper(7, 0)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("exit code 7 != expected 0", result.stderr)

    def test_cmake_registration_quotes_the_python_executable(self) -> None:
        source = self.directory / "cmake source"
        source.mkdir()
        build = self.directory / "cmake build"
        command = _command(self.arguments)
        (source / "CMakeLists.txt").write_text("\n".join([
            "cmake_minimum_required(VERSION 3.20)",
            "project(wrapperQuoting LANGUAGES NONE)",
            "enable_testing()",
            "set(BUILD_TESTING ON)",
            f"set(Python3_EXECUTABLE [==[{self.executable.as_posix()}]==])",
            "add_library(_usdMdl SHARED IMPORTED)",
            f"set_target_properties(_usdMdl PROPERTIES IMPORTED_LOCATION [==[{(self.directory / 'unused.pyd').as_posix()}]==])",
            f"include([==[{Path(_ARGS.helper).as_posix()}]==])",
            "mdl_register_test(wrapperSpacing PYTHON",
            f"    COMMAND [==[{command}]==]",
            f"    TESTENV_DIR [==[{self.fixture.as_posix()}]==]",
            "    ENV WRAPPER_MARKER=spaced\\ environment\\ value)",
            "",
        ]))
        configured = subprocess.run([_ARGS.cmake, "-S", str(source), "-B", str(build)],
                                    env=self.environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        ctest = Path(_ARGS.cmake).with_name("ctest.exe" if os.name == "nt" else "ctest")
        tested = subprocess.run([str(ctest), "--test-dir", str(build), "-C", "Release", "--output-on-failure", "--no-tests=error"],
                                env=self.environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
        self.assertIn("100% tests passed", tested.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("wrapper", "helper", "cmake"):
        parser.add_argument("--" + name, required=True)
    _ARGS = parser.parse_args()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
