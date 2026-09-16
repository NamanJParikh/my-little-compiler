// Compile statement:
// clang++ -g -O3 main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o compile
// clang++ -g -03 main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o compile


#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

/* 
    #############################
    ###         Lexer         ###
    #############################

    For get token, the possible tokens we can read are
     - Numbers (numeric string with <= 1 '.')
     - Variable names (alphanumeric strings starting with a letter)
     - Function names (same as variable names, but followed by '(')
     - Keywords (e.g. 'def')
     - Comments (starting with '#' and possibly multi-line with '#*' and '*#')
     - Special characters (e.g. '+', '=', '(', etc.), may require further parsing
     - End of file (EOF)
*/

static std::string IdentifierStr;   // filled in with variable name
static std::string NumStr;          // filled in with number string

// Possible token types
// Special characters are returned as their ASCII value, comments are skipped
enum Token {
    // Number
    tok_number = -1,
    // Variable/function name
    tok_name = -2,
    // Keywords
    tok_def = -3,
    // EOF
    tok_eof = -4,
    // if, then, else
    tok_if = -5,
    tok_then = -6,
    tok_else = -7,
    // for loop
    tok_for = -8
};

/* 
    ##### gettok ##### 
    Reads the next token from standard input and returns its type, filling in
      IdentifierStr or NumVal if appropriate.
*/

static int gettok() {
    static int LastChar = ' ';

    // Whitespace - ignore
    while (isspace(LastChar)) {
        LastChar = getchar();
    }
    
    // Numbers
    if (isdigit(LastChar) || LastChar == '.' || LastChar == '~') {
        NumStr = "";
        while (isalnum(LastChar) || LastChar == '.' || LastChar == '~') {
            NumStr += LastChar;
            LastChar = getchar();
        }
        return tok_number;
    }

    // Variable/function names or keywords - starting with a letter
    if (isalpha(LastChar)) {
        IdentifierStr = LastChar;
        // read rest of alphanumeric string or underscore for variable/function name
        while (isalnum((LastChar = getchar())) || LastChar == '_') {
            IdentifierStr += LastChar;
        }
        // identify if it's a keyword. if not, it's a variable/function name
        if (IdentifierStr == "def") {
            return tok_def;
        } else if (IdentifierStr == "if") {
            return tok_if;
        } else if (IdentifierStr == "then") {
            return tok_then;
        } else if (IdentifierStr == "else") {
            return tok_else;
        } else if (IdentifierStr == "for") {
            return tok_for;
        } else {
            return tok_name;
        }
    }
    
    // Comments - starting with '#'
    if (LastChar == '#') {
        // is it a multiline comment?
        LastChar = getchar();
        if (LastChar == '*') {
            // skip until the multiline comment ends upon '*#'
            while (true) {
                LastChar = getchar();
                if (LastChar == '*') {
                    LastChar = getchar();
                    if (LastChar == '#') {
                        break;
                    }
                }
            }
        } else {
            // skip until the end of the line
            while (LastChar != EOF && LastChar != '\n' && LastChar != '\r') {
                LastChar = getchar();
            }
        }
        // since we skipped the comment, get the next token and return that
        return gettok();
    }

    // End of file
    if (LastChar == EOF) {
        return tok_eof;
    }

    // Special characters
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;

}

/* 
    #############################
    ###          AST          ###
    #############################

    The most basic form of Kaleidescope contains two types of statements: 
    expressions and function definitions.

    Expressions are built from either numerical values or variable names. Binary 
    operators and functions are applied to build more complex expressions. This
    justifies the following classes:
     - ExprAST: base class for expressions
      - NumberExprAST: numeric values (e.g. "123.4")
      - VariableExprAST: variable names (e.g. "myVar")
      - BinaryExprAST: binary operators (e.g. "myVar + 123.4")
      - CallExprAST: function calls (e.g. "myFunc(myVar, 123.4)")
    
    Functions are defined as a prototype (def funcName(arg1, arg2, ...)) and a 
    body, which is an expression. This justifies the following classes:
     - PrototypeAST: function prototype
     - FunctionAST: function definition (prototype + body expression)
      
*/

