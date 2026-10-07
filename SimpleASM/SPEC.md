# A Simple Assembly Language

The LLVM tutorial focuses on compiling a modern language to LLVM IR. The LLVM 
backend then handles the remaining steps of converting LLVM IR to executable 
machine code. Here, the goal is to open up the backend and compile a simple 
high-level assembly language to executable machine code for at least two modern
architectures. 

I'll first define here a simple register-based high-level assembly language.
The main goals for this language are only that it be Turing complete, easy to 
parse (since parsing is not really the focus of the backend), and be a usable
programming language. 

## Registers

Our simple machine will have 16 general purpose registers labelled R0 to R15. 
The registers will be referred to as is done here: Rx, where x is the register number. 
All general purpose registers are 64-bit. 

There is an additional 2-bit register called the conditional register, referred
to as Rc. This is used for comparisons and conditional control flow.

## Program Structure

Each program is made of two sections: ```.DATA``` and ```.CODE```. ```.DATA```
contains a collection of data, while ```.CODE``` contains executable instructions.

Each line in ```.DATA``` is of the format ```label value```. The data section
is considered to be loaded directly into memory, and the labels can be used to
address the values. 

Each line in ```.CODE``` is of the format ```label instruction operands```. The
labels can be used for control flow to access different sections of code. Program
execution will begin at the start of ```.CODE```.

Labels must be in all caps to distinguish them from the data described below.

## Data Formatting

Decimal values should begin with the letter ```d```.

Bit strings should begin with the letter ```b```.

Hex strings should begin with the letter ```x```.

Text strings should begin with the letter ```t```, followed by text enclosed in
single apostrophes. Text is encoded in ASCII, and whitespace inside the
apostrophes is preserved (for example, ```t'Hello world'```).

## Immediate Values

Immediate values are values defined directly in an instruction. They must follow
the same formatting conventions as above. 

## Addressing

Addresses can of course be explicitly specified. However, they can also use 
displacement with a register. Syntax for displacement is [Rx + I] or [Rx - I], 
where Rx is a register and I is an immediate value. The address is then the 
address contained in Rx adjusted by the value I as specified. Simply addressing
[Rx] takes the address to be the contents of Rx. Additionally, each line of code 
can be labelled. These labels can then be referenced as addresses.

## Return Codes

The return code for the program should be loaded into R0 before using EXIT to
stop execution.

## Comments

A line can be made a comment by starting it with ```*```.

## The Stack and Function Calls

At this point, there is no stack. As such, there is no shorthand instruction for
performing function calls. 

## Instructions

| Instruction | Operands | Action |
| :--- | :---: | ---: |
| DEF | I | Defines an immediate value I at the current address |
| Load & Store | --- | --- |
| LI | Rx, I | Loads immediate value I into Rx |
| LR | Rx, Ry | Loads the contents of Ry into Rx |
| LA | Rx, Addr | Loads the address Addr into Rx |
| LMU | Rx, Addr, n | Loads n bytes from memory at Addr into Rx |
| LMS | Rx, Addr, n | Loads n bytes from memory at Addr into Rx with sign extension |
| ST | Rx, Addr, n | Stores n (least significant) bytes from Rx into memory at Addr |
| Logical Operations | --- | --- |
| AND | Rx, Ry | Loads Rx with Rx AND Ry |
| OR | Rx, Ry | Loads Rx with Rx OR Ry |
| XOR | Rx, Ry | Loads Rx with Rx XOR Ry |
| LSH | Rx, n | Left shifts the contents of Rx by n bits |
| RSH | Rx, n | Right shifts the contents of Rx by n bits |
| RSHS | Rx, n | Right shifts the contents of Rx by n bits with sign extension |
| Arithmetic Operations | --- | --- |
| ADD | Rx, Ry | Loads Rx with Rx + Ry |
| ADDI | Rx, I | Loads Rx with Rx + I, where I is an immediate value |
| SUB | Rx, Ry | Loads Rx with Rx - Ry |
| SUBI | Rx, I | Loads Rx with Rx - I, where I is an immediate value |
| MUL | Rx, Ry | Loads Rx with Rx * Ry, unsigned |
| MULS | Rx, Ry | Loads Rx with Rx * Ry, signed |
| DIV | Rx, Ry | Loads Rx with Rx / Ry using integer division, unsigned |
| DIVS | Rx, Ry | Loads Rx with Rx / Ry using integer division, signed |
| Control Flow | --- | --- |
| CMP | Rx, Ry | Sets Rc to 00 if Rx = Ry, 01 if Rx < Ry, and 10 if Rx > Ry, unsigned |
| CMPS | Rx, Ry | Sets Rc to 00 if Rx = Ry, 01 if Rx < Ry, and 10 if Rx > Ry, signed |
| BRU | Label | Unconditionally branches to Label |
| BRC | Label, CC | Branches to Label if Rc = CC |
| System Calls | --- | --- |
| EXIT | | Stops program execution |
| WRTE | Addr | Writes the text at Addr to the terminal |

## Technical Documentation

### Parser

The parser is implemented by the `Parser` class. It reads the source file one
line at a time, ignores blank lines and lines beginning with `*`, and uses the
`.DATA` and `.CODE` declarations to decide how to interpret subsequent lines.
Data entries are parsed into typed immediate values. Code lines are parsed into
instructions with an optional label and operands represented as registers,
immediates, addresses, or strings (used for values such as condition codes).
Invalid opcodes, malformed data entries, and invalid immediate or register
formats produce parser errors.

`DEF` in the code section is treated as a data definition rather than an
executable instruction. Its immediate is appended to the program's data section;
if the `DEF` line has a label, that label is attached to the new data entry.

### Program Representation

Parsing produces a `Program` containing:

* `dataSection`: an ordered list of `DataEntry` values. Each entry has a label
  (empty for an unlabeled `DEF`) and an `Immediate`, whose type distinguishes
  decimal, binary, hexadecimal, and text values.
* `codeSection`: an ordered list of `Instruction` values. Each instruction has
  an optional label, an `Opcode`, and an ordered list of operands. An operand is
  represented by one of `Register`, `Immediate`, `Address`, or `std::string`.
  Addresses can hold a label or a base register with a displacement.
* `symbolTable`: a map from labels to numeric indices. Data labels map to their
  index in `dataSection`; code labels map to their index in `codeSection`.
  `DEF` labels map to their data index. The map does not record which section
  an index belongs to, so the index must be interpreted with the label's use.