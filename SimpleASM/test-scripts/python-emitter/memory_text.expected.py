from reference_runtime import run_reference

MASK64 = (1 << 64) - 1
registers = [0] * 17
registers[0] = 73
registers[1] = 0
registers[2] = 8
registers[3] = 0x0506
registers[4] = 0x0102030405060708
registers[5] = 2
registers[6] = 254
registers[7] = (-2) & MASK64
registers[8] = 8
registers[9] = 4660
registers[10] = 4660
registers[11] = 32768
registers[12] = (-32768) & MASK64
registers[13] = 16
registers[14] = 0x05060708
registers[15] = 99

memory = bytearray.fromhex(
    "0807060504030201"
    "fe07341200800201"
    "53696d706c6541534d00"
    "2074657374732100"
    "6300000000000000"
)

raise SystemExit(run_reference(registers, memory, "SimpleASM tests!"))