namespace {

class ExprAST  {
    public:
        virtual ~ExprAST() = default;
        virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
    private:
        double Val;                                 // Numerical value
    public:
        NumberExprAST(double Val) : Val(Val) {}
        Value *codegen() override;
};

class VariableExprAST : public ExprAST {
    private:
        std::string Name;                           // Variable name
    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
        Value *codegen() override;
};

//* TODO- will later want to extend the character Op to a string to support
//* multi-character operators like ":=", "<=", etc.
class BinaryExprAST : public ExprAST {
    private:
        char Op;                                    // Operator
        std::unique_ptr<ExprAST> LHS, RHS;          // Expressions operator acts on
    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
            : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
        Value *codegen() override;
};

class CallExprAST : public ExprAST {
    private:
        std::string Callee;                         // Function name
        std::vector<std::unique_ptr<ExprAST>> Args; // Function inputs
    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
            : Callee(Callee), Args(std::move(Args)) {}
        Value *codegen() override;
};

class IfThenElseExprAST : public ExprAST {
    private:
        std::unique_ptr<ExprAST> If;
        std::unique_ptr<ExprAST> Then;
        std::unique_ptr<ExprAST> Else;
    public:
        IfThenElseExprAST(
            std::unique_ptr<ExprAST>(If), std::unique_ptr<ExprAST>(Then),
            std::unique_ptr<ExprAST>(Else)) : If(std::move(If)), 
            Then(std::move(Then)), Else(std::move(Else)) {};
        Value *codegen() override;
};

class ForExprAST : public ExprAST {
    private:
        std::string VarName;
        std::unique_ptr<ExprAST> Init, Step, End, Body;
    public:
        ForExprAST(const std::string VarName, std::unique_ptr<ExprAST>(Init),
            std::unique_ptr<ExprAST>(Step), std::unique_ptr<ExprAST>(End),
            std::unique_ptr<ExprAST>(Body)) : VarName(VarName), Init(std::move(Init)),
            Step(std::move(Step)), End(std::move(End)), Body(std::move(Body)) {}
        Value *codegen() override;
};

class PrototypeAST {
    private:
        std::string Name;                           // Function name
        std::vector<std::string> Args;              // Function inputs
    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args)
            : Name(Name), Args(std::move(Args)) {}

        const std::string &getName() const { return Name; }
        virtual Function *codegen();
};

class FunctionAST {
    private:
        std::unique_ptr<PrototypeAST> Proto;        // Prototype
        std::unique_ptr<ExprAST> Body;              // Body expression
    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
        Function *codegen();
};

}

/*
    #############################
    ###      LLVM IR Gen      ###
    #############################

    Defined IR codegen methods for each type of AST node.

    Expressions in the AST are converted to LLVM Values, while prototypes and 
    functions are converted to LLVM Functions.
*/

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *LogErrorV(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

Value *VariableExprAST::codegen() {
    // look up var name in map
    Value *V = NamedValues[Name];
    if (!V) {
        LogErrorV("Unknown variable referenced");
    }
    return V;
}

//* TODO- add more binary operators as desired
Value *BinaryExprAST::codegen() {
    Value *L = LHS->codegen();
    Value *R = RHS->codegen();

    if (!L || !R) {
        return nullptr;
    }

    switch (Op) {
        case '+':
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '/':
            return Builder->CreateFDiv(L, R, "addtmp");
        case '<':
            L = Builder->CreateFCmpULT(L, R, "cmptmp");
            // Convert bool 0/1 to double 0.0 or 1.0
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
                                        "booltmp");
        default:
            return LogErrorV("invalid binary operator");
    }
}

Value *CallExprAST::codegen() {
    // lookup callee in the module
    Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF) {
        return LogErrorV("Unknown function referenced");
    }

    // check number of args is appropriate
    unsigned arg_num = CalleeF->arg_size();
    if (Args.size() != arg_num) {
        return LogErrorV("Incorrect number of arguments passed");
    }

    // generate LLVM IR for each arg and make vector
    std::vector<Value *> ArgsV;
    for (unsigned i = 0; i < arg_num; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfThenElseExprAST::codegen() {
    // codegen relevant expressions
    Value *IfV = If->codegen();
    Value *ThenV = Then->codegen();
    Value *ElseV = Else->codegen();
    if (!IfV || !ThenV || !ElseV) {return nullptr;}

    // floating point not-equal: compare IfV to 0
    // normalizes IfV to 0 if 0 and 1 if non-0
    IfV = Builder->CreateFCmpONE(
        IfV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

    // get parent function, builder is inserting at end of function
    Function *TheFunction = Builder->GetInsertBlock()->getParent();

    // create blocks for then body, else body, and body after the if,then,else
    BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then");
    BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
    BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");
    
    // add conditional branch to function
    Builder->CreateCondBr(IfV, ThenBB, ElseBB);

    // add block at end of function
    TheFunction->insert(TheFunction->end(), ThenBB);
    // add branch to merge block at end of then block
    Builder->SetInsertPoint(ThenBB);
    Builder->CreateBr(MergeBB);
    // update ThenBB to the new then block with the branch to MergeBB
    ThenBB = Builder->GetInsertBlock();

    // repeat for else block
    TheFunction->insert(TheFunction->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);
    Builder->CreateBr(MergeBB);
    ElseBB = Builder->GetInsertBlock();

    // add merge block
    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);
    // create PHI node to selectively update value based on the branch taken
    PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);

    return PN;
}

