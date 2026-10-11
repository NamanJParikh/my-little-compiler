#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

// ============================================================================
// 1. Intermediate Representation Base Classes
// ============================================================================

// Registers
enum class Register {
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15,
    Rc      // condition register
};

// Value types
enum class Type {
    Decimal, Binary, Hex, Text
};

// Immediate values
struct Immediate {
    Type type = Type::Decimal;
    int64_t numericalValue = 0;
    std::string textValue;
};

// Addressing modes
struct Address {
    std::optional<std::string> label;
    std::optional<Register> baseRegister;
    int64_t displacement = 0;
};

// .DATA entries
struct DataEntry {
    std::string label;
    Immediate value;
};

// .CODE entries

enum class Opcode {
    DEF,
    // Load & Store
    LI, LR, LA, LMU, LMS, ST,
    // Logical
    AND, OR, XOR, LSH, RSH, RSHS,
    // Arithmetic
    ADD, ADDI, SUB, SUBI, MUL, MULS, DIV, DIVS,
    // Control Flow
    CMP, CMPS, BRU, BRC,
    // System Calls
    EXIT, WRTE
};

using Operand = std::variant<Register, Immediate, Address, std::string>;

struct Instruction {
    std::optional<std::string> label;
    Opcode opcode;
    std::vector<Operand> operands;
};

// Full program
struct Program {
    std::vector<DataEntry> dataSection;
    std::vector<Instruction> codeSection;
    std::map<std::string, uint64_t> symbolTable;    // For storing labels
};

// ============================================================================
// 2. Backend / Emitter Framework
// ============================================================================

enum class Architecture {
    X86,
    ARM,
    IBMZ,
    // emulator
    Python,
};

// ============================================================================
// Python emitter code-generation helpers
// ============================================================================

static std::string registerName(Register reg);
static std::string opcodeName(Opcode opcode);

static std::string pythonStringLiteral(const std::string& value) {
    static constexpr char hexDigits[] = "0123456789abcdef";
    std::string result = "'";
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '\'': result += "\\'"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                if (ch < 0x20 || ch == 0x7f) {
                    result += "\\x";
                    result += hexDigits[ch >> 4];
                    result += hexDigits[ch & 0x0f];
                } else {
                    result += static_cast<char>(ch);
                }
        }
    }
    result += '\'';
    return result;
}

static const Operand& pythonOperandAt(const Instruction& instruction, std::size_t index) {
    if (index >= instruction.operands.size()) {
        throw std::invalid_argument(opcodeName(instruction.opcode) + " has too few operands");
    }
    return instruction.operands[index];
}

static void requirePythonOperandCount(const Instruction& instruction, std::size_t count) {
    if (instruction.operands.size() != count) {
        throw std::invalid_argument(opcodeName(instruction.opcode) + " requires " +
                                    std::to_string(count) + " operands");
    }
}

static Register pythonRegisterOperand(const Instruction& instruction, std::size_t index) {
    const auto* reg = std::get_if<Register>(&pythonOperandAt(instruction, index));
    if (!reg) {
        throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                    std::to_string(index + 1) + " must be a register");
    }
    return *reg;
}

static int64_t pythonImmediateOperand(const Instruction& instruction, std::size_t index) {
    const auto* immediate = std::get_if<Immediate>(&pythonOperandAt(instruction, index));
    if (!immediate || immediate->type == Type::Text) {
        throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                    std::to_string(index + 1) + " must be a numeric immediate");
    }
    return immediate->numericalValue;
}

static std::string pythonRegisterExpression(Register reg) {
    return "registers[" + std::to_string(static_cast<int>(reg)) + "]";
}

static std::string pythonAddressExpression(const Instruction& instruction, std::size_t index,
                                           const std::map<std::string, uint64_t>& dataLabels) {
    const auto* address = std::get_if<Address>(&pythonOperandAt(instruction, index));
    if (!address || (!address->label && !address->baseRegister)) {
        throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                    std::to_string(index + 1) + " must be an address");
    }

    std::ostringstream expression;
    expression << "address_of(";
    if (address->label) {
        if (dataLabels.find(*address->label) == dataLabels.end()) {
            throw std::invalid_argument("Unknown data label in address: " + *address->label);
        }
        expression << pythonStringLiteral(*address->label) << ", None";
    } else {
        expression << "None, " << static_cast<int>(*address->baseRegister);
    }
    expression << ", " << address->displacement << ", registers, data_labels)";
    return expression.str();
}

