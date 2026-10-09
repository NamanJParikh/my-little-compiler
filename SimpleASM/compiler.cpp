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
};

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

std::unique_ptr<Emitter> createEmitter(Architecture architecture) {
    switch (architecture) {
        case Architecture::X86:
            return std::make_unique<X86Emitter>();
        case Architecture::ARM:
            return std::make_unique<ARMEmitter>();
        case Architecture::IBMZ:
            return std::make_unique<IBMZEmitter>();
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

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file.s>\n";
        return 1;
    }

    try {
        Parser parser(argv[1]);
        const Program program = parser.parse();
        printProgram(program, std::cout);
    } catch (const std::exception& error) {
        std::cerr << "Parser error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}