Value *ForExprAST::codegen() {
    Value *InitV = Init->codegen();
    if (!InitV) {return nullptr;}

    // get parent function
    Function *TheFunction = Builder->GetInsertBlock()->getParent();
    // get block before start of for loop so we can insert at the end of it to repeat
    BasicBlock *PreheaderBB = Builder->GetInsertBlock();
    // add block for the loop
    BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);

    // branch into the loop block and start inserting
    Builder->CreateBr(LoopBB);
    Builder->SetInsertPoint(LoopBB);

    // make a PHI node to maintain the loop variable
    PHINode *Variable = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
    Variable->addIncoming(InitV, PreheaderBB);

    // if loop var shadows an existing var, save the existing value 
    Value *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Variable;

    // implicitly generates code for the body with insert point in loop
    if (!Body->codegen()) {return nullptr;}

    // generate step or set it to 1.0 by default
    Value *StepV = nullptr;
    if (Step) {
        StepV = Step->codegen();
        if (!StepV) {return nullptr;}
    } else {
        StepV = ConstantFP::get(*TheContext, APFloat(1.0));
    }
  
    // add step to loop var to get it's next value
    Value *NextV = Builder->CreateFAdd(Variable, StepV, "nextvar");

    Value *EndCond = End->codegen();
    if (!EndCond) {return nullptr;}
    // make boolean condition just as in if,then,else
    EndCond = Builder->CreateFCmpONE(
        EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");

    // get loop block at end of loop
    BasicBlock *LoopEndBB = Builder->GetInsertBlock();
    BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "afterloop", TheFunction);

    // conditionally branch back to loop or leave loop based on end condition
    Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

    Builder->SetInsertPoint(AfterBB);
    Variable->addIncoming(NextV, LoopEndBB);

    // restore original var if it existed or remove loop var
    if (OldVal)
        NamedValues[VarName] = OldVal;
    else
        NamedValues.erase(VarName);

    // return null
    return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Function *PrototypeAST::codegen() {
    // input arg types, ArgSize doubles
    std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
    // function type: takes Doubles as input, returns Double
    FunctionType *FT = 
        FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

    // prototypes registered in TheModule
    Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    // name args in F
    unsigned idx = 0;
    for (auto &Arg : F->args()) {
        Arg.setName(Args[idx]);
        idx++;
    }

    return F;
}

Function *FunctionAST::codegen() {
    // check if function already defined
    Function *TheFunction = TheModule->getFunction(Proto->getName());
    // if not make the prototype
    if (!TheFunction) {
        TheFunction = Proto->codegen();
    }
    // propagate prototype codegen failure
    if (!TheFunction) {
        return nullptr;
    }
    // if function was already defined, don't allow it to be overriden
    // however, it may have been declared but not defined, which is fine
    // so, enforce that the body is empty
    if (!TheFunction->empty()) {
        return (Function*)LogErrorV("Function cannot be redefined.");
    }

    // make basic block for function body
    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // reset NamedValues so it only knows arguments and vars inside the function
    NamedValues.clear();
    for (auto &Arg : TheFunction->args()) {
        NamedValues[std::string(Arg.getName())] = &Arg;
    }

    // generate body expression
    Value *RetVal = Body->codegen();
    // if error, remove the function before returning
    if (!RetVal) {
        TheFunction->eraseFromParent();
        return nullptr;
    }

    Builder->CreateRet(RetVal);
    verifyFunction(*TheFunction);
    return TheFunction;
}

/*
    ##############################
    ###         Parser         ###
    ##############################

    Below is a top-down logical flow of the parsing.

    If tok_def, parse following function definition
     -> parse prototype
         -> parse function name
         -> '('
         -> parse args list separated by ',' to vector
         -> ')'
     -> parse expression
    
    Otherwise, parse following expression
     -> parse simple expression (store as LHS)
         -> could be number, variable name, function call, or expr in parens
             -> number: stored by lexer in NumVal
             -> variable name: stored by lexer in IdentifierStr
             -> function call: callee stored by lexer in IdentifierStr, parse args
             -> parse expression in parens
     -> loop: is next token a binary operator?
         -> yes: parse following expression as RHS
             -> if next token is another binary operator
                binary operators have "precedence" defining correct order of ops
                 -> if next op is higher precedence (e.g. x+y*z), 
                    recurse with RHS as new LHS to get full RHS
                 -> combine RHS with LHS
         -> no: return LHS

    For the sake of giving LLVM a unified top-level view, we place expressions
    into anonymous functions, i.e. a function with no name or args. This way,
    everything is parsed into a FunctionAST at the highest level.

    Error handling - return nullptr if a parse fails. 
*/