static std::string pythonBranchLabel(const Instruction& instruction, std::size_t index,
                                     const std::map<std::string, std::size_t>& codeLabels) {
    const Operand& operand = pythonOperandAt(instruction, index);
    std::string label;
    if (const auto* address = std::get_if<Address>(&operand); address && address->label) {
        label = *address->label;
    } else if (const auto* value = std::get_if<std::string>(&operand)) {
        label = *value;
    } else {
        throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                    std::to_string(index + 1) + " must be a code label");
    }
    if (codeLabels.find(label) == codeLabels.end()) {
        throw std::invalid_argument("Unknown code label: " + label);
    }
    return label;
}

static void emitPythonInstruction(std::ostream& out, const Instruction& instruction, std::size_t index,
                                  const std::map<std::string, std::size_t>& codeLabels,
                                  const std::map<std::string, uint64_t>& dataLabels) {
    auto regExpr = [&](std::size_t operandIndex) {
        return pythonRegisterExpression(pythonRegisterOperand(instruction, operandIndex));
    };
    auto setReg = [&](std::size_t operandIndex, const std::string& value) {
        const Register reg = pythonRegisterOperand(instruction, operandIndex);
        out << "            set_register(registers, " << static_cast<int>(reg) << ", " << value << ")\n";
    };
    auto addressExpr = [&](std::size_t operandIndex) {
        return pythonAddressExpression(instruction, operandIndex, dataLabels);
    };
    auto emitNext = [&]() {
        out << "            pc += 1\n";
    };
    auto emitBinary = [&](const char* operation, bool signedOperands = false) {
        requirePythonOperandCount(instruction, 2);
        const std::string left = regExpr(0);
        const std::string right = regExpr(1);
        const std::string lhs = signedOperands ? "signed64(" + left + ")" : left;
        const std::string rhs = signedOperands ? "signed64(" + right + ")" : right;
        setReg(0, lhs + " " + operation + " " + rhs);
        emitNext();
    };

    out << "        " << (index == 0 ? "if" : "elif") << " pc == " << index << ":\n";
    switch (instruction.opcode) {
        case Opcode::LI:
            requirePythonOperandCount(instruction, 2);
            if (const auto* immediate = std::get_if<Immediate>(&pythonOperandAt(instruction, 1));
                immediate && immediate->type != Type::Text) {
                setReg(0, std::to_string(immediate->numericalValue));
            } else {
                throw std::invalid_argument("LI operand 2 must be a numeric immediate");
            }
            emitNext();
            break;
        case Opcode::LR:
            requirePythonOperandCount(instruction, 2);
            setReg(0, regExpr(1));
            emitNext();
            break;
        case Opcode::LA:
            requirePythonOperandCount(instruction, 2);
            setReg(0, addressExpr(1));
            emitNext();
            break;
        case Opcode::LMU:
        case Opcode::LMS: {
            requirePythonOperandCount(instruction, 3);
            const int64_t size = pythonImmediateOperand(instruction, 2);
            if (size < 1 || size > 8) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " size must be between 1 and 8 bytes");
            }
            const std::string read = "read_memory(memory, " + addressExpr(1) + ", " +
                                    std::to_string(size) + ", " +
                                    (instruction.opcode == Opcode::LMS ? "True" : "False") + ")";
            setReg(0, read);
            emitNext();
            break;
        }
        case Opcode::ST: {
            requirePythonOperandCount(instruction, 3);
            const int64_t size = pythonImmediateOperand(instruction, 2);
            if (size < 1 || size > 8) {
                throw std::invalid_argument("ST size must be between 1 and 8 bytes");
            }
            out << "            write_memory(memory, " << addressExpr(1) << ", " << regExpr(0)
                << ", " << size << ")\n";
            emitNext();
            break;
        }
        case Opcode::AND: emitBinary("&"); break;
        case Opcode::OR: emitBinary("|"); break;
        case Opcode::XOR: emitBinary("^"); break;
        case Opcode::ADD: emitBinary("+"); break;
        case Opcode::SUB: emitBinary("-"); break;
        case Opcode::MUL: emitBinary("*"); break;
        case Opcode::MULS: emitBinary("*", true); break;
        case Opcode::DIV:
        case Opcode::DIVS: {
            requirePythonOperandCount(instruction, 2);
            const std::string lhs = regExpr(0);
            const std::string rhs = regExpr(1);
            if (instruction.opcode == Opcode::DIV) {
                setReg(0, lhs + " // " + rhs);
            } else {
                setReg(0, "signed_divide(" + lhs + ", " + rhs + ")");
            }
            emitNext();
            break;
        }
        case Opcode::ADDI:
        case Opcode::SUBI: {
            requirePythonOperandCount(instruction, 2);
            const int64_t immediate = pythonImmediateOperand(instruction, 1);
            setReg(0, regExpr(0) + (instruction.opcode == Opcode::ADDI ? " + " : " - ") +
                      std::to_string(immediate));
            emitNext();
            break;
        }
        case Opcode::LSH:
        case Opcode::RSH:
        case Opcode::RSHS: {
            requirePythonOperandCount(instruction, 2);
            const int64_t shift = pythonImmediateOperand(instruction, 1);
            if (shift < 0 || shift > 63) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " shift must be between 0 and 63");
            }
            const std::string value = regExpr(0);
            std::string shifted = value + (instruction.opcode == Opcode::LSH ? " << " : " >> ") +
                                  std::to_string(shift);
            if (instruction.opcode == Opcode::RSHS) {
                shifted = "signed64(" + value + ") >> " + std::to_string(shift);
            }
            setReg(0, shifted);
            emitNext();
            break;
        }
        case Opcode::CMP:
        case Opcode::CMPS: {
            requirePythonOperandCount(instruction, 2);
            const std::string left = regExpr(0);
            const std::string right = regExpr(1);
            const std::string lhs = instruction.opcode == Opcode::CMPS ? "signed64(" + left + ")" : left;
            const std::string rhs = instruction.opcode == Opcode::CMPS ? "signed64(" + right + ")" : right;
            out << "            registers[16] = 0 if " << lhs << " == " << rhs
                << " else (1 if " << lhs << " < " << rhs << " else 2)\n";
            emitNext();
            break;
        }
        case Opcode::BRU: {
            requirePythonOperandCount(instruction, 1);
            const std::string label = pythonBranchLabel(instruction, 0, codeLabels);
            out << "            pc = code_labels[" << pythonStringLiteral(label) << "]\n";
            break;
        }
        case Opcode::BRC: {
            requirePythonOperandCount(instruction, 2);
            const std::string label = pythonBranchLabel(instruction, 0, codeLabels);
            const Operand& conditionOperand = pythonOperandAt(instruction, 1);
            std::string condition;
            if (const auto* value = std::get_if<std::string>(&conditionOperand)) {
                if (value->size() != 2 ||
                    !std::all_of(value->begin(), value->end(), [](char ch) { return ch == '0' || ch == '1'; })) {
                    throw std::invalid_argument("BRC condition must be a 2-bit condition code");
                }
                condition = "int(" + pythonStringLiteral(*value) + ", 2)";
            } else if (const auto* immediate = std::get_if<Immediate>(&conditionOperand);
                       immediate && immediate->type != Type::Text &&
                       immediate->numericalValue >= 0 && immediate->numericalValue <= 3) {
                condition = std::to_string(immediate->numericalValue);
            } else {
                throw std::invalid_argument("BRC condition must be a 2-bit condition code");
            }
            out << "            if registers[16] == " << condition << ":\n"
                << "                pc = code_labels[" << pythonStringLiteral(label) << "]\n"
                << "            else:\n"
                << "                pc += 1\n";
            break;
        }
        case Opcode::WRTE:
            requirePythonOperandCount(instruction, 1);
            out << "            write_text(memory, " << addressExpr(0) << ")\n";
            emitNext();
            break;
        case Opcode::EXIT:
            requirePythonOperandCount(instruction, 0);
            out << "            break\n";
            break;
        case Opcode::DEF:
            throw std::invalid_argument("DEF must be represented in the data section");
    }
}

