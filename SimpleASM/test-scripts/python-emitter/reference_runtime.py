import sys


def run_reference(registers, memory, text=""):
    sys.stdout.write(text)
    print()
    print('Return code (R0): {}'.format(registers[0]))
    print('Final register values:')
    for register_index in range(16):
        print('  R{}: {}'.format(register_index, registers[register_index]))
    print('  Rc: {}'.format(registers[16]))
    print('Storage contents (hex): {}'.format(memory.hex()))
    return registers[0] & 0xff
