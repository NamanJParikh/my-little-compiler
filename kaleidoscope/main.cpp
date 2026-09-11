#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
static double NumVal;               // filled in with number value

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
    tok_eof = -4
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
    //* this is only positive values ??, need negative sign
    //* this allows numbers like 1.2.3, needs to be fixed
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        while (isdigit(LastChar) || LastChar == '.') {
            NumStr += LastChar;
            LastChar = getchar();
        }
        // convert the string to a double
        NumVal = strtod(NumStr.c_str(), 0);
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
};

class NumberExprAST : public ExprAST {
    private:
        double Val;                                 // Numerical value
    public:
        NumberExprAST(double Val) : Val(Val) {}
};

class VariableExprAST : public ExprAST {
    private:
        std::string Name;                           // Variable name
    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
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
};

class CallExprAST : public ExprAST {
    private:
        std::string Callee;                         // Function name
        std::vector<std::unique_ptr<ExprAST>> Args; // Function inputs
    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
            : Callee(Callee), Args(std::move(Args)) {}
};

class PrototypeAST {
    private:
        std::string Name;                           // Function name
        std::vector<std::string> Args;              // Function inputs
    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args)
            : Name(Name), Args(std::move(Args)) {}

        const std::string &getName() const { return Name; }
};

class FunctionAST {
    private:
        std::unique_ptr<PrototypeAST> Proto;        // Prototype
        std::unique_ptr<ExprAST> Body;              // Body expression
    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

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
static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<ExprAST> ParseNumberExpr() {
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
        if (auto Arg = ParseExpression()) {Args.push_back(std::move(Arg));}
        else {return nullptr;}      // propagate nullptr if ParseExpression fail
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
    auto E = ParseExpression();     // should consume all tokens in the expression
    if (!E) {return nullptr;}       // propagate nullptr if ParseExpression fail
    if (CurTok != ')') {
        return LogError("Unclosed parentheses");
    }
    getNextToken();
    return E;                       // return the expression parsed
}

static std::unique_ptr<ExprAST> ParseSimple() {
    switch (CurTok) {
        case tok_number: return ParseNumberExpr();
        case tok_name: return ParseNameExpr();
        case '(': return ParseParenExpr();
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
    if (BinopPrecedence.contains(CurTok)) {
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
    }

    if (CurTok != ')') {
        return LogErrorP("Unclosed parens in function prototype");
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
  if (ParseFunction()) {
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr\n");
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

int main() {
    // Install standard binary operators.
    BinopPrecedence['<'] = 10;
    BinopPrecedence['>'] = 10;
    BinopPrecedence['='] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 30;
    BinopPrecedence['/'] = 30;
    BinopPrecedence['//'] = 30;
    BinopPrecedence['%'] = 30;

    // Prime the first tokens
    fprintf(stderr, "ready> ");
    getNextToken();

    // Run main interpreter loop
    MainLoop();

    return 0;
}