# SimpleASM test scripts

Run the Python-emitter regression suite from the repository root:

```sh
python3 SimpleASM/test-scripts/test_python_emitter.py
```

To compare one expected Python script with one compiler-generated script:

```sh
python3 SimpleASM/test-scripts/compare_python.py expected.py generated.py
```

The comparison command runs both files with the current Python interpreter,
compares stdout, stderr, and exit status, and displays unified diffs for output
that differs. It exits with status `0` for a match, `1` for differences, or `2`
if either script cannot be run. Use `--timeout SECONDS` to change the per-script
execution limit.

The harness compiles every `python-emitter/*.sasm` fixture to a temporary
directory, runs the emitted Python and its paired `*.expected.py` reference,
and compares standard output and process exit status. It covers the arithmetic,
logical, comparison, branch, memory, and text-output instructions supported by
the Python emitter. The paired references describe expected final registers,
memory, and output independently of emitted code. Negative fixtures verify
runtime errors for out-of-bounds memory and text accesses, non-ASCII text
output, and signed and unsigned division by zero. Fixtures exercise data-only
programs with no `.CODE` section, code-only programs with no `.DATA` section,
and an entirely empty program.