// ============================================================================
// Emitter interface and target backend classes
// ============================================================================

class Emitter {
    public:
        virtual ~Emitter() = default;
        virtual std::vector<uint8_t> emit(const Program& program) const = 0;
};

class X86Emitter final : public Emitter {
    public:
        std::vector<uint8_t> emit(const Program& program) const override {
            (void)program;
            throw std::logic_error("x86 emitter is not implemented");
        }
};

class ARMEmitter final : public Emitter {
    public:
        std::vector<uint8_t> emit(const Program& program) const override {
            (void)program;
            throw std::logic_error("ARM emitter is not implemented");
        }
};

class IBMZEmitter final : public Emitter {
    public:
        std::vector<uint8_t> emit(const Program& program) const override {
            (void)program;
            throw std::logic_error("IBM Z emitter is not implemented");
        }
};

class PythonEmitter final : public Emitter {
    public:
        std::vector<uint8_t> emit(const Program& program) const override {
            std::vector<uint8_t> memory;
            std::map<std::string, uint64_t> dataLabels;
            for (const auto& entry : program.dataSection) {
                if (!entry.label.empty()) {
                    dataLabels[entry.label] = memory.size();
                }
                if (entry.value.type == Type::Text) {
                    for (unsigned char byte : entry.value.textValue) {
                        memory.push_back(byte);
                    }
                    memory.push_back(0);
                } else {
                    const uint64_t value = static_cast<uint64_t>(entry.value.numericalValue);
                    for (int byte = 0; byte < 8; ++byte) {
                        memory.push_back(static_cast<uint8_t>(value >> (byte * 8)));
                    }
                }
            }

            std::map<std::string, std::size_t> codeLabels;
            for (std::size_t index = 0; index < program.codeSection.size(); ++index) {
                if (program.codeSection[index].label) {
                    codeLabels[*program.codeSection[index].label] = index;
                }
            }

            std::ostringstream script;
            script << "#!/usr/bin/env python3\n"
                      "\"\"\"Generated Python emulator for a SimpleASM program.\"\"\"\n"
                      "import sys\n\n"
                      "MASK64 = (1 << 64) - 1\n\n"
                      "def signed64(value):\n"
                      "    value &= MASK64\n"
                      "    return value - (1 << 64) if value & (1 << 63) else value\n\n"
                      "def set_register(registers, index, value):\n"
                      "    registers[index] = value & (3 if index == 16 else MASK64)\n\n"
                      "def signed_divide(left, right):\n"
                      "    left, right = signed64(left), signed64(right)\n"
                      "    if right == 0:\n"
                      "        raise ZeroDivisionError('SimpleASM signed division by zero')\n"
                      "    quotient = abs(left) // abs(right)\n"
                      "    return -quotient if (left < 0) != (right < 0) else quotient\n\n"
                      "def address_of(label, base_register, displacement, registers, data_labels):\n"
                      "    if label is not None:\n"
                      "        return data_labels[label] + displacement\n"
                      "    return registers[base_register] + displacement\n\n"
                      "def check_memory_range(memory, address, size):\n"
                      "    if address < 0 or size < 0 or address + size > len(memory):\n"
                      "        raise IndexError('SimpleASM memory access out of bounds')\n\n"
                      "def read_memory(memory, address, size, signed=False):\n"
                      "    check_memory_range(memory, address, size)\n"
                      "    return int.from_bytes(memory[address:address + size], 'little', signed=signed)\n\n"
                      "def write_memory(memory, address, value, size):\n"
                      "    check_memory_range(memory, address, size)\n"
                      "    mask = (1 << (size * 8)) - 1\n"
                      "    memory[address:address + size] = (value & mask).to_bytes(size, 'little')\n\n"
                      "def write_text(memory, address):\n"
                      "    if address < 0 or address > len(memory):\n"
                      "        raise IndexError('SimpleASM text address out of bounds')\n"
                      "    end = memory.find(b'\\0', address)\n"
                      "    if end < 0:\n"
                      "        end = len(memory)\n"
                      "    sys.stdout.write(bytes(memory[address:end]).decode('ascii'))\n\n"
                      "def main():\n"
                      "    registers = [0] * 17\n"
                      "    memory = bytearray([";
            for (std::size_t i = 0; i < memory.size(); ++i) {
                if (i != 0) {
                    script << ", ";
                }
                script << static_cast<unsigned int>(memory[i]);
            }
            script << "])\n"
                      "    data_labels = {\n";
            for (const auto& label : dataLabels) {
                script << "        " << pythonStringLiteral(label.first) << ": " << label.second << ",\n";
            }
            script << "    }\n"
                      "    code_labels = {\n";
            for (const auto& label : codeLabels) {
                script << "        " << pythonStringLiteral(label.first) << ": " << label.second << ",\n";
            }
            script << "    }\n"
                      "    pc = 0\n"
                      "    while 0 <= pc < " << program.codeSection.size() << ":\n";
            if (program.codeSection.empty()) {
                script << "        break\n";
            } else {
                for (std::size_t index = 0; index < program.codeSection.size(); ++index) {
                    emitPythonInstruction(script, program.codeSection[index], index, codeLabels, dataLabels);
                }
                script << "        else:\n"
                          "            raise RuntimeError('Invalid SimpleASM program counter: {}'.format(pc))\n";
            }
            script << "    print()\n"
                      "    print('Return code (R0): {}'.format(registers[0]))\n"
                      "    print('Final register values:')\n"
                      "    for register_index in range(16):\n"
                      "        print('  R{}: {}'.format(register_index, registers[register_index]))\n"
                      "    print('  Rc: {}'.format(registers[16]))\n"
                      "    print('Storage contents (hex): {}'.format(memory.hex()))\n"
                      "    return registers[0] & 0xff\n\n"
                      "if __name__ == '__main__':\n"
                      "    sys.exit(main())\n";

            const std::string output = script.str();
            std::vector<uint8_t> bytes;
            bytes.reserve(output.size());
            for (unsigned char byte : output) {
                bytes.push_back(byte);
            }
            return bytes;
        }
};

