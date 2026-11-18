from reference_runtime import run_reference

registers = [0] * 17

memory = bytearray.fromhex(
    "c3a900"
)

raise SystemExit(run_reference(registers, memory, "é"))