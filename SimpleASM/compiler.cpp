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

class X86CodeGenerator {
    private:
        struct RipFixup {
            std::size_t fieldOffset;
            int64_t targetDataOffset;
            std::size_t nextInstructionOffset;
        };
        struct BranchFixup {
            std::size_t fieldOffset;
            std::string label;
        };

        // RSP stays reserved for the native process stack. Abstract these
        // mappings so additional SimpleASM registers can later use spill slots.
        static constexpr int DirectGuestRegisterCount = 8;

        const Program& program_;
        std::vector<uint8_t> code_;
        std::vector<uint8_t> data_;
        std::map<std::string, std::size_t> dataLabels_;
        std::map<std::string, std::size_t> codeLabels_;
        std::vector<RipFixup> ripFixups_;
        std::vector<BranchFixup> branchFixups_;
        std::size_t conditionOffset_ = 0;
        static constexpr std::size_t CodeFileOffset = 0x1000;
        static constexpr uint64_t ImageBase = 0x400000;

        static void append8(std::vector<uint8_t>& output, uint8_t value) {
            output.push_back(value);
        }

        static void append32(std::vector<uint8_t>& output, uint32_t value) {
            for (unsigned int shift = 0; shift < 32; shift += 8) {
                output.push_back(static_cast<uint8_t>(value >> shift));
            }
        }

        static void append64(std::vector<uint8_t>& output, uint64_t value) {
            for (unsigned int shift = 0; shift < 64; shift += 8) {
                output.push_back(static_cast<uint8_t>(value >> shift));
            }
        }

        static uint32_t signed32Bits(int64_t value, const std::string& context) {
            if (value < INT32_MIN || value > INT32_MAX) {
                throw std::invalid_argument(context + " does not fit an x86-64 signed 32-bit displacement");
            }
            return static_cast<uint32_t>(static_cast<int32_t>(value));
        }

        static void patch32(std::vector<uint8_t>& output, std::size_t offset, uint32_t value) {
            if (offset + 4 > output.size()) {
                throw std::logic_error("Invalid x86-64 fixup offset");
            }
            for (unsigned int shift = 0; shift < 32; shift += 8) {
                output[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
            }
        }

        static void write16(std::vector<uint8_t>& output, std::size_t offset, uint16_t value) {
            for (unsigned int shift = 0; shift < 16; shift += 8) {
                output[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
            }
        }

        static void write32(std::vector<uint8_t>& output, std::size_t offset, uint32_t value) {
            for (unsigned int shift = 0; shift < 32; shift += 8) {
                output[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
            }
        }

        static void write64(std::vector<uint8_t>& output, std::size_t offset, uint64_t value) {
            for (unsigned int shift = 0; shift < 64; shift += 8) {
                output[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
            }
        }

        static const Operand& operandAt(const Instruction& instruction, std::size_t index) {
            if (index >= instruction.operands.size()) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " has too few operands");
            }
            return instruction.operands[index];
        }

        static void requireOperandCount(const Instruction& instruction, std::size_t count) {
            if (instruction.operands.size() != count) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " requires " +
                                            std::to_string(count) + " operands");
            }
        }