std::unique_ptr<Emitter> createEmitter(Architecture architecture) {
    switch (architecture) {
        case Architecture::X86:
            return std::make_unique<X86Emitter>();
        case Architecture::ARM:
            return std::make_unique<ARMEmitter>();
        case Architecture::IBMZ:
            return std::make_unique<IBMZEmitter>();
        case Architecture::Python:
            return std::make_unique<PythonEmitter>();
    }

    throw std::invalid_argument("Unsupported target architecture");
}

// ============================================================================
// 3. Frontend / Parser
// ============================================================================

// Per-file parser object
class Parser {
    public:
        explicit Parser(std::string fileName) : filename_(std::move(fileName)) {}
        Program parse() {
            return parse_();
        }

    private:
        std::string filename_;

        static void trim_(std::string& s) {
            auto start = std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            });
            if (start == s.end()) {
                s.clear();
                return;
            }
            s.erase(s.begin(), start);
            auto end = std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base();
            s.erase(end, s.end());
        }

        Program parse_() {
            std::ifstream file(filename_);
            if (!file.is_open()) {
                throw std::runtime_error("Could not open file: " + filename_);
            }

            Program program;
            std::string currLine;
            bool inData = false;
            bool inCode = false;

            while (std::getline(file, currLine)) {
                trim_(currLine);
                if (currLine.empty() || currLine[0] == '*') {
                    continue;
                }

                if (currLine == ".DATA") {
                    inData = true;
                    inCode = false;
                    continue;
                }
                if (currLine == ".CODE") {
                    inData = false;
                    inCode = true;
                    continue;
                }

                if (inData) {
                    parseData_(program, currLine);
                } else if (inCode) {
                    parseCode_(program, currLine);
                }
            }

            return program;
        }

        std::vector<std::string> lexLine_(const std::string& line) {
            std::istringstream stream(line);
            std::string token;
            std::vector<std::string> tokens;
            while (stream >> token) {
                tokens.push_back(token);
            }
            return tokens;
        }

        static bool isOpcode_(const std::string& text) {
            static const std::map<std::string, Opcode> opcodes = {
                {"DEF", Opcode::DEF}, {"LI", Opcode::LI}, {"LR", Opcode::LR}, {"LA", Opcode::LA},
                {"LMU", Opcode::LMU}, {"LMS", Opcode::LMS}, {"ST", Opcode::ST}, {"AND", Opcode::AND},
                {"OR", Opcode::OR}, {"XOR", Opcode::XOR}, {"LSH", Opcode::LSH}, {"RSH", Opcode::RSH},
                {"RSHS", Opcode::RSHS}, {"ADD", Opcode::ADD}, {"ADDI", Opcode::ADDI}, {"SUB", Opcode::SUB},
                {"SUBI", Opcode::SUBI}, {"MUL", Opcode::MUL}, {"MULS", Opcode::MULS}, {"DIV", Opcode::DIV},
                {"DIVS", Opcode::DIVS}, {"CMP", Opcode::CMP}, {"CMPS", Opcode::CMPS}, {"BRU", Opcode::BRU},
                {"BRC", Opcode::BRC}, {"EXIT", Opcode::EXIT}, {"WRTE", Opcode::WRTE}
            };
            return opcodes.find(text) != opcodes.end();
        }

        static Opcode stringToOpcode_(const std::string& opcodeText) {
            static const std::map<std::string, Opcode> opcodes = {
                {"DEF", Opcode::DEF}, {"LI", Opcode::LI}, {"LR", Opcode::LR}, {"LA", Opcode::LA},
                {"LMU", Opcode::LMU}, {"LMS", Opcode::LMS}, {"ST", Opcode::ST}, {"AND", Opcode::AND},
                {"OR", Opcode::OR}, {"XOR", Opcode::XOR}, {"LSH", Opcode::LSH}, {"RSH", Opcode::RSH},
                {"RSHS", Opcode::RSHS}, {"ADD", Opcode::ADD}, {"ADDI", Opcode::ADDI}, {"SUB", Opcode::SUB},
                {"SUBI", Opcode::SUBI}, {"MUL", Opcode::MUL}, {"MULS", Opcode::MULS}, {"DIV", Opcode::DIV},
                {"DIVS", Opcode::DIVS}, {"CMP", Opcode::CMP}, {"CMPS", Opcode::CMPS}, {"BRU", Opcode::BRU},
                {"BRC", Opcode::BRC}, {"EXIT", Opcode::EXIT}, {"WRTE", Opcode::WRTE}
            };

            auto it = opcodes.find(opcodeText);
            if (it == opcodes.end()) {
                throw std::runtime_error("Unknown instruction opcode: " + opcodeText);
            }
            return it->second;
        }

        static Immediate parseImmediate_(const std::string& token) {
            if (token.empty()) {
                throw std::runtime_error("Empty immediate value");
            }

            Immediate immediate;
            const char prefix = token.front();
            const std::string payload = token.substr(1);

            switch (prefix) {
                case 'd':
                    immediate.type = Type::Decimal;
                    immediate.numericalValue = std::stoll(payload, nullptr, 10);
                    break;
                case 'b':
                    immediate.type = Type::Binary;
                    immediate.numericalValue = std::stoll(payload, nullptr, 2);
                    break;
                case 'x':
                    immediate.type = Type::Hex;
                    immediate.numericalValue = std::stoll(payload, nullptr, 16);
                    break;
                case 't':
                    immediate.type = Type::Text;
                    if (payload.size() < 2 || payload.front() != '\'' || payload.back() != '\'') {
                        throw std::runtime_error("Text immediate must be enclosed in single quotes: " + token);
                    }
                    immediate.textValue = payload.substr(1, payload.size() - 2);
                    break;
                default:
                    throw std::runtime_error("Invalid immediate prefix: " + token);
            }
            return immediate;
        }

        static Register parseRegister_(const std::string& regStr) {
            if (regStr == "Rc") {
                return Register::Rc;
            }

            if (regStr.size() > 1 && regStr[0] == 'R') {
                const int regNum = std::stoi(regStr.substr(1));
                if (regNum >= 0 && regNum <= 15) {
                    return static_cast<Register>(regNum);
                }
            }

            throw std::runtime_error("Invalid register identifier: " + regStr);
        }

        static Address parseAddress_(const std::string& token) {
            Address addr;

            if (token.size() >= 3 && token.front() == '[' && token.back() == ']') {
                const std::string inner = token.substr(1, token.size() - 2);
                std::stringstream ss(inner);
                std::string regText;
                if (ss >> regText) {
                    addr.baseRegister = parseRegister_(regText);
                    std::string op;
                    std::string valueText;
                    if (ss >> op >> valueText) {
                        const int64_t value = parseImmediate_(valueText).numericalValue;
                        addr.displacement = (op == "-") ? -value : value;
                    }
                }
                return addr;
            }

            addr.label = token;
            return addr;
        }

        void parseData_(Program& program, const std::string& line) {
            std::istringstream stream(line);
            std::string label;
            std::string valueText;

            if (!(stream >> label)) {
                throw std::runtime_error("Malformed .DATA entry: " + line);
            }
            std::getline(stream, valueText);
            trim_(valueText);
            if (valueText.empty()) {
                throw std::runtime_error("Malformed .DATA entry: " + line);
            }

            DataEntry entry;
            entry.label = label;
            entry.value = parseImmediate_(valueText);
            program.dataSection.push_back(entry);
            program.symbolTable[label] = program.dataSection.size() - 1;
        }

        void parseCode_(Program& program, const std::string& line) {
            std::istringstream stream(line);
            std::string firstToken;
            stream >> firstToken;

            Instruction inst;
            std::string opcodeText;

            if (!firstToken.empty() && std::all_of(firstToken.begin(), firstToken.end(), [](unsigned char ch) {
                    return std::isupper(ch) || std::isdigit(ch);
                }) && !isOpcode_(firstToken)) {
                inst.label = firstToken;
                stream >> opcodeText;
            } else {
                opcodeText = firstToken;
            }

            inst.opcode = stringToOpcode_(opcodeText);

            std::string operandText;
            while (std::getline(stream, operandText, ',')) {
                trim_(operandText);
                if (operandText.empty()) {
                    continue;
                }

                if (operandText[0] == 'R' || operandText == "Rc") {
                    inst.operands.push_back(parseRegister_(operandText));
                } else if (operandText[0] == '[' || operandText[0] == 'A' || operandText[0] == 'B' ||
                           operandText[0] == 'C' || operandText[0] == 'D' || operandText[0] == 'E' ||
                           operandText[0] == 'F' || operandText[0] == 'G' || operandText[0] == 'H' ||
                           operandText[0] == 'I' || operandText[0] == 'J' || operandText[0] == 'K' ||
                           operandText[0] == 'L' || operandText[0] == 'M' || operandText[0] == 'N' ||
                           operandText[0] == 'O' || operandText[0] == 'P' || operandText[0] == 'Q' ||
                           operandText[0] == 'S' || operandText[0] == 'T' || operandText[0] == 'U' ||
                           operandText[0] == 'V' || operandText[0] == 'W' || operandText[0] == 'X' ||
                           operandText[0] == 'Y' || operandText[0] == 'Z') {
                    inst.operands.push_back(parseAddress_(operandText));
                } else if (operandText[0] == 'd' || operandText[0] == 'b' || operandText[0] == 'x' || operandText[0] == 't') {
                    inst.operands.push_back(parseImmediate_(operandText));
                } else {
                    inst.operands.push_back(operandText);
                }
            }

            if (inst.opcode == Opcode::DEF) {
                if (inst.operands.size() != 1 || !std::holds_alternative<Immediate>(inst.operands[0])) {
                    throw std::runtime_error("DEF requires exactly one immediate value: " + line);
                }

                const std::string label = inst.label.value_or("");
                program.dataSection.push_back({label, std::get<Immediate>(inst.operands[0])});
                if (!label.empty()) {
                    program.symbolTable[label] = program.dataSection.size() - 1;
                }
                return;
            }

            program.codeSection.push_back(inst);
            if (inst.label.has_value()) {
                program.symbolTable[inst.label.value()] = program.codeSection.size() - 1;
            }
        }
};

