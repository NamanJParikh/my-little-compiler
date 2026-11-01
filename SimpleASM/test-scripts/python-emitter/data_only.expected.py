from reference_runtime import run_reference

registers = [0] * 17
memory = bytearray.fromhex("0000000000000000ffffffffffffffff6564676500")

raise SystemExit(run_reference(registers, memory))
