from reference_runtime import run_reference

MASK64 = (1 << 64) - 1
registers = [0] * 17
registers[0] = 42
registers[1] = 7
registers[2] = 3
registers[3] = 10 + 3 + 2 - 5
registers[4] = ((registers[3] - 3) * 3) // 3
registers[6] = 2
registers[5] = (-6 // 2) & MASK64
registers[7] = 14 // 4
registers[8] = 4
registers[9] = 0
registers[10] = 204
registers[11] = 240 & 204
registers[12] = (3 << 4) >> 4
registers[13] = (-3 >> 1) & MASK64
registers[14] = 0
registers[15] = registers[14]

raise SystemExit(run_reference(registers, bytearray()))