        int guestRegister(const Instruction& instruction, std::size_t index) const {
            const auto* reg = std::get_if<Register>(&operandAt(instruction, index));
            if (!reg || *reg == Register::Rc) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                            std::to_string(index + 1) + " must be a general register");
            }
            const int guestIndex = static_cast<int>(*reg);
            if (guestIndex >= DirectGuestRegisterCount) {
                throw std::invalid_argument("x86 emitter currently supports only R0 through R7");
            }
            return nativeRegister(guestIndex);
        }

        static int64_t immediate(const Instruction& instruction, std::size_t index) {
            const auto* value = std::get_if<Immediate>(&operandAt(instruction, index));
            if (!value || value->type == Type::Text) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                            std::to_string(index + 1) + " must be a numeric immediate");
            }
            return value->numericalValue;
        }

        static int nativeRegister(int guestIndex) {
            // Avoid RSP and syscall-clobbered R11; the remaining guest registers
            // survive Linux syscalls used by WRTE.
            static constexpr int mapping[DirectGuestRegisterCount] = {
                8, 9, 10, 12, 13, 14, 15, 3
            };
            if (guestIndex < 0 || guestIndex >= DirectGuestRegisterCount) {
                throw std::invalid_argument("x86 emitter currently supports only R0 through R7");
            }
            return mapping[guestIndex];
        }

        const Address& address(const Instruction& instruction, std::size_t index) const {
            const auto* value = std::get_if<Address>(&operandAt(instruction, index));
            if (!value || (!value->label && !value->baseRegister)) {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                            std::to_string(index + 1) + " must be an address");
            }
            if (value->label && dataLabels_.find(*value->label) == dataLabels_.end()) {
                throw std::invalid_argument("Unknown data label in address: " + *value->label);
            }
            if (value->baseRegister) {
                const int reg = static_cast<int>(*value->baseRegister);
                if (reg >= DirectGuestRegisterCount) {
                    throw std::invalid_argument("x86 emitter currently supports only R0 through R7");
                }
            }
            return *value;
        }

        static int registerCodeForGuest(Register reg) {
            if (reg == Register::Rc || static_cast<int>(reg) >= DirectGuestRegisterCount) {
                throw std::invalid_argument("x86 emitter currently supports only R0 through R7");
            }
            return nativeRegister(static_cast<int>(reg));
        }

        static void emitRex(std::vector<uint8_t>& output, bool wide, int reg, int index, int rm,
                            bool force = false) {
            const uint8_t rex = static_cast<uint8_t>(0x40 | (wide ? 0x08 : 0) |
                ((reg >> 3) << 2) | ((index >> 3) << 1) | (rm >> 3));
            if (rex != 0x40 || force) {
                append8(output, rex);
            }
        }

        void emitAddressModRM(int regField, const Address& value, int64_t extraDisplacement = 0) {
            if ((extraDisplacement > 0 && value.displacement > INT64_MAX - extraDisplacement) ||
                (extraDisplacement < 0 && value.displacement < INT64_MIN - extraDisplacement)) {
                throw std::invalid_argument("Address displacement overflow");
            }
            const int64_t displacement = value.displacement + extraDisplacement;
            if (value.label) {
                append8(code_, static_cast<uint8_t>(((regField & 7) << 3) | 5));
                append32(code_, 0);
                const int64_t labelOffset = static_cast<int64_t>(dataLabels_.at(*value.label));
                if ((displacement > 0 && labelOffset > INT64_MAX - displacement) ||
                    (displacement < 0 && labelOffset < INT64_MIN - displacement)) {
                    throw std::invalid_argument("Data-label displacement overflow");
                }
                ripFixups_.push_back({code_.size() - 4, labelOffset + displacement, code_.size()});
                return;
            }

            const int base = registerCodeForGuest(*value.baseRegister);
            signed32Bits(displacement, "Address displacement");
            append8(code_, static_cast<uint8_t>(0x80 | ((regField & 7) << 3) | (base & 7)));
            if ((base & 7) == 4) {
                append8(code_, 0x24); // SIB: no index, base register.
            }
            append32(code_, static_cast<uint32_t>(static_cast<int32_t>(displacement)));
        }

        void emitRegReg(uint8_t opcode, int regField, int rmField, bool wide = true,
                        bool forceRex = false) {
            emitRex(code_, wide, regField, 0, rmField, forceRex);
            append8(code_, opcode);
            append8(code_, static_cast<uint8_t>(0xC0 | ((regField & 7) << 3) | (rmField & 7)));
        }

        void emitMemInstruction(const std::vector<uint8_t>& opcode, int regField, const Address& value,
                                bool wide, bool forceRex = false) {
            const int base = value.label ? 0 : registerCodeForGuest(*value.baseRegister);
            emitRex(code_, wide, regField, 0, base, forceRex);
            code_.insert(code_.end(), opcode.begin(), opcode.end());
            emitAddressModRM(regField, value);
        }

        void emitNativeBaseMemInstruction(const std::vector<uint8_t>& opcode, int regField,
                                          int baseRegister, int32_t displacement, bool wide) {
            emitRex(code_, wide, regField, 0, baseRegister);
            code_.insert(code_.end(), opcode.begin(), opcode.end());
            append8(code_, static_cast<uint8_t>(0x80 | ((regField & 7) << 3) |
                                                (baseRegister & 7)));
            if ((baseRegister & 7) == 4) {
                append8(code_, 0x24);
            }
            append32(code_, static_cast<uint32_t>(displacement));
        }

        void emitRegImmediate(int reg, uint64_t value) {
            emitRex(code_, true, 0, 0, reg);
            append8(code_, static_cast<uint8_t>(0xB8 + (reg & 7)));
            append64(code_, value);
        }

        void emitGroupImmediateRegister(uint8_t opcode, int group, int reg, uint8_t value,
                                        bool wide = true) {
            emitRex(code_, wide, group, 0, reg);
            append8(code_, opcode);
            append8(code_, static_cast<uint8_t>(0xC0 | ((group & 7) << 3) | (reg & 7)));
            append8(code_, value);
        }

        void emitConditionStore(uint32_t value) {
            append8(code_, 0xC7);
            append8(code_, 0x05); // mov dword ptr [RIP + disp32], imm32
            const std::size_t fieldOffset = code_.size();
            append32(code_, 0);
            append32(code_, value);
            ripFixups_.push_back({fieldOffset, static_cast<int64_t>(conditionOffset_), code_.size()});
        }

        void emitConditionCompare(uint8_t value) {
            append8(code_, 0x83);
            append8(code_, 0x3D); // cmp dword ptr [RIP + disp32], imm8
            const std::size_t fieldOffset = code_.size();
            append32(code_, 0);
            append8(code_, value);
            ripFixups_.push_back({fieldOffset, static_cast<int64_t>(conditionOffset_), code_.size()});
        }

        void emitBranch(uint8_t condition, const std::string& label) {
            if (condition == 0xFF) {
                append8(code_, 0xE9);
            } else {
                append8(code_, 0x0F);
                append8(code_, static_cast<uint8_t>(0x80 | condition));
            }
            const std::size_t field = code_.size();
            append32(code_, 0);
            branchFixups_.push_back({field, label});
        }

        std::size_t emitConditionalPlaceholder(uint8_t condition) {
            append8(code_, 0x0F);
            append8(code_, static_cast<uint8_t>(0x80 | condition));
            const std::size_t field = code_.size();
            append32(code_, 0);
            return field;
        }

        void patchLocalBranch(std::size_t field, std::size_t target, const std::string& context) {
            patch32(code_, field, signed32Bits(static_cast<int64_t>(target) -
                static_cast<int64_t>(field + 4), context));
        }

        void emitLocalBranch(uint8_t condition, std::size_t targetOffset) {
            if (condition == 0xFF) {
                append8(code_, 0xE9);
            } else {
                append8(code_, 0x0F);
                append8(code_, static_cast<uint8_t>(0x80 | condition));
            }
            const std::size_t field = code_.size();
            append32(code_, 0);
            patchLocalBranch(field, targetOffset, "Local x86-64 branch");
        }

        std::string branchLabel(const Instruction& instruction, std::size_t index) const {
            const Operand& operand = operandAt(instruction, index);
            std::string label;
            if (const auto* value = std::get_if<Address>(&operand); value && value->label) {
                label = *value->label;
            } else if (const auto* text = std::get_if<std::string>(&operand)) {
                label = *text;
            } else {
                throw std::invalid_argument(opcodeName(instruction.opcode) + " operand " +
                                            std::to_string(index + 1) + " must be a code label");
            }
            if (codeLabels_.find(label) == codeLabels_.end()) {
                // Forward labels are validated after all code labels are collected.
                const bool exists = std::any_of(program_.codeSection.begin(), program_.codeSection.end(),
                    [&](const Instruction& candidate) {
                        return candidate.label && *candidate.label == label;
                    });
                if (!exists) {
                    throw std::invalid_argument("Unknown code label: " + label);
                }
            }
            return label;
        }

        uint8_t branchCondition(const Instruction& instruction, std::size_t index) const {
            const Operand& operand = operandAt(instruction, index);
            int64_t value = -1;
            if (const auto* text = std::get_if<std::string>(&operand)) {
                if (text->size() != 2 ||
                    !std::all_of(text->begin(), text->end(), [](char ch) { return ch == '0' || ch == '1'; })) {
                    throw std::invalid_argument("BRC condition must be a 2-bit condition code");
                }
                value = ((*text)[0] - '0') * 2 + ((*text)[1] - '0');
            } else if (const auto* imm = std::get_if<Immediate>(&operand);
                       imm && imm->type != Type::Text) {
                value = imm->numericalValue;
            }
            if (value < 0 || value > 3) {
                throw std::invalid_argument("BRC condition must be between 0 and 3");
            }
            return static_cast<uint8_t>(value);
        }

        void emitStore(const Instruction& instruction) {
            const int sourceRegister = guestRegister(instruction, 0);
            const Address& destination = address(instruction, 1);
            const int64_t size = immediate(instruction, 2);
            if (size < 1 || size > 8) {
                throw std::invalid_argument("ST size must be between 1 and 8 bytes");
            }
            emitRegReg(0x89, sourceRegister, 0); // Preserve the guest value in RAX.
            for (int64_t byte = 0; byte < size; ++byte) {
                Address byteAddress = destination;
                if (byte > 0 && byteAddress.displacement > INT64_MAX - byte) {
                    throw std::invalid_argument("Address displacement overflow");
                }
                byteAddress.displacement += byte;
                const int base = byteAddress.label ? 0 : registerCodeForGuest(*byteAddress.baseRegister);
                emitRex(code_, false, 0, 0, base, true);
                append8(code_, 0x88);
                emitAddressModRM(0, byteAddress);
                if (byte + 1 < size) {
                    emitGroupImmediateRegister(0xC1, 5, 0, 8);
                }
            }
        }

        void emitTextWrite(const Instruction& instruction) {
            const Address& source = address(instruction, 0);
            emitMemInstruction({0x8D}, 6, source, true); // lea rsi, [source]
            append8(code_, 0x48);
            append8(code_, 0x89);
            append8(code_, 0xF7); // mov rdi, rsi; scan without losing the start pointer.
            append8(code_, 0x31);
            append8(code_, 0xD2); // xor edx, edx
            const std::size_t scan = code_.size();
            emitRex(code_, false, 7, 0, 7, true);
            append8(code_, 0x80); // cmp byte ptr [rdi], 0
            append8(code_, 0xBF); // [rdi + disp32]
            append32(code_, 0);
            append8(code_, 0);
            const std::size_t doneScanField = emitConditionalPlaceholder(0x04); // je
            append8(code_, 0x48);
            append8(code_, 0xFF);
            append8(code_, 0xC7); // inc rdi
            append8(code_, 0x48);
            append8(code_, 0xFF);
            append8(code_, 0xC2); // inc rdx
            emitLocalBranch(0xFF, scan);
            const std::size_t afterScan = code_.size();
            patchLocalBranch(doneScanField, afterScan, "WRTE scan exit");

            const std::size_t writeLoop = code_.size();
            append8(code_, 0xB8);
            append32(code_, 1); // write syscall
            append8(code_, 0xBF);
            append32(code_, 1); // stdout
            append8(code_, 0x0F);
            append8(code_, 0x05); // syscall
            append8(code_, 0x48);
            append8(code_, 0x85);
            append8(code_, 0xC0); // test rax, rax
            const std::size_t zeroWriteField = emitConditionalPlaceholder(0x04); // je done
            const std::size_t errorBranchField = emitConditionalPlaceholder(0x08); // js error
            append8(code_, 0x48);
            append8(code_, 0x01);
            append8(code_, 0xC6); // add rsi, rax
            append8(code_, 0x48);
            append8(code_, 0x29);
            append8(code_, 0xC2); // sub rdx, rax
            const std::size_t moreBytesField = emitConditionalPlaceholder(0x05); // jne write loop
            append8(code_, 0xE9);
            const std::size_t completedField = code_.size();
            append32(code_, 0);
            const std::size_t errorHandling = code_.size();
            append8(code_, 0x48);
            append8(code_, 0x83);
            append8(code_, 0xF8);
            append8(code_, 0xFC); // cmp rax, -EINTR
            const std::size_t notInterruptedField = emitConditionalPlaceholder(0x05); // jne done
            emitLocalBranch(0xFF, writeLoop);
            const std::size_t done = code_.size();
            patchLocalBranch(zeroWriteField, done, "WRTE zero-write branch");
            patchLocalBranch(errorBranchField, errorHandling, "WRTE error branch");
            patchLocalBranch(moreBytesField, writeLoop, "WRTE partial-write branch");
            patchLocalBranch(completedField, done, "WRTE completion branch");
            patchLocalBranch(notInterruptedField, done, "WRTE error completion branch");
        }

        void emitInstruction(const Instruction& instruction) {
            auto binary = [&](uint8_t opcode, bool signedMultiply = false) {
                requireOperandCount(instruction, 2);
                const int destination = guestRegister(instruction, 0);
                const int source = guestRegister(instruction, 1);
                emitRegReg(opcode, source, destination);
                (void)signedMultiply;
            };

            switch (instruction.opcode) {
                case Opcode::LI: {
                    requireOperandCount(instruction, 2);
                    emitRegImmediate(guestRegister(instruction, 0),
                                     static_cast<uint64_t>(immediate(instruction, 1)));
                    break;
                }
                case Opcode::LR: {
                    requireOperandCount(instruction, 2);
                    const int destination = guestRegister(instruction, 0);
                    const int source = guestRegister(instruction, 1);
                    emitRegReg(0x89, source, destination);
                    break;
                }
                case Opcode::LA: {
                    requireOperandCount(instruction, 2);
                    emitMemInstruction({0x8D}, guestRegister(instruction, 0),
                                       address(instruction, 1), true);
                    break;
                }
                case Opcode::LMU:
                case Opcode::LMS: {
                    requireOperandCount(instruction, 3);
                    const int destination = guestRegister(instruction, 0);
                    const Address& source = address(instruction, 1);
                    const int64_t size = immediate(instruction, 2);
                    if (size < 1 || size > 8) {
                        throw std::invalid_argument(opcodeName(instruction.opcode) +
                                                    " size must be between 1 and 8 bytes");
                    }
                    // Resolve the address before clearing the destination; the
                    // destination may also be the address's base register.
                    emitMemInstruction({0x8D}, 6, source, true); // lea rsi, [source]
                    emitRegReg(0x31, destination, destination);
                    for (int64_t byte = 0; byte < size; ++byte) {
                        emitNativeBaseMemInstruction({0x0F, 0xB6}, 0, 6,
                                                     static_cast<int32_t>(byte), true);
                        if (byte != 0) {
                            emitGroupImmediateRegister(0xC1, 4, 0, static_cast<uint8_t>(byte * 8));
                        }
                        emitRegReg(0x09, 0, destination);
                    }
                    if (instruction.opcode == Opcode::LMS && size < 8) {
                        const uint8_t shift = static_cast<uint8_t>(64 - size * 8);
                        emitGroupImmediateRegister(0xC1, 4, destination, shift);
                        emitGroupImmediateRegister(0xC1, 7, destination, shift);
                    }
                    break;
                }
                case Opcode::ST:
                    requireOperandCount(instruction, 3);
                    emitStore(instruction);
                    break;
                case Opcode::AND: binary(0x21); break;
                case Opcode::OR: binary(0x09); break;
                case Opcode::XOR: binary(0x31); break;
                case Opcode::ADD: binary(0x01); break;
                case Opcode::SUB: binary(0x29); break;
                case Opcode::MUL:
                case Opcode::MULS: {
                    requireOperandCount(instruction, 2);
                    const int destination = guestRegister(instruction, 0);
                    const int source = guestRegister(instruction, 1);
                    emitRex(code_, true, destination, 0, source);
                    append8(code_, 0x0F);
                    append8(code_, 0xAF);
                    append8(code_, static_cast<uint8_t>(0xC0 | ((destination & 7) << 3) | (source & 7)));
                    break;
                }
                case Opcode::DIV:
                case Opcode::DIVS: {
                    requireOperandCount(instruction, 2);
                    const int destination = guestRegister(instruction, 0);
                    const int divisor = guestRegister(instruction, 1);
                    emitRegReg(0x89, destination, 0); // mov rax, dividend
                    if (instruction.opcode == Opcode::DIVS) {
                        // x86 IDIV traps on INT64_MIN / -1, while SimpleASM wraps
                        // the mathematically overflowing quotient back to INT64_MIN.
                        emitRegImmediate(2, UINT64_C(0x8000000000000000));
                        emitRegReg(0x39, 2, 0); // cmp rax, INT64_MIN
                        append8(code_, 0x0F);
                        append8(code_, 0x85); // jne normal division
                        const std::size_t notMinimumField = code_.size();
                        append32(code_, 0);
                        emitGroupImmediateRegister(0x83, 7, divisor, 0xFF); // cmp divisor, -1
                        append8(code_, 0x0F);
                        append8(code_, 0x85); // jne normal division
                        const std::size_t notMinusOneField = code_.size();
                        append32(code_, 0);
                        emitRegReg(0x89, 0, destination); // wrapped result is dividend
                        append8(code_, 0xE9);
                        const std::size_t overflowDoneField = code_.size();
                        append32(code_, 0);
                        const std::size_t normalDivision = code_.size();
                        patch32(code_, notMinimumField, signed32Bits(
                            static_cast<int64_t>(normalDivision) -
                                static_cast<int64_t>(notMinimumField + 4),
                            "Signed division overflow branch"));
                        patch32(code_, notMinusOneField, signed32Bits(
                            static_cast<int64_t>(normalDivision) -
                                static_cast<int64_t>(notMinusOneField + 4),
                            "Signed division overflow branch"));
                        append8(code_, 0x48);
                        append8(code_, 0x99); // cqo
                        emitRex(code_, true, 7, 0, divisor);
                        append8(code_, 0xF7);
                        append8(code_, static_cast<uint8_t>(0xC0 | (7 << 3) | (divisor & 7)));
                        emitRegReg(0x89, 0, destination); // mov quotient, rax
                        const std::size_t divisionDone = code_.size();
                        patch32(code_, overflowDoneField, signed32Bits(
                            static_cast<int64_t>(divisionDone) -
                                static_cast<int64_t>(overflowDoneField + 4),
                            "Signed division completion"));
                    } else {
                        append8(code_, 0x48);
                        append8(code_, 0x31);
                        append8(code_, 0xD2); // xor rdx, rdx
                        emitRex(code_, true, 6, 0, divisor);
                        append8(code_, 0xF7);
                        append8(code_, static_cast<uint8_t>(0xC0 | (6 << 3) | (divisor & 7)));
                        emitRegReg(0x89, 0, destination); // mov quotient, rax
                    }
                    break;
                }
                case Opcode::ADDI:
                case Opcode::SUBI: {
                    requireOperandCount(instruction, 2);
                    const int destination = guestRegister(instruction, 0);
                    emitRegImmediate(0, static_cast<uint64_t>(immediate(instruction, 1)));
                    emitRegReg(instruction.opcode == Opcode::ADDI ? 0x01 : 0x29, 0, destination);
                    break;
                }
                case Opcode::LSH:
                case Opcode::RSH:
                case Opcode::RSHS: {
                    requireOperandCount(instruction, 2);
                    const int destination = guestRegister(instruction, 0);
                    const int64_t shift = immediate(instruction, 1);
                    if (shift < 0 || shift > 63) {
                        throw std::invalid_argument("Shift must be between 0 and 63");
                    }
                    const int group = instruction.opcode == Opcode::LSH ? 4 :
                                      instruction.opcode == Opcode::RSH ? 5 : 7;
                    emitGroupImmediateRegister(0xC1, group, destination,
                                               static_cast<uint8_t>(shift));
                    break;
                }
                case Opcode::CMP:
                case Opcode::CMPS: {
                    requireOperandCount(instruction, 2);
                    const int lhs = guestRegister(instruction, 0);
                    const int rhs = guestRegister(instruction, 1);
                    emitRegReg(0x39, rhs, lhs); // cmp lhs, rhs
                    append8(code_, 0x0F);
                    append8(code_, 0x84); // je equal
                    const std::size_t equalField = code_.size();
                    append32(code_, 0);
                    append8(code_, 0x0F);
                    append8(code_, instruction.opcode == Opcode::CMP ? 0x82 : 0x8C);
                    const std::size_t lessField = code_.size();
                    append32(code_, 0);
                    emitConditionStore(2);
                    append8(code_, 0xE9);
                    const std::size_t doneField = code_.size();
                    append32(code_, 0);
                    const std::size_t lessOffset = code_.size();
                    emitConditionStore(1);
                    append8(code_, 0xE9);
                    const std::size_t lessDoneField = code_.size();
                    append32(code_, 0);
                    const std::size_t equalOffset = code_.size();
                    emitConditionStore(0);
                    const std::size_t doneOffset = code_.size();
                    patch32(code_, equalField, signed32Bits(
                        static_cast<int64_t>(equalOffset) - static_cast<int64_t>(equalField + 4),
                        "Comparison equal branch"));
                    patch32(code_, lessField, signed32Bits(
                        static_cast<int64_t>(lessOffset) - static_cast<int64_t>(lessField + 4),
                        "Comparison less branch"));
                    patch32(code_, doneField, signed32Bits(
                        static_cast<int64_t>(doneOffset) - static_cast<int64_t>(doneField + 4),
                        "Comparison greater branch"));
                    patch32(code_, lessDoneField, signed32Bits(
                        static_cast<int64_t>(doneOffset) - static_cast<int64_t>(lessDoneField + 4),
                        "Comparison less completion"));
                    break;
                }
                case Opcode::BRU:
                    requireOperandCount(instruction, 1);
                    emitBranch(0xFF, branchLabel(instruction, 0));
                    break;
                case Opcode::BRC: {
                    requireOperandCount(instruction, 2);
                    const uint8_t condition = branchCondition(instruction, 1);
                    emitConditionCompare(condition);
                    emitBranch(0x04, branchLabel(instruction, 0)); // je
                    break;
                }
                case Opcode::WRTE:
                    requireOperandCount(instruction, 1);
                    emitTextWrite(instruction);
                    break;
                case Opcode::EXIT:
                    requireOperandCount(instruction, 0);
                    emitRegReg(0x89, nativeRegister(0), 7); // mov rdi, R0
                    append8(code_, 0xB8);
                    append32(code_, 60); // Linux x86-64 exit syscall
                    append8(code_, 0x0F);
                    append8(code_, 0x05);
                    break;
                case Opcode::DEF:
                    throw std::invalid_argument("DEF must be represented in the data section");
            }
        }

        void buildData() {
            for (const auto& entry : program_.dataSection) {
                if (!entry.label.empty()) {
                    dataLabels_[entry.label] = data_.size();
                }
                if (entry.value.type == Type::Text) {
                    for (unsigned char byte : entry.value.textValue) {
                        data_.push_back(byte);
                    }
                    data_.push_back(0);
                } else {
                    append64(data_, static_cast<uint64_t>(entry.value.numericalValue));
                }
            }
            // R8-R15 backing slots, in register order, keep later spill support
            // from changing the layout of the program's data entries.
            data_.resize(data_.size() + (16 - DirectGuestRegisterCount) * 8, 0);
            conditionOffset_ = data_.size();
            data_.resize(data_.size() + 8, 0);
        }

        void buildCode() {
            for (int guest = 0; guest < DirectGuestRegisterCount; ++guest) {
                const int native = nativeRegister(guest);
                emitRegReg(0x31, native, native);
            }
            for (const auto& instruction : program_.codeSection) {
                if (instruction.label) {
                    codeLabels_[*instruction.label] = code_.size();
                }
                emitInstruction(instruction);
            }
            // Reaching the end of .CODE is equivalent to EXIT with the current R0.
            emitRegReg(0x89, nativeRegister(0), 7);
            append8(code_, 0xB8);
            append32(code_, 60);
            append8(code_, 0x0F);
            append8(code_, 0x05);
        }

        void resolveFixups(std::size_t dataFileOffset) {
            for (const auto& fixup : branchFixups_) {
                const auto target = codeLabels_.find(fixup.label);
                if (target == codeLabels_.end()) {
                    throw std::invalid_argument("Unknown code label: " + fixup.label);
                }
                const int64_t displacement = static_cast<int64_t>(target->second) -
                                             static_cast<int64_t>(fixup.fieldOffset + 4);
                patch32(code_, fixup.fieldOffset, signed32Bits(displacement, "Code-label branch"));
            }
            for (const auto& fixup : ripFixups_) {
                const int64_t target = static_cast<int64_t>(dataFileOffset) + fixup.targetDataOffset;
                const int64_t instructionEnd =
                    static_cast<int64_t>(CodeFileOffset + fixup.nextInstructionOffset);
                patch32(code_, fixup.fieldOffset,
                        signed32Bits(target - instructionEnd, "RIP-relative data reference"));
            }
        }

        static std::size_t alignTo(std::size_t value, std::size_t alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        std::vector<uint8_t> buildElf() {
            const std::size_t dataFileOffset = alignTo(CodeFileOffset + code_.size(), 0x1000);
            resolveFixups(dataFileOffset);
            const std::size_t fileSize = dataFileOffset + data_.size();
            std::vector<uint8_t> image(fileSize, 0);

            // ELF64, little-endian, System V ABI, ET_EXEC for x86-64.
            image[0] = 0x7F;
            image[1] = 'E';
            image[2] = 'L';
            image[3] = 'F';
            image[4] = 2;
            image[5] = 1;
            image[6] = 1;
            write16(image, 16, 2); // ET_EXEC
            write16(image, 18, 0x3E); // EM_X86_64
            write32(image, 20, 1);
            write64(image, 24, ImageBase + CodeFileOffset);
            write64(image, 32, 64); // Program-header table follows ELF header.
            write16(image, 52, 64);
            write16(image, 54, 56);
            write16(image, 56, 2);

            // RX load segment includes the ELF headers and machine code.
            write32(image, 64, 1); // PT_LOAD
            write32(image, 68, 5); // PF_R | PF_X
            write64(image, 72, 0);
            write64(image, 80, ImageBase);
            write64(image, 88, ImageBase);
            write64(image, 96, CodeFileOffset + code_.size());
            write64(image, 104, CodeFileOffset + code_.size());
            write64(image, 112, 0x1000);

            // Writable data is page-aligned and remains separate from code.
            const std::size_t dataHeader = 64 + 56;
            write32(image, dataHeader, 1);
            write32(image, dataHeader + 4, 6); // PF_R | PF_W
            write64(image, dataHeader + 8, dataFileOffset);
            write64(image, dataHeader + 16, ImageBase + dataFileOffset);
            write64(image, dataHeader + 24, ImageBase + dataFileOffset);
            write64(image, dataHeader + 32, data_.size());
            write64(image, dataHeader + 40, data_.size());
            write64(image, dataHeader + 48, 0x1000);

            std::copy(code_.begin(), code_.end(), image.begin() + CodeFileOffset);
            std::copy(data_.begin(), data_.end(), image.begin() + dataFileOffset);
            return image;
        }

    public:
        explicit X86CodeGenerator(const Program& program) : program_(program) {}

        std::vector<uint8_t> generate() {
            buildData();
            buildCode();
            return buildElf();
        }
};