// ============================================================================
// Temp. Parser CLI Entry Point
// ============================================================================

static std::string registerName(Register reg) {
    if (reg == Register::Rc) {
        return "Rc";
    }
    return "R" + std::to_string(static_cast<int>(reg));
}

static std::string opcodeName(Opcode opcode) {
    static const std::map<Opcode, std::string> names = {
        {Opcode::DEF, "DEF"}, {Opcode::LI, "LI"}, {Opcode::LR, "LR"}, {Opcode::LA, "LA"},
        {Opcode::LMU, "LMU"}, {Opcode::LMS, "LMS"}, {Opcode::ST, "ST"}, {Opcode::AND, "AND"},
        {Opcode::OR, "OR"}, {Opcode::XOR, "XOR"}, {Opcode::LSH, "LSH"}, {Opcode::RSH, "RSH"},
        {Opcode::RSHS, "RSHS"}, {Opcode::ADD, "ADD"}, {Opcode::ADDI, "ADDI"}, {Opcode::SUB, "SUB"},
        {Opcode::SUBI, "SUBI"}, {Opcode::MUL, "MUL"}, {Opcode::MULS, "MULS"}, {Opcode::DIV, "DIV"},
        {Opcode::DIVS, "DIVS"}, {Opcode::CMP, "CMP"}, {Opcode::CMPS, "CMPS"}, {Opcode::BRU, "BRU"},
        {Opcode::BRC, "BRC"}, {Opcode::EXIT, "EXIT"}, {Opcode::WRTE, "WRTE"}
    };
    auto it = names.find(opcode);
    return it == names.end() ? "<unknown opcode>" : it->second;
}