static int CurTok;              // current token to be parsed
static int getNextToken() {     // update CurTok to the next token using lexer
    CurTok = gettok();
    return CurTok;
}

/* 
    ##### Error Handling Helpers ##### 
*/

std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

/* 
    ##### Parsing Simple Expressions ##### 
*/

// will be specified fully later, defined now to allow recursive use 
static std::unique_ptr<ExprAST> ParseFull();

static std::unique_ptr<ExprAST> ParseNumberExpr() {
    // determine if positive or negative number
    // don't need to check if string is empty because this won't run unless at
    // least one character was added to NumStr by lexer
    bool isNegative;
    if (NumStr[0] == '~') {
        isNegative = true;
        NumStr.erase(0, 1);
    } else {
        isNegative = false;
    }

    // assert at most 1 decimal point in number
    if (ptrdiff_t count = std::count(NumStr.begin(), NumStr.end(), '.'); count > 1) {
        return LogError("Multiple decimal points in number");
    }
    // ensure only numbers and . in number (e.g. 12x is not an allowed token)
    for (char n : NumStr) {
        if (!isdigit(n) && n != '.') {
            return LogError("Non-numerical character in number");
        }
    }

    double NumVal = strtod(NumStr.c_str(), 0);
    if (isNegative) {
        NumVal = -1 * NumVal;
    }
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

static std::unique_ptr<ExprAST>  ParseNameExpr() {
    std::string IdName = IdentifierStr;                 // get var or func name
    getNextToken();                                     // consume name

    // is it a function call or just a var?
    if (CurTok != '(') {
        return std::make_unique<VariableExprAST>(IdName);   // just a var name
    }

    getNextToken();                                         // consume '('
    std::vector<std::unique_ptr<ExprAST>> Args;             // args list

    // No args - func()
    if (CurTok == ')') {
        getNextToken();                                     // consume '('
        return std::make_unique<CallExprAST>(IdName, std::move(Args));
    }

    // Parse args
    while (true) {
        if (auto Arg = ParseFull()) {Args.push_back(std::move(Arg));}
        else {return nullptr;}      // propagate nullptr if ParseFull fail
        if (CurTok == ',') {
            getNextToken();
        } else if (CurTok == ')') {
            break;
        } else {
            return LogError("Argument list incorrectly formatted");
        }
    }
    getNextToken();             // consume ')'
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken();                 // consume '('
    auto E = ParseFull();           // should consume all tokens in the expression
    if (!E) {return nullptr;}       // propagate nullptr if ParseFull fail
    if (CurTok != ')') {
        return LogError("Unclosed parentheses");
    }
    getNextToken();
    return E;                       // return the expression parsed
}

static std::unique_ptr<ExprAST> ParseIfThenElseExpr() {
    getNextToken();                 // consume 'if'
    auto If = ParseFull();
    if (!If) {return nullptr;}

    if (CurTok != tok_then) {return LogError("Expected then after if");}
    getNextToken();                 // consume 'then'
    auto Then = ParseFull();
    if (!Then) {return nullptr;}

    if (CurTok != tok_else) {return LogError("Expected else after if, then");}
    getNextToken();                 // consume 'else'
    auto Else = ParseFull();
    if (!Else) {return nullptr;}

    return std::make_unique<IfThenElseExprAST>(
        std::move(If), std::move(Then), std::move(Else));
}

static std::unique_ptr<ExprAST> ParseForExpr() {
    getNextToken();                 // consume 'for'

    if (CurTok != tok_name) {return LogError("Incorrect for loop formatting");}
    std::string VarName = IdentifierStr;
    getNextToken();
    
    if (CurTok != '=') {return LogError("Incorrect for loop formatting");}
    getNextToken();                 // consume '='

    auto Init = ParseFull();
    if (!Init) {return nullptr;}
    
    if (CurTok != ',') {return LogError("Incorrect for loop formatting");}
    getNextToken();                 // consume ','

    auto End = ParseFull();
    if (!End) {return nullptr;}

    // make step optional, can default to 1
    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',') {
        getNextToken();             // consume ','
        auto Step = ParseFull();
        if (!Step) {return nullptr;}
    }

    if (CurTok != ':') {return LogError("Incorrect for loop formatting");}
    getNextToken();                 // consume ':'

    auto Body = ParseFull();
    if (!Body) {return nullptr;}

    return std::make_unique<ForExprAST>(VarName, std::move(Init), std::move(Step),
        std::move(End), std::move(Body));
    
}