class X86Emitter final : public Emitter {
    public:
        std::vector<uint8_t> emit(const Program& program) const override {
            return X86CodeGenerator(program).generate();
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

                // ^ is there sufficient error checking here for a badly formatted input program?

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

        // ^ it's not terrible but can we not combine this with stringToOpcode_?
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
                const std::string regNumText = regStr.substr(1);
                std::size_t parsedChars = 0;
                const int regNum = std::stoi(regNumText, &parsedChars);
                if (parsedChars == regNumText.size() && regNum >= 0 && regNum <= 15) {
                    return static_cast<Register>(regNum);
                }
            }

            throw std::runtime_error("Invalid register identifier: " + regStr);
        }

        static Address parseAddress_(const std::string& token) {
            Address addr;

            auto parseDisplacement = [&](const std::string& expression, std::size_t opPos) {
                std::string valueText = expression.substr(opPos + 1);
                trim_(valueText);
                const Immediate immediate = parseImmediate_(valueText);
                if (immediate.type == Type::Text) {
                    throw std::runtime_error("Address displacement must be numeric: " + token);
                }
                if (expression[opPos] == '-') {
                    if (immediate.numericalValue == INT64_MIN) {
                        throw std::runtime_error("Address displacement is out of range: " + token);
                    }
                    addr.displacement = -immediate.numericalValue;
                } else {
                    addr.displacement = immediate.numericalValue;
                }
            };

            if (token.size() >= 3 && token.front() == '[' && token.back() == ']') {
                std::string inner = token.substr(1, token.size() - 2);
                trim_(inner);
                const std::size_t opPos = inner.find_first_of("+-", 1);
                std::string regText = inner.substr(0, opPos);
                trim_(regText);
                addr.baseRegister = parseRegister_(regText);

                if (opPos != std::string::npos) {
                    parseDisplacement(inner, opPos);
                }
                return addr;
            }

            const std::size_t opPos = token.find_first_of("+-");
            std::string label = token.substr(0, opPos);
            trim_(label);
            addr.label = label;
            if (opPos != std::string::npos) {
                parseDisplacement(token, opPos);
            }
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

            // ^ are we error handling duplicate labels appropriately ?
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

                if (operandText == "Rc" ||
                    (operandText.size() > 1 && operandText[0] == 'R' &&
                     std::all_of(operandText.begin() + 1, operandText.end(), [](unsigned char ch) {
                         return std::isdigit(ch);
                     }))) {
                    inst.operands.push_back(parseRegister_(operandText));
                // ^ ... surely there is a cleaner way to do this
                } else if (operandText[0] == '[' ||
                           std::isupper(static_cast<unsigned char>(operandText[0]))) {
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
                // ^ I guess this is fine. surely there should never be a def without a label in
                // ^ a correct program
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
            if (address->displacement > 0) {
                out << " + " << address->displacement;
            } else if (address->displacement < 0) {
                out << " - " << static_cast<uint64_t>(-(address->displacement + 1)) + 1;
            }
        } else if (address->baseRegister) {
            out << "[" << registerName(*address->baseRegister);
            if (address->displacement > 0) {
                out << " + " << address->displacement;
            } else if (address->displacement < 0) {
                out << " - " << static_cast<uint64_t>(-(address->displacement + 1)) + 1;
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
    if (!outputFile.empty() && (!target.has_value() ||
        (*target != Architecture::Python && *target != Architecture::X86))) {
        std::cerr << "--output is only supported with --emit python or --emit x86\n";
        return 1;
    }

    try {
        Parser parser(sourceFile);
        const Program program = parser.parse();

        if (target.has_value()) {
            const auto emitter = createEmitter(*target);
            const auto output = emitter->emit(program);
            // ^ this is a temporary check, right? will be removed after full implementation
            if (*target == Architecture::Python || *target == Architecture::X86) {
                std::filesystem::path defaultOutputPath(sourceFile);
                // ^ this won't generalize well to adding more architectures, should probably just case it
                defaultOutputPath.replace_extension(*target == Architecture::Python ? ".py" : ".out");
                const std::filesystem::path outputPath = outputFile.empty()
                    ? defaultOutputPath : std::filesystem::path(outputFile);
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
                file.close();
                if (!file) {
                    throw std::runtime_error("Could not close output file: " + outputPath.string());
                }
                // ^ again would make this a case for full implementation
                if (*target == Architecture::X86) {
                    std::error_code permissionError;
                    std::filesystem::permissions(outputPath,
                        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                        std::filesystem::perms::owner_exec | std::filesystem::perms::group_read |
                        std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                        std::filesystem::perms::others_exec,
                        std::filesystem::perm_options::replace, permissionError);
                    if (permissionError) {
                        throw std::runtime_error("Could not make executable file executable: " +
                                                 permissionError.message());
                    }
                    std::cerr << "Wrote x86-64 Linux executable: " << outputPath.string() << '\n';
                    return 0;
                }
                std::cerr << "Wrote Python emulator: " << outputPath.string() << '\n';
                return 0;
            }
            std::cout.write(reinterpret_cast<const char*>(output.data()),
                            static_cast<std::streamsize>(output.size()));
            return 0;
        }

        // ^ don't need this for a final program but nice to have for now
        printProgram(program, std::cout);
    } catch (const std::exception& error) {
        std::cerr << "Compiler error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}