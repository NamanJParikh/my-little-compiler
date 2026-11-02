"""Compile and behavior-test every paired Python-emitter fixture."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


FIXTURE_DIR = Path(__file__).resolve().parent / "python-emitter"
COMPILER = Path(__file__).resolve().parents[1] / "compile"
SOURCES = sorted(FIXTURE_DIR.glob("*.sasm"))


class PythonEmitterTests(unittest.TestCase):
    def test_compiler_available(self):
        self.assertTrue(COMPILER.is_file(), "Build SimpleASM/compile before running this harness")

    def test_each_fixture_matches_reference(self):
        self.assertTrue(SOURCES, "No .sasm fixtures found")
        for source in SOURCES:
            with self.subTest(fixture=source.name):
                reference = source.with_suffix(".expected.py")
                self.assertTrue(reference.is_file(), f"Missing reference script: {reference.name}")

                with tempfile.TemporaryDirectory(prefix="simpleasm-python-emitter-") as temp_dir:
                    emitted = Path(temp_dir) / "emitted.py"
                    compile_result = subprocess.run(
                        [str(COMPILER), "--emit", "python", "-o", str(emitted), str(source)],
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
                    self.assertTrue(emitted.is_file(), f"Compiler did not create Python for {source.name}")

                    env = os.environ.copy()
                    env["PYTHONDONTWRITEBYTECODE"] = "1"
                    expected_result = subprocess.run(
                        [sys.executable, str(reference)],
                        capture_output=True,
                        text=True,
                        timeout=5,
                        check=False,
                        env=env,
                    )
                    actual_result = subprocess.run(
                        [sys.executable, str(emitted)],
                        capture_output=True,
                        text=True,
                        timeout=5,
                        check=False,
                        env=env,
                    )
                    self.assertEqual(
                        expected_result.returncode,
                        actual_result.returncode,
                        f"Exit status differs for {source.name}.\n"
                        f"Reference stderr:\n{expected_result.stderr}\n"
                        f"Emitter stderr:\n{actual_result.stderr}",
                    )
                    self.assertEqual(
                        expected_result.stdout,
                        actual_result.stdout,
                        f"Output differs for {source.name}.\n"
                        f"Reference stderr:\n{expected_result.stderr}\n"
                        f"Emitter stderr:\n{actual_result.stderr}",
                    )
                    if expected_result.stderr.strip():
                        expected_error = expected_result.stderr.strip().splitlines()
                        actual_error = actual_result.stderr.strip().splitlines()
                        self.assertTrue(expected_error, f"Reference did not report an error for {source.name}")
                        self.assertTrue(actual_error, f"Emitter did not report an error for {source.name}")
                        self.assertEqual(
                            expected_error[-1],
                            actual_error[-1],
                            f"Error differs for {source.name}.\n"
                            f"Reference stderr:\n{expected_result.stderr}\n"
                            f"Emitter stderr:\n{actual_result.stderr}",
                        )
                    else:
                        self.assertEqual(
                            expected_result.stderr,
                            actual_result.stderr,
                            f"Standard error differs for {source.name}",
                        )


if __name__ == "__main__":
    unittest.main()