static void printImmediate(std::ostream& out, const Immediate& immediate) {
    switch (immediate.type) {
        case Type::Decimal:
            out << "Decimal(" << immediate.numericalValue << ")";
            break;
        case Type::Binary:
            out << "Binary(" << immediate.numericalValue << ")";
            break;
        case Type::Hex:
            out << "Hex(" << immediate.numericalValue << ")";
            break;
        case Type::Text:
            out << "Text(\"" << immediate.textValue << "\")";
            break;
    }
}

static void printOperand(std::ostream& out, const Operand& operand) {
    if (const auto* reg = std::get_if<Register>(&operand)) {
        out << registerName(*reg);
    } else if (const auto* immediate = std::get_if<Immediate>(&operand)) {
        printImmediate(out, *immediate);
    } else if (const auto* address = std::get_if<Address>(&operand)) {
        if (address->label) {
            out << *address->label;
        } else if (address->baseRegister) {
            out << "[" << registerName(*address->baseRegister);
            if (address->displacement > 0) {
                out << " + " << address->displacement;
            } else if (address->displacement < 0) {
                out << " - " << -address->displacement;
            }
            out << "]";
        } else {
            out << "<invalid address>";
        }
    } else {
        out << std::get<std::string>(operand);
    }
}

static void printProgram(const Program& program, std::ostream& out) {
    out << ".DATA (" << program.dataSection.size() << " entries)\n";
    int entryIndex = 0;
    for (const auto& entry : program.dataSection) {
        out << "  " << entryIndex << ": " << (entry.label.empty() ? "<anonymous>" : entry.label) << " = ";
        printImmediate(out, entry.value);
        out << '\n';
        entryIndex++;
    }

    out << ".CODE (" << program.codeSection.size() << " instructions)\n";
    for (const auto& instruction : program.codeSection) {
        out << "  ";
        if (instruction.label) {
            out << *instruction.label << ": ";
        }
        out << opcodeName(instruction.opcode);
        for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
            out << (i == 0 ? " " : ", ");
            printOperand(out, instruction.operands[i]);
        }
        out << '\n';
    }

    out << "Symbols (" << program.symbolTable.size() << " entries)\n";
    for (const auto& symbol : program.symbolTable) {
        out << "  " << symbol.first << " -> " << symbol.second << '\n';
    }
}

