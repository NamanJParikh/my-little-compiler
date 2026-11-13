"""Compile and behavior-test x86 ELF fixtures in an amd64 Linux container."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from show_x86_state import read_snapshot


FIXTURE_DIR = Path(__file__).resolve().parent / "x86-emitter"
COMPILER = Path(__file__).resolve().parents[1] / "compile"
DOCKER_IMAGE = "ubuntu:24.04"
SOURCES = sorted(FIXTURE_DIR.glob("*.sasm"))


class X86EmitterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not COMPILER.is_file():
            raise RuntimeError("Build SimpleASM/compile before running this harness")
        if shutil.which("docker") is None:
            raise RuntimeError("Docker is required to run x86 ELF tests")
        try:
            subprocess.run(
                ["docker", "info"],
                capture_output=True,
                text=True,
                timeout=15,
                check=True,
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise RuntimeError(f"Docker is unavailable or its daemon is not running: {error}") from error

    def test_each_fixture_matches_expected_behavior_and_state(self):
        self.assertTrue(SOURCES, "No x86-emitter .sasm fixtures found")
        for source in SOURCES:
            with self.subTest(fixture=source.name):
                expected_file = source.with_suffix(".expected.json")
                self.assertTrue(expected_file.is_file(), f"Missing expected results: {expected_file.name}")
                expected = json.loads(expected_file.read_text())

                with tempfile.TemporaryDirectory(prefix="simpleasm-x86-emitter-") as temp_dir:
                    work_dir = Path(temp_dir)
                    executable = work_dir / "program.out"
                    snapshot = work_dir / "state.bin"
                    compile_result = subprocess.run(
                        [
                            str(COMPILER),
                            "--emit", "x86",
                            "--x86-state-fd", "3",
                            "-o", str(executable),
                            str(source),
                        ],
                        capture_output=True,
                        text=True,
                        timeout=15,
                        check=False,
                    )
                    self.assertEqual(
                        compile_result.returncode,
                        0,
                        f"Compiler failed for {source.name}:\n{compile_result.stderr}",
                    )
                    self.assertTrue(executable.is_file(), f"Compiler did not create ELF for {source.name}")

                    run_result = subprocess.run(
                        [
                            "docker", "run", "--rm", "--platform", "linux/amd64",
                            "--volume", f"{work_dir}:/work",
                            "--workdir", "/work",
                            DOCKER_IMAGE,
                            "sh", "-c", "exec 3>state.bin; exec ./program.out",
                        ],
                        capture_output=True,
                        text=True,
                        timeout=60,
                        check=False,
                    )
                    self.assertEqual(
                        run_result.returncode,
                        expected["exit_status"],
                        f"Exit status differs for {source.name}.\n"
                        f"Docker stderr:\n{run_result.stderr}",
                    )
                    self.assertEqual(
                        run_result.stdout,
                        expected["stdout"],
                        f"stdout differs for {source.name}.\nDocker stderr:\n{run_result.stderr}",
                    )
                    self.assertEqual(
                        run_result.stderr,
                        expected["stderr"],
                        f"stderr differs for {source.name}",
                    )
                    self.assertTrue(snapshot.is_file(), f"No state snapshot produced for {source.name}")
                    registers, condition, storage = read_snapshot(snapshot)
                    actual_registers = {
                        f"R{index}": value for index, value in enumerate(registers)
                    }
                    self.assertEqual(actual_registers, expected["registers"], "Final registers differ")
                    self.assertEqual(condition, expected["rc"], "Final Rc differs")
                    self.assertEqual(storage.hex(), expected["storage_hex"], "Final storage differs")


if __name__ == "__main__":
    unittest.main()
