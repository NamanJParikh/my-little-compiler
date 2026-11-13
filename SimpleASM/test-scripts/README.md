# SimpleASM test scripts

### Parser

Run the parser regression suite from the repository root:

```sh
python3 SimpleASM/test-scripts/test_parser.py
```

The parser harness runs the compiler with `--emit print` and compares its
parsed-program output to the paired `parser/*.expected.txt` snapshots. Its
fixtures cover every opcode, operand type, immediate type, addressing form,
label handling, data-only and code-only programs, and empty input. It also
checks that representative malformed inputs are rejected with parser errors.

### Python Emitter

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

### x86 Emitter

Run the x86-emitter regression suite from the repository root with Docker
available and `SimpleASM/compile` built:

```sh
python3 SimpleASM/test-scripts/test_x86_emitter.py
```

The harness compiles every `x86-emitter/*.sasm` fixture with state snapshots
enabled, runs each ELF in a `linux/amd64` Ubuntu container, and compares its
exit status, stdout, stderr, final R0-R7 and Rc values, and complete program
storage against the paired `*.expected.json` file.

For x86-64 ELF tests on macOS, run the generated executable in an `amd64` Linux
container. `--x86-state-fd 3` enables a final-state snapshot on inherited file
descriptor 3, separate from program stdout:

```sh
SimpleASM/compile --emit x86 --x86-state-fd 3 -o /tmp/program.out path/to/program.sasm
docker run --rm --platform linux/amd64 -v /tmp:/work -w /work ubuntu:24.04 \
  sh -c 'exec 3>state.bin; exec ./program.out'
```

The snapshot file is `/tmp/state.bin` on the host. Its binary layout is
documented in `SimpleASM/SPEC.md`; it contains the x86 backend's supported
registers R0-R7, Rc, and the final program storage bytes.
Display it from the repository root with:

```sh
python3 SimpleASM/test-scripts/show_x86_state.py /tmp/state.bin
```

The script prints registers in decimal and hexadecimal and storage in
offsetted hexadecimal/ASCII rows. If no path is provided, it reads `state.bin`
from the current directory.