static std::unique_ptr<ExprAST> ParseSimple() {
    switch (CurTok) {
        case tok_number: return ParseNumberExpr();
        case tok_name: return ParseNameExpr();
        case '(': return ParseParenExpr();
        case tok_if: return ParseIfThenElseExpr();
        case tok_for: return ParseForExpr();
        default: return LogError("unknown token when expecting an expression");
    }
}

/* 
    ##### Parsing Full Expressions ##### 
*/

// specified later in parsing loop
static std::map<char, int> BinopPrecedence;

// returns precedence of -1 if CurTok is not a valid bin op
static int GetTokPrecedence() {
    if (BinopPrecedence.count(CurTok)) {
        return BinopPrecedence[CurTok];
    } else {
        return -1;
    }
}

// Parse binary operations in order of precedence
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int CurrPrec = GetTokPrecedence();
        // occurs if CurTok not a bin op or LHS was previously an RHS and needs  
        // to be combined with an old LHS before continuing
        if (CurrPrec < ExprPrec) {
            return LHS;
        }

        int BinOp = CurTok;
        getNextToken();                     // consume bin op

        auto RHS = ParseSimple();           // consumes RHS
        if (!RHS) {return nullptr;}         // propagate pointer if parse fails

        int NextPrec = GetTokPrecedence();
        if (NextPrec > CurrPrec) {
            // +1 to ensure RHS only combines if precedence is strictly higher
            // than CurrPrec
            RHS = ParseBinOpRHS(CurrPrec+1, std::move(RHS));
            if (!RHS) {return nullptr;}     // propagate pointer if parse fails
        }

        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

static std::unique_ptr<ExprAST> ParseFull() {
    auto LHS = ParseSimple();
    if (!LHS) {return nullptr;}             // propagate nullptr if fail
    return ParseBinOpRHS(0, std::move(LHS));
}

/* 
    ##### Parsing Top Level Expression ##### 
*/

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    auto E = ParseFull();
    if (!E) {return nullptr;}               // propagate nullptr if fail
    
    // anonymous function prototype with no name or args
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

/* 
    ##### Parsing Functions ##### 
*/

static std::unique_ptr<PrototypeAST> ParseProto() {
    if (CurTok != tok_name) {
        return LogErrorP("Function name not found in prototype");
    }
    std::string FnName = IdentifierStr;     // parse function name
    getNextToken();                         // consume FnName

    if (CurTok != '(') {
        return LogErrorP("Function name not followed by parens");
    }

    std::vector<std::string> ArgNames;      // parse function args
    while (getNextToken() == tok_name) {
        ArgNames.push_back(IdentifierStr);
        if (getNextToken() != ',') {
            break;
        }
    }
    if (CurTok != ')') {
        return LogErrorP("Functions args not separated by commas or unclosed parentheses");
    }

    getNextToken();                         // consume ')'
    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseFunction() {
    getNextToken();                         // consume 'def'
    auto Proto = ParseProto();
    if (!Proto) {return nullptr;}           // propagate nullptr if fail

    auto E = ParseFull();
    if (!E) {return nullptr;}               // propagate nullptr if fail

    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

/* 
    ##### Top Level Parsing ##### 
*/

static void HandleDefinition() {
  if (auto FnAST = ParseFunction()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
        fprintf(stderr, "Read top-level expression:");
        FnIR->print(errs());
        fprintf(stderr, "\n");

        // remove anonymous expression once processed
        FnIR->eraseFromParent();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
            case tok_eof:
                return;
            case ';':               // ignore top-level semicolons.
                getNextToken();
                break;
            case tok_def:
                HandleDefinition();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }
    }
}

/*
    ##############################
    ###         Driver         ###
    ##############################
*/

static void InitializeModule() {
  // Make new context and module
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("my cool jit", *TheContext);

  // Create a new builder for the module
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

int main() {
    // Install standard binary operators.
    BinopPrecedence['<'] = 10;
    BinopPrecedence['>'] = 10;
    BinopPrecedence['='] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 30;
    BinopPrecedence['/'] = 30;
    BinopPrecedence['%'] = 30;

    // Prime the first tokens
    fprintf(stderr, "ready> ");
    getNextToken();

    InitializeModule();

    // Run main interpreter loop
    MainLoop();

    // Print out all of the generated code.
    TheModule->print(errs(), nullptr);

    return 0;
}