static std::optional<Architecture> parseArchitecture_(const std::string& value) {
    if (value == "x86" || value == "X86") {
        return Architecture::X86;
    }
    if (value == "arm" || value == "ARM") {
        return Architecture::ARM;
    }
    if (value == "ibmz" || value == "IBMZ") {
        return Architecture::IBMZ;
    }
    if (value == "python" || value == "Python") {
        return Architecture::Python;
    }
    return std::nullopt;
}

static void printUsage_(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--emit <target>] [-o <output_file>] <source_file.s>\n"
              << "Targets: print, x86, arm, ibmz, python\n";
}

int main(int argc, char* argv[]) {
    std::optional<Architecture> target;
    std::string sourceFile;
    std::string outputFile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage_(argv[0]);
            return 0;
        }
        if (arg == "--emit" || arg == "-e") {
            if (i + 1 >= argc) {
                printUsage_(argv[0]);
                return 1;
            }
            const std::string next = argv[++i];
            if (next == "print") {
                target = std::nullopt;
                continue;
            }
            const auto parsedTarget = parseArchitecture_(next);
            if (!parsedTarget.has_value()) {
                std::cerr << "Unsupported target: " << next << '\n';
                printUsage_(argv[0]);
                return 1;
            }
            target = parsedTarget;
            continue;
        }
        if (arg == "--output" || arg == "-o") {
            if (i + 1 >= argc) {
                printUsage_(argv[0]);
                return 1;
            }
            outputFile = argv[++i];
            continue;
        }
        if (arg.rfind("--emit=", 0) == 0) {
            const std::string next = arg.substr(7);
            if (next == "print") {
                target = std::nullopt;
                continue;
            }
            const auto parsedTarget = parseArchitecture_(next);
            if (!parsedTarget.has_value()) {
                std::cerr << "Unsupported target: " << next << '\n';
                printUsage_(argv[0]);
                return 1;
            }
            target = parsedTarget;
            continue;
        }
        if (sourceFile.empty()) {
            sourceFile = arg;
            continue;
        }

        std::cerr << "Unexpected argument: " << arg << '\n';
        printUsage_(argv[0]);
        return 1;
    }

    if (sourceFile.empty()) {
        printUsage_(argv[0]);
        return 1;
    }
    if (!outputFile.empty() && (!target.has_value() || *target != Architecture::Python)) {
        std::cerr << "--output is only supported with --emit python\n";
        return 1;
    }

    try {
        Parser parser(sourceFile);
        const Program program = parser.parse();

        if (target.has_value()) {
            const auto emitter = createEmitter(*target);
            const auto output = emitter->emit(program);
            if (*target == Architecture::Python) {
                std::filesystem::path outputPath = outputFile.empty()
                    ? std::filesystem::path(sourceFile).replace_extension(".py")
                    : std::filesystem::path(outputFile);
                if (std::filesystem::absolute(outputPath).lexically_normal() ==
                    std::filesystem::absolute(sourceFile).lexically_normal()) {
                    throw std::runtime_error("Output file must not overwrite the source file");
                }

                std::ofstream file(outputPath, std::ios::binary);
                if (!file.is_open()) {
                    throw std::runtime_error("Could not open output file: " + outputPath.string());
                }
                file.write(reinterpret_cast<const char*>(output.data()),
                           static_cast<std::streamsize>(output.size()));
                if (!file) {
                    throw std::runtime_error("Could not write output file: " + outputPath.string());
                }
            }
            std::cout.write(reinterpret_cast<const char*>(output.data()),
                            static_cast<std::streamsize>(output.size()));
            return 0;
        }

        printProgram(program, std::cout);
    } catch (const std::exception& error) {
        std::cerr << "Compiler error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}