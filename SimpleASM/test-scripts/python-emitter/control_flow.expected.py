from reference_runtime import run_reference

registers = [0] * 17
registers[0] = 0
registers[1] = 5
registers[2] = 6
registers[3] = 1
registers[4] = 2
registers[5] = 3
registers[6] = 1 << 63
registers[8] = 4
registers[9] = 5
registers[11] = 3
registers[12] = 0

raise SystemExit(run_reference(registers, bytearray()))